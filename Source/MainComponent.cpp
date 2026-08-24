#include "MainComponent.h"
#include "UI/Palette.h"

using namespace ui;

namespace
{
    constexpr int lowestKeyboardNote  = 36;   // C2
    constexpr int highestKeyboardNote = 96;   // C7
}

//==============================================================================
MainComponent::MainComponent()
{
    buildControls();

    addAndMakeVisible (notation);
    addChildComponent (melodyStaff);
    addAndMakeVisible (keyboard);

    buildDemoMelody();
    buildTuneControls();
    buildExerciseControls();
    buildRehearsalControls();
    buildPanels();

    melodyStaff.onMelodyChanged = [this] { syncTuneControls(); refreshRehearsalNotes(); };
    melodyStaff.onNotePreview   = [this] (int note) { previewNote (note); };

    keyboard.setAvailableRange (lowestKeyboardNote, highestKeyboardNote);
    keyboard.setOctaveForMiddleC (4);

    updateScale();

    // Two in, two out: the input side feeds the pitch detector, the output side
    // the piano. A device with no inputs still opens fine - the tuner simply
    // stays quiet.
    deviceManager.initialise (2, 2, nullptr, true);
    deviceManager.addAudioCallback (this);

    // Turn on every MIDI port we can see, so a controller that is already
    // plugged in just works without a trip through the settings dialog.
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled (device.identifier, true);

    deviceManager.addMidiInputDeviceCallback ({}, this);

    // A scan can run for minutes, and adds plugins as it finds them, so follow
    // the list rather than sampling it once.
    juce::Desktop::getInstance().addGlobalMouseListener (this);

    pluginHost.getKnownPluginList().addChangeListener (this);
    refreshPluginCombo();
    updateMode();

    setSize (1180, 820);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();

    juce::Desktop::getInstance().removeGlobalMouseListener (this);

    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);

    pluginHost.getKnownPluginList().removeChangeListener (this);

    pluginHost.savePluginList();
    pluginHost.unloadPlugin();
}

//==============================================================================
void MainComponent::buildControls()
{
    auto addLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, palette::inkDim);
        label.setFont (juce::Font (12.0f));
        addAndMakeVisible (label);
    };

    addAndMakeVisible (audioSettingsButton);
    audioSettingsButton.onClick = [this] { showAudioSettings(); };

    addAndMakeVisible (liveModeButton);
    addAndMakeVisible (tuneModeButton);
    liveModeButton.onClick = [this] { tuneMode = false; updateMode(); };
    tuneModeButton.onClick = [this] { tuneMode = true;  updateMode(); };

    addLabel (rootLabel, "Root");
    addAndMakeVisible (rootCombo);

    for (int i = 0; i < 12; ++i)
        rootCombo.addItem (theory::pitchClassNames[i], i + 1);

    rootCombo.setSelectedId (1, juce::dontSendNotification);
    rootCombo.onChange = [this] { updateScale(); };

    addLabel (scaleLabel, "Scale");
    addAndMakeVisible (scaleCombo);

    const auto& scaleTypes = theory::getScaleTypes();

    for (size_t i = 0; i < scaleTypes.size(); ++i)
        scaleCombo.addItem (scaleTypes[i].name, (int) i + 1);

    scaleCombo.setSelectedId (1, juce::dontSendNotification);
    scaleCombo.onChange = [this] { updateScale(); };

    addAndMakeVisible (instrumentButton);
    instrumentButton.onClick = [this] { togglePanel (instrumentPanel, instrumentButton); };

    addAndMakeVisible (scaleHighlightToggle);
    scaleHighlightToggle.setToggleState (true, juce::dontSendNotification);
    scaleHighlightToggle.onClick = [this]
    {
        keyboard.setScaleHighlightEnabled (scaleHighlightToggle.getToggleState());
    };

    addAndMakeVisible (chordToggle);
    chordToggle.setToggleState (false, juce::dontSendNotification);
    chordToggle.onClick = [this]
    {
        analyser.setChordDetectionEnabled (chordToggle.getToggleState());
    };

    synthToggle.setToggleState (true, juce::dontSendNotification);
    synthToggle.onClick = [this] { synth.setEnabled (synthToggle.getToggleState()); };

    synthGainSlider.setRange (0.0, 1.5, 0.01);
    synthGainSlider.setValue (synth.getGain(), juce::dontSendNotification);
    synthGainSlider.onValueChange = [this] { synth.setGain ((float) synthGainSlider.getValue()); };

    pluginCombo.setTextWhenNothingSelected ("Built-in piano only");
    pluginCombo.onChange = [this] { loadSelectedPlugin(); };

    pluginScanButton.onClick = [this] { showPluginScanner(); };
    pluginEditorButton.onClick = [this] { pluginHost.showEditor(); };

    pluginUnloadButton.onClick = [this]
    {
        pluginHost.unloadPlugin();
        pluginCombo.setSelectedId (0, juce::dontSendNotification);
    };
}

/** Fills the three panels and hides them until they are asked for. */
void MainComponent::buildPanels()
{
    instrumentPanel.addRow ({}, synthToggle);
    instrumentPanel.addRow ("Piano level", synthGainSlider);
    instrumentPanel.addGap();
    instrumentPanel.addRow ("Plugin (VST3)", pluginCombo);
    instrumentPanel.addRow ({}, pluginScanButton);
    instrumentPanel.addRow ({}, pluginEditorButton, pluginUnloadButton);

    exerciseOptionsPanel.addRow ("Key", exerciseKeyCombo);
    exerciseOptionsPanel.addRow ("Metre", metreCombo);
    exerciseOptionsPanel.addRow ("Minor scale", minorFormCombo);
    exerciseOptionsPanel.addRow ("Range", rangeLowCombo, rangeHighCombo);
    exerciseOptionsPanel.addGap();
    exerciseOptionsPanel.addToggles (restsToggle, upbeatToggle);
    exerciseOptionsPanel.addToggles (tiesToggle, syncopationToggle);
    exerciseOptionsPanel.addRow ({}, chromaticToggle);

    matchingPanel.addToggles (midiSourceToggle, voiceSourceToggle);
    matchingPanel.addRow ({}, anyOctaveToggle);
    matchingPanel.addRow ("Tolerance", toleranceSlider);

    for (auto* panel : { &instrumentPanel, &exerciseOptionsPanel, &matchingPanel })
    {
        addChildComponent (*panel);
        panel->setSize (panel->getPreferredWidth(), panel->getPreferredHeight());
    }
}

/** Opens a panel under the button that owns it, or closes it if it is already
    open. Only one is ever up at a time - two overlapping panels would be worse
    than the row of controls they replaced. */
void MainComponent::togglePanel (ui::SettingsPanel& panel, juce::Component& anchor)
{
    const auto wasVisible = panel.isVisible();

    hidePanels();

    if (wasVisible)
        return;

    const auto below = getLocalPoint (anchor.getParentComponent(),
                                      anchor.getBounds().getBottomLeft());

    panel.setTopLeftPosition (juce::jlimit (8, juce::jmax (8, getWidth() - panel.getWidth() - 8),
                                            below.x),
                              below.y + 4);
    panel.setVisible (true);
    panel.toFront (false);
}

void MainComponent::hidePanels()
{
    for (auto* panel : { &instrumentPanel, &exerciseOptionsPanel, &matchingPanel })
        panel->setVisible (false);
}

/** A click anywhere else puts an open panel away, which is what every menu does
    and what anyone will try first.

    It listens globally rather than only to its own clicks, because the click
    that should dismiss a panel usually lands on something else - the staff, a
    button, the keyboard - and those are all children that would swallow it. The
    exceptions are the panel itself, the button that opened it (which would
    otherwise close and reopen it in one click), and anything outside this
    component altogether, which is how a combo box's own popup stays usable.
*/
void MainComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.eventComponent == nullptr || ! isParentOf (event.eventComponent))
        return;

    const auto where = event.getScreenPosition();

    for (auto* panel : { &instrumentPanel, &exerciseOptionsPanel, &matchingPanel })
        if (panel->isVisible() && panel->getScreenBounds().contains (where))
            return;

    for (auto* button : { &instrumentButton, &exerciseOptionsButton, &matchingButton })
        if (button->isVisible() && button->getScreenBounds().contains (where))
            return;

    hidePanels();
}

/** The note-entry palette and the tune library controls. Only shown in Tunes
    mode, where they replace nothing - they get their own row. */
void MainComponent::buildTuneControls()
{
    struct ValueSpec { int baseTicks; const char* name; };

    static const ValueSpec specs[] =
    {
        { 3840, "Whole (1)" }, { 1920, "Half (2)" },  { 960, "Quarter (3)" },
        {  480, "Eighth (4)" }, { 240, "Sixteenth (5)" }
    };

    for (const auto& spec : specs)
    {
        auto* button = new ui::NoteValueButton (spec.baseTicks, spec.name);
        valueButtons.add (button);
        addChildComponent (button);

        const auto ticks = spec.baseTicks;

        button->onClick = [this, ticks]
        {
            melodyStaff.setDotted (false);
            melodyStaff.setNoteValueTicks (ticks);
            syncTuneControls();
        };
    }

    addChildComponent (dotToggle);
    dotToggle.onClick = [this]
    {
        melodyStaff.setDotted (dotToggle.getToggleState());
        syncTuneControls();
    };

    addChildComponent (restToggle);
    restToggle.onClick = [this] { melodyStaff.setRestMode (restToggle.getToggleState()); };

    auto addTuneLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, palette::inkDim);
        label.setFont (juce::Font (12.0f));
        addChildComponent (label);
    };

    addTuneLabel (clefLabel, "Clef");
    addChildComponent (clefCombo);
    clefCombo.addItem ("Treble", 1);
    clefCombo.addItem ("Bass", 2);
    clefCombo.setSelectedId (1, juce::dontSendNotification);
    clefCombo.onChange = [this]
    {
        applyTuneSettings();
        applyClefRangeDefaults();
    };

    addTuneLabel (timeSigLabel, "Time");
    addChildComponent (timeSigCombo);
    {
        auto id = 1;

        for (const auto& signature : model::getWritableMetres())
            timeSigCombo.addItem (juce::String (signature.numerator) + "/"
                                    + juce::String (signature.denominator), id++);
    }

    timeSigCombo.setSelectedId (1, juce::dontSendNotification);
    timeSigCombo.onChange = [this] { applyTuneSettings(); };

    addChildComponent (nameEditor);
    nameEditor.setTextToShowWhenEmpty ("tune name", palette::inkDim);
    nameEditor.setText ("Demo", juce::dontSendNotification);

    addChildComponent (saveTuneButton);
    saveTuneButton.onClick = [this] { saveCurrentTune(); };

    addChildComponent (newTuneButton);
    newTuneButton.onClick = [this] { newTune(); };

    addChildComponent (deleteTuneButton);
    deleteTuneButton.onClick = [this] { deleteSelectedTune(); };

    addChildComponent (fileButton);
    fileButton.onClick = [this] { showFileMenu(); };

    addTuneLabel (tuneLabel, "Open");
    addChildComponent (tuneCombo);
    tuneCombo.setTextWhenNothingSelected ("saved tunes");
    tuneCombo.onChange = [this] { loadSelectedTune(); };

    addTuneLabel (entryHintLabel,
                  "click the staff to write   1-5 value   . dot   R rest   arrows move/nudge   backspace delete");

    refreshTuneList();
    syncTuneControls();
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Import MIDI file...");
    menu.addItem (2, "Export as MIDI...");
    menu.addItem (3, "Export as PDF...");

    juce::Component::SafePointer<MainComponent> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileButton),
                        [safeThis] (int choice)
    {
        if (safeThis == nullptr)
            return;

        if (choice == 1) safeThis->importMidi();
        if (choice == 2) safeThis->exportMidi();
        if (choice == 3) safeThis->exportPdf();
    });
}

void MainComponent::importMidi()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Import a MIDI file",
                                                       juce::File::getSpecialLocation (
                                                           juce::File::userMusicDirectory),
                                                       "*.mid;*.midi");

    juce::Component::SafePointer<MainComponent> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();

        if (file == juce::File())
            return;

        model::Melody imported;
        juce::String report;

        if (! model::MidiIO::importFromFile (file, imported, report))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not import", report);
            return;
        }

        imported.setName (file.getFileNameWithoutExtension());

        safeThis->rehearsal.stop();
        safeThis->melodyPlayer.stop();
        safeThis->metronome.stop();
        safeThis->melodyStaff.setPlayheadTick (-1);

        safeThis->melodyStaff.setMelody (imported);
        safeThis->nameEditor.setText (imported.getName(), juce::dontSendNotification);
        safeThis->syncTuneControls();
        safeThis->refreshRehearsalNotes();

        // Importing always has to make choices - which track, what to do with
        // chords, how to round the timing - so say what they were.
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                file.getFileName(), report);
    });
}

void MainComponent::exportMidi()
{
    const auto suggested = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                             .getChildFile (juce::File::createLegalFileName (
                                 nameEditor.getText().trim().isEmpty() ? juce::String ("Tune")
                                                                       : nameEditor.getText().trim())
                               + ".mid");

    fileChooser = std::make_unique<juce::FileChooser> ("Export as MIDI", suggested, "*.mid");

    juce::Component::SafePointer<MainComponent> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();

        if (file == juce::File())
            return;

        if (! model::MidiIO::exportToFile (safeThis->melodyStaff.getMelody(), file))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not export",
                                                    "Writing " + file.getFullPathName() + " failed.");
    });
}

void MainComponent::exportPdf()
{
    const auto name = nameEditor.getText().trim().isEmpty() ? juce::String ("Tune")
                                                            : nameEditor.getText().trim();

    const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                             .getChildFile (juce::File::createLegalFileName (name) + ".pdf");

    fileChooser = std::make_unique<juce::FileChooser> ("Export as PDF", suggested, "*.pdf");

    juce::Component::SafePointer<MainComponent> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis, name] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();

        if (file == juce::File())
            return;

        if (! exporter::ScorePdf::write (safeThis->melodyStaff.getMelody(),
                                         safeThis->scale, name, file))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not export",
                                                    "Writing " + file.getFullPathName() + " failed.");
    });
}

void MainComponent::layoutTuneControls (juce::Rectangle<int> row)
{
    for (auto* button : valueButtons)
    {
        button->setBounds (row.removeFromLeft (34).reduced (1));
        row.removeFromLeft (2);
    }

    row.removeFromLeft (8);
    dotToggle.setBounds (row.removeFromLeft (56));
    restToggle.setBounds (row.removeFromLeft (62));

    row.removeFromLeft (10);
    clefLabel.setBounds (row.removeFromLeft (32));
    clefCombo.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (6);
    timeSigLabel.setBounds (row.removeFromLeft (34));
    timeSigCombo.setBounds (row.removeFromLeft (70));

    row.removeFromLeft (12);
    nameEditor.setBounds (row.removeFromLeft (130));
    row.removeFromLeft (4);
    saveTuneButton.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (4);
    newTuneButton.setBounds (row.removeFromLeft (52));

    row.removeFromLeft (10);
    tuneLabel.setBounds (row.removeFromLeft (38));
    tuneCombo.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (4);
    deleteTuneButton.setBounds (row.removeFromLeft (62));

    row.removeFromLeft (8);
    fileButton.setBounds (row.removeFromLeft (68));

    row.removeFromLeft (10);
    entryHintLabel.setBounds (row);
}

void MainComponent::syncTuneControls()
{
    const auto ticks  = melodyStaff.getNoteValueTicks();
    const auto isDot  = melodyStaff.isDotted();
    const auto base   = isDot ? (ticks * 2) / 3 : ticks;

    for (auto* button : valueButtons)
        button->setToggleState (button->getBaseTicks() == base, juce::dontSendNotification);

    dotToggle.setToggleState (isDot, juce::dontSendNotification);
    restToggle.setToggleState (melodyStaff.isRestMode(), juce::dontSendNotification);

    const auto& tune = melodyStaff.getMelody();
    clefCombo.setSelectedId (tune.isTrebleClef() ? 1 : 2, juce::dontSendNotification);

    const auto signature = tune.getTimeSignature();
    const auto& metres = model::getWritableMetres();
    auto signatureId = 1;

    for (size_t i = 0; i < metres.size(); ++i)
        if (metres[i] == signature)
            signatureId = (int) i + 1;

    timeSigCombo.setSelectedId (signatureId, juce::dontSendNotification);
}

void MainComponent::applyTuneSettings()
{
    auto tune = melodyStaff.getMelody();

    tune.setTrebleClef (clefCombo.getSelectedId() == 1);

    {
        const auto& metres = model::getWritableMetres();
        const auto chosen = timeSigCombo.getSelectedId() - 1;

        tune.setTimeSignature (chosen >= 0 && chosen < (int) metres.size()
                                 ? metres[(size_t) chosen]
                                 : model::TimeSignature { 4, 4 });
    }

    melodyStaff.setMelody (tune);
}

void MainComponent::refreshTuneList()
{
    const auto previous = tuneCombo.getText();

    tuneCombo.clear (juce::dontSendNotification);

    const auto names = model::TuneLibrary::listTuneNames();

    for (int i = 0; i < names.size(); ++i)
        tuneCombo.addItem (names[i], i + 1);

    if (previous.isNotEmpty())
        for (int i = 0; i < names.size(); ++i)
            if (names[i] == previous)
                tuneCombo.setSelectedId (i + 1, juce::dontSendNotification);
}

void MainComponent::saveCurrentTune()
{
    auto tune = melodyStaff.getMelody();
    tune.setName (nameEditor.getText().trim());

    if (tune.getName().isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                "Name needed", "Give the tune a name before saving.");
        return;
    }

    melodyStaff.setMelody (tune);

    if (! model::TuneLibrary::save (tune))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Could not save",
                                                "Writing to " + model::TuneLibrary::getDirectory()
                                                    .getFullPathName() + " failed.");
        return;
    }

    refreshTuneList();
}

void MainComponent::loadSelectedTune()
{
    const auto name = tuneCombo.getText();

    if (name.isEmpty())
        return;

    model::Melody loaded;

    if (! model::TuneLibrary::load (name, loaded))
        return;

    melodyStaff.setMelody (loaded);
    nameEditor.setText (loaded.getName(), juce::dontSendNotification);
    syncTuneControls();
}

void MainComponent::newTune()
{
    model::Melody fresh;
    fresh.setName ("Untitled");
    fresh.setBarCount (4);

    melodyStaff.setMelody (fresh);
    nameEditor.setText ("Untitled", juce::dontSendNotification);
    tuneCombo.setSelectedId (0, juce::dontSendNotification);
    syncTuneControls();
}

void MainComponent::deleteSelectedTune()
{
    const auto name = tuneCombo.getText();

    if (name.isEmpty())
        return;

    model::TuneLibrary::remove (name);
    tuneCombo.setSelectedId (0, juce::dontSendNotification);
    refreshTuneList();
}

/** Sounds a note as it is written, through the same keyboard state the rest of
    the app uses, so the on-screen keys light up too. */
void MainComponent::previewNote (int midiNote)
{
    keyboardState.noteOn (1, midiNote, 0.75f);

    juce::Component::SafePointer<MainComponent> safeThis (this);

    juce::Timer::callAfterDelay (450, [safeThis, midiNote]
    {
        if (safeThis != nullptr)
            safeThis->keyboardState.noteOff (1, midiNote, 0.0f);
    });
}

/** The sight-reading generator's controls.

    Every dial moves on its own, so awkward rhythms can be practised in an easy
    key or plain rhythms in five flats.
*/
void MainComponent::buildExerciseControls()
{
    addChildComponent (generateButton);
    generateButton.onClick = [this] { generateExercise(); };

    // A grade sets all four dials at once, because that is how lessons work -
    // you read at a level, not at four independent levels. Moving any dial
    // afterwards drops back to Custom rather than leaving the grade menu
    // claiming something that is no longer true.
    addChildComponent (gradeCombo);
    gradeCombo.addItem ("Custom", 1);

    for (int grade = 1; grade <= practice::ExerciseGenerator::numGrades; ++grade)
        gradeCombo.addItem (practice::ExerciseGenerator::describeGrade (grade), grade + 1);

    gradeCombo.setSelectedId (2, juce::dontSendNotification);
    gradeCombo.onChange = [this] { applyGradePreset(); };

    auto fillLevels = [this] (juce::ComboBox& box, juce::String (*describe) (int))
    {
        for (int level = 1; level <= practice::ExerciseGenerator::numLevels; ++level)
            box.addItem (juce::String (level) + "  " + describe (level), level);

        box.setSelectedId (1, juce::dontSendNotification);
        box.onChange = [this] { markLevelsCustom(); };
    };

    addChildComponent (intervalCombo);
    fillLevels (intervalCombo, practice::ExerciseGenerator::describeIntervalLevel);

    addChildComponent (rhythmCombo);
    fillLevels (rhythmCombo, practice::ExerciseGenerator::describeRhythmLevel);

    addChildComponent (keyLevelCombo);
    fillLevels (keyLevelCombo, practice::ExerciseGenerator::describeKeyLevel);

    addChildComponent (lengthCombo);
    fillLevels (lengthCombo, practice::ExerciseGenerator::describeLengthLevel);

    // The features graded reading introduces one at a time. Each is separate
    // because a reader is usually weak at one particular thing - upbeats, or
    // holding a note over a barline - rather than at a whole grade of them.
    juce::ToggleButton* features[] = { &restsToggle, &upbeatToggle, &tiesToggle,
                                       &syncopationToggle, &chromaticToggle };

    for (auto* toggle : features)
        toggle->onClick = [this] { markLevelsCustom(); };

    restsToggle.setTooltip ("A rest to breathe in, where a phrase ends");
    upbeatToggle.setTooltip ("Start before the first barline");
    tiesToggle.setTooltip ("Hold a note across a barline");
    syncopationToggle.setTooltip ("Notes that start off the beat");
    chromaticToggle.setTooltip ("Notes from outside the key");

    metreCombo.addItem ("Metre: any", 1);

    {
        auto id = 2;

        for (const auto& signature : model::getWritableMetres())
            metreCombo.addItem (juce::String (signature.numerator) + "/"
                                  + juce::String (signature.denominator), id++);
    }

    metreCombo.setSelectedId (1, juce::dontSendNotification);
    metreCombo.setTooltip ("Pin the exercise to one time signature, for the week "
                           "the lesson is about that metre");

    minorFormCombo.addItem ("Melodic minor", 1);
    minorFormCombo.addItem ("Harmonic minor", 2);
    minorFormCombo.addItem ("Natural minor", 3);
    minorFormCombo.setSelectedId (1, juce::dontSendNotification);
    minorFormCombo.onChange = [this] { markLevelsCustom(); };


    exerciseKeyCombo.addItem ("Random key", 1);

    for (int i = 0; i < 12; ++i)
        exerciseKeyCombo.addItem (juce::String (theory::pitchClassNames[i]) + " major", i + 2);

    for (int i = 0; i < 12; ++i)
        exerciseKeyCombo.addItem (juce::String (theory::pitchClassNames[i]) + " minor", i + 14);

    exerciseKeyCombo.setSelectedId (1, juce::dontSendNotification);

    addChildComponent (exerciseOptionsButton);
    exerciseOptionsButton.onClick = [this]
    {
        togglePanel (exerciseOptionsPanel, exerciseOptionsButton);
    };

    exerciseLabel.setText ("Exercise", juce::dontSendNotification);
    exerciseLabel.setColour (juce::Label::textColourId, palette::inkDim);
    exerciseLabel.setFont (juce::Font (12.0f));
    addChildComponent (exerciseLabel);

    // The range is what makes a random key usable: without it an exercise can
    // land somewhere you simply cannot sing.
    for (int note = 36; note <= 84; ++note)
    {
        rangeLowCombo.addItem (theory::midiNoteName (note), note);
        rangeHighCombo.addItem (theory::midiNoteName (note), note);
    }

    applyClefRangeDefaults();
    applyGradePreset();
}

/** Sets every dial and switch to what a lesson working at this grade reads. */
void MainComponent::applyGradePreset()
{
    const auto grade = gradeCombo.getSelectedId() - 1;

    if (grade < 1)
        return;                 // Custom: leave the dials where the user put them

    const auto settings = practice::ExerciseGenerator::forGrade (grade);

    intervalCombo.setSelectedId (settings.intervalLevel, juce::dontSendNotification);
    rhythmCombo.setSelectedId   (settings.rhythmLevel,   juce::dontSendNotification);
    keyLevelCombo.setSelectedId (settings.keyLevel,      juce::dontSendNotification);
    lengthCombo.setSelectedId   (settings.lengthLevel,   juce::dontSendNotification);

    restsToggle.setToggleState       (settings.rests,            juce::dontSendNotification);
    upbeatToggle.setToggleState      (settings.upbeat,           juce::dontSendNotification);
    tiesToggle.setToggleState        (settings.tiesOverBarlines, juce::dontSendNotification);
    syncopationToggle.setToggleState (settings.syncopation,      juce::dontSendNotification);
    chromaticToggle.setToggleState   (settings.chromatic,        juce::dontSendNotification);

    minorFormCombo.setSelectedId (settings.minorForm == practice::MinorForm::melodic  ? 1
                                : settings.minorForm == practice::MinorForm::harmonic ? 2 : 3,
                                  juce::dontSendNotification);
}

void MainComponent::markLevelsCustom()
{
    gradeCombo.setSelectedId (1, juce::dontSendNotification);
}

void MainComponent::layoutExerciseControls (juce::Rectangle<int> row)
{
    exerciseLabel.setBounds (row.removeFromLeft (54));
    generateButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (8);

    gradeCombo.setBounds (row.removeFromLeft (330));
    row.removeFromLeft (8);

    intervalCombo.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (4);
    rhythmCombo.setBounds (row.removeFromLeft (160));
    row.removeFromLeft (4);
    keyLevelCombo.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (4);
    lengthCombo.setBounds (row.removeFromLeft (88));

    row.removeFromLeft (8);
    exerciseOptionsButton.setBounds (row.removeFromLeft (96));
}

/** Points the exercise range at whatever the chosen clef reads comfortably.

    Choosing bass clef and then being handed exercises pitched for a soprano is
    no use, and the reverse buries a treble exercise under ledger lines. The
    range follows the clef, and can still be adjusted afterwards.
*/
void MainComponent::applyClefRangeDefaults()
{
    int lowestNote = 0, highestNote = 0;
    theory::getComfortableRange (clefCombo.getSelectedId() == 1, lowestNote, highestNote);

    rangeLowCombo.setSelectedId (lowestNote, juce::dontSendNotification);
    rangeHighCombo.setSelectedId (highestNote, juce::dontSendNotification);
}

void MainComponent::generateExercise()
{
    rehearsal.stop();
    melodyPlayer.stop();
    metronome.stop();
    melodyStaff.setPlayheadTick (-1);

    practice::ExerciseSettings settings;
    settings.intervalLevel = intervalCombo.getSelectedId();
    settings.rhythmLevel   = rhythmCombo.getSelectedId();
    settings.keyLevel      = keyLevelCombo.getSelectedId();
    settings.lengthLevel   = lengthCombo.getSelectedId();
    settings.lowestNote    = rangeLowCombo.getSelectedId();
    settings.highestNote   = rangeHighCombo.getSelectedId();
    settings.tempoBpm      = tempoSlider.getValue();
    settings.trebleClef    = clefCombo.getSelectedId() == 1;

    settings.rests            = restsToggle.getToggleState();
    settings.upbeat           = upbeatToggle.getToggleState();
    settings.tiesOverBarlines = tiesToggle.getToggleState();
    settings.syncopation      = syncopationToggle.getToggleState();
    settings.chromatic        = chromaticToggle.getToggleState();

    settings.minorForm = minorFormCombo.getSelectedId() == 2 ? practice::MinorForm::harmonic
                       : minorFormCombo.getSelectedId() == 3 ? practice::MinorForm::natural
                                                             : practice::MinorForm::melodic;

    const auto metreChoice = metreCombo.getSelectedId() - 2;
    const auto& metres = practice::ExerciseGenerator::getAllMetres();

    if (metreChoice >= 0 && metreChoice < (int) metres.size())
    {
        settings.timeSignatureNumerator   = metres[(size_t) metreChoice].numerator;
        settings.timeSignatureDenominator = metres[(size_t) metreChoice].denominator;
    }

    const auto keyChoice = exerciseKeyCombo.getSelectedId();

    if (keyChoice <= 1)
    {
        settings.randomiseKey = true;
    }
    else if (keyChoice < 14)
    {
        settings.rootPitchClass = keyChoice - 2;
        settings.minorKey = false;
    }
    else
    {
        settings.rootPitchClass = keyChoice - 14;
        settings.minorKey = true;
    }

    const auto exercise = practice::ExerciseGenerator::generate (settings);

    // Point the app's own key selectors at whatever was generated, so the staff
    // spells the accidentals the way the exercise's key demands.
    rootCombo.setSelectedId (exercise.scale.getRootPitchClass() + 1, juce::dontSendNotification);
    scaleCombo.setSelectedId (exercise.scale.getScaleTypeIndex() + 1, juce::dontSendNotification);
    updateScale();

    melodyStaff.setMelody (exercise.melody);
    nameEditor.setText (exercise.description, juce::dontSendNotification);

    // The rhythm dial chooses the metre, so bring the tune controls back in
    // step with what was actually generated.
    syncTuneControls();
    refreshRehearsalNotes();
}

/** Rehearsal transport and the voice-matching settings. */
void MainComponent::buildRehearsalControls()
{
    addChildComponent (rehearseButton);
    rehearseButton.onClick = [this] { toggleRehearsal(); };

    addChildComponent (modeCombo);
    modeCombo.addItem ("Step by step", 1);
    modeCombo.addItem ("In time", 2);
    modeCombo.setSelectedId (1, juce::dontSendNotification);
    modeCombo.onChange = [this]
    {
        rehearsal.setMode (modeCombo.getSelectedId() == 2 ? practice::RehearsalMode::inTime
                                                          : practice::RehearsalMode::stepByStep);
        melodyStaff.setPlayheadTick (-1);
        metronome.stop();
        refreshRehearsalNotes();
    };

    tempoLabel.setText ("Tempo", juce::dontSendNotification);
    tempoLabel.setColour (juce::Label::textColourId, palette::inkDim);
    tempoLabel.setFont (juce::Font (12.0f));
    addChildComponent (tempoLabel);

    addChildComponent (tempoSlider);
    tempoSlider.setRange (30.0, 200.0, 1.0);
    tempoSlider.setValue (melodyStaff.getMelody().getTempoBpm(), juce::dontSendNotification);
    tempoSlider.setTextValueSuffix (" bpm");
    tempoSlider.onValueChange = [this]
    {
        auto tune = melodyStaff.getMelody();
        tune.setTempoBpm (tempoSlider.getValue());
        melodyStaff.setMelody (tune);
        applyTempoAndMetre();
    };

    addChildComponent (clickToggle);
    clickToggle.setToggleState (true, juce::dontSendNotification);
    clickToggle.onClick = [this] { metronome.setClickAudible (clickToggle.getToggleState()); };

    addChildComponent (playTuneButton);
    playTuneButton.onClick = [this]
    {
        melodyPlayer.play (melodyStaff.getMelody());
    };

    addChildComponent (cueNoteButton);
    cueNoteButton.onClick = [this]
    {
        const auto note = rehearsal.getTargetNote();

        if (note >= 0)
            melodyPlayer.playSingleNote (note);
    };

    addChildComponent (hearKeyButton);
    hearKeyButton.setTooltip ("A cadence in the key, then the note you start on");
    hearKeyButton.onClick = [this] { hearKey(); };

    addChildComponent (rhythmOnlyToggle);
    rhythmOnlyToggle.setTooltip ("Work on the rhythm first: any pitch counts, so the "
                                 "tune can be tapped, played on one key, or sung on "
                                 "any note you like");
    rhythmOnlyToggle.onClick = [this]
    {
        rehearsal.setIgnorePitch (rhythmOnlyToggle.getToggleState());
        syncRehearsalControls();
    };

    addChildComponent (prepareToggle);

    // Off by default. Half a minute of enforced staring is right when you are
    // practising for an exam and wrong the first twenty times you press
    // Rehearse to see what it does.
    prepareToggle.setToggleState (false, juce::dontSendNotification);
    prepareToggle.setTooltip ("Half a minute to read it through before it starts, "
                              "the way an exam gives you");

    progressLabel.setColour (juce::Label::textColourId, palette::ink);
    progressLabel.setFont (juce::Font (13.0f));
    addChildComponent (progressLabel);

    anyOctaveToggle.setToggleState (true, juce::dontSendNotification);
    anyOctaveToggle.setTooltip ("Match by note name, ignoring which octave you sing it in");
    anyOctaveToggle.onClick = [this]
    {
        auto settings = rehearsal.getVoiceSettings();
        settings.octaveAgnostic = anyOctaveToggle.getToggleState();
        rehearsal.setVoiceSettings (settings);
    };

    voiceSourceToggle.setToggleState (true, juce::dontSendNotification);
    voiceSourceToggle.onClick = [this]
    {
        rehearsal.setVoiceEnabled (voiceSourceToggle.getToggleState());
    };

    addChildComponent (matchingButton);
    matchingButton.onClick = [this] { togglePanel (matchingPanel, matchingButton); };

    midiSourceToggle.setToggleState (true, juce::dontSendNotification);
    midiSourceToggle.onClick = [this]
    {
        rehearsal.setMidiEnabled (midiSourceToggle.getToggleState());
    };

    toleranceSlider.setRange (10.0, 50.0, 1.0);
    toleranceSlider.setValue (rehearsal.getVoiceSettings().centsTolerance, juce::dontSendNotification);
    toleranceSlider.setTextValueSuffix (" cents");
    toleranceSlider.onValueChange = [this]
    {
        auto settings = rehearsal.getVoiceSettings();
        settings.centsTolerance = toleranceSlider.getValue();
        rehearsal.setVoiceSettings (settings);
    };

    rehearsal.onChanged = [this]
    {
        melodyStaff.setTargetIndex (rehearsal.getTargetIndex());
        melodyStaff.setCompletedCount (rehearsal.getCompletedCount());
        syncRehearsalControls();
    };

    rehearsal.onComplete = [this]
    {
        melodyStaff.setTargetIndex (-1);
        syncRehearsalControls();
    };

    melodyPlayer.onNoteStarted = [this] (int index)
    {
        // While listening back, follow along on the staff rather than leaving
        // the rehearsal highlight where it was.
        if (! rehearsal.isRunning())
            melodyStaff.setTargetIndex (index);
    };

    melodyPlayer.onFinished = [this]
    {
        if (! rehearsal.isRunning())
            melodyStaff.setTargetIndex (-1);
    };

    refreshRehearsalNotes();
    syncRehearsalControls();
}

void MainComponent::layoutRehearsalControls (juce::Rectangle<int> row)
{
    rehearseButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (4);
    playTuneButton.setBounds (row.removeFromLeft (78));
    row.removeFromLeft (4);
    cueNoteButton.setBounds (row.removeFromLeft (74));
    row.removeFromLeft (4);
    hearKeyButton.setBounds (row.removeFromLeft (76));

    row.removeFromLeft (8);
    modeCombo.setBounds (row.removeFromLeft (102));
    row.removeFromLeft (4);
    rhythmOnlyToggle.setBounds (row.removeFromLeft (98));
    prepareToggle.setBounds (row.removeFromLeft (88));

    row.removeFromLeft (6);
    tempoLabel.setBounds (row.removeFromLeft (44));
    tempoSlider.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (4);
    clickToggle.setBounds (row.removeFromLeft (62));

    row.removeFromLeft (8);
    matchingButton.setBounds (row.removeFromLeft (94));

    row.removeFromLeft (10);
    progressLabel.setBounds (row);
}

/** Plays a cadence in the exercise's key, then the note it starts on.

    Sight-singing out of nowhere tests pitch memory rather than reading. Every
    lesson and every exam puts the key in your ear first, so this does too: the
    tonic, subdominant and dominant chords, home again, then the first note on
    its own. In a minor key the dominant takes its raised third, because that is
    what makes a minor cadence sound like one.
*/
void MainComponent::hearKey()
{
    const auto notes = melodyStaff.getMelody().getRehearsalNotes();
    const auto first = notes.empty() ? 60 : notes.front();

    const auto tonicLetter = scale.spell (60 + scale.getRootPitchClass()).letter;
    const auto minor = scale.getScaleTypeIndex() != 0;

    // Put the chords under the first note rather than at some fixed pitch, so
    // they are in the same part of the voice as the exercise.
    auto octave = 4;

    for (int candidate = 1; candidate <= 7; ++candidate)
    {
        const auto tonic = scale.noteForDiatonicStep (candidate * 7 + tonicLetter);

        if (tonic <= first && tonic > first - 12)
        {
            octave = candidate;
            break;
        }
    }

    auto triad = [this, tonicLetter, octave] (int degree)
    {
        std::vector<int> chord;

        for (int tone = 0; tone < 3; ++tone)
            chord.push_back (scale.noteForDiatonicStep (octave * 7 + tonicLetter
                                                          + degree - 1 + tone * 2));

        return chord;
    };

    auto dominant = triad (5);

    if (minor && dominant.size() == 3)
        ++dominant[1];

    melodyPlayer.playChords ({ triad (1), triad (4), dominant, triad (1), { first } });
}

/** Half a minute to look before it starts. */
void MainComponent::startPreparation()
{
    preparing = true;
    preparationEndsAtMs = juce::Time::getMillisecondCounterHiRes() + 30000.0;
    rehearseButton.setButtonText ("Start now");
}

void MainComponent::refreshRehearsalNotes()
{
    // Only the pitches; the due times are worked out when timed rehearsal
    // actually starts, since they depend on the tempo at that moment.
    rehearsal.setNotes (melodyStaff.getMelody().getRehearsalNotes());
    tempoSlider.setValue (melodyStaff.getMelody().getTempoBpm(), juce::dontSendNotification);
    applyTempoAndMetre();
    melodyStaff.setTargetIndex (-1);
    melodyStaff.setCompletedCount (0);
    syncRehearsalControls();
}

/** Sets the metronome to whatever the tune says. The felt beat is a dotted
    quarter in the compound metres and the denominator's unit otherwise. */
void MainComponent::applyTempoAndMetre()
{
    const auto tune = melodyStaff.getMelody();
    const auto signature = tune.getTimeSignature();

    metronome.setTempo (tune.getTempoBpm());

    if (signature.denominator == 8 && signature.numerator % 3 == 0 && signature.numerator > 3)
        metronome.setBeat (signature.numerator / 3, 1.5);
    else
        metronome.setBeat (signature.numerator, 4.0 / signature.denominator);
}

void MainComponent::startTimedRehearsal()
{
    const auto tune = melodyStaff.getMelody();
    const auto signature = tune.getTimeSignature();

    currentMsPerTick = 60000.0 / (juce::jmax (20.0, tune.getTempoBpm())
                                    * (double) model::ticksPerQuarter);

    // One bar of clicks before the tune starts, so there is something to come
    // in on.
    currentCountInMs = signature.barTicks() * currentMsPerTick;

    std::vector<int> pitches;
    std::vector<double> dueTimes;

    for (const auto& note : tune.getPlaybackNotes())
    {
        pitches.push_back (note.midiNote);
        dueTimes.push_back (currentCountInMs + note.startTick * currentMsPerTick);
    }

    rehearsal.setTimedNotes (std::move (pitches), std::move (dueTimes));

    applyTempoAndMetre();
    metronome.start();
    rehearsal.start();
}

void MainComponent::beginRehearsal()
{
    preparing = false;

    if (rehearsal.getMode() == practice::RehearsalMode::inTime)
    {
        melodyPlayer.stop();
        startTimedRehearsal();
    }
    else
    {
        melodyPlayer.stop();
        rehearsal.setNotes (melodyStaff.getMelody().getRehearsalNotes());
        rehearsal.start();
    }

    melodyStaff.setTargetIndex (rehearsal.getTargetIndex());
    melodyStaff.setCompletedCount (rehearsal.getCompletedCount());
    syncRehearsalControls();
}

void MainComponent::toggleRehearsal()
{
    if (preparing)
    {
        // Pressing it again during the reading time means "I am ready".
        beginRehearsal();
        return;
    }

    if (rehearsal.isRunning())
    {
        rehearsal.stop();
        metronome.stop();
        melodyStaff.setPlayheadTick (-1);
    }
    else if (prepareToggle.getToggleState()
              && ! melodyStaff.getMelody().getRehearsalNotes().empty())
    {
        melodyPlayer.stop();
        startPreparation();
        return;
    }
    else
    {
        beginRehearsal();
        return;
    }

    melodyStaff.setTargetIndex (rehearsal.getTargetIndex());
    melodyStaff.setCompletedCount (rehearsal.getCompletedCount());
    syncRehearsalControls();
}

void MainComponent::syncRehearsalControls()
{
    const auto running = rehearsal.isRunning();
    const auto total = (int) rehearsal.getNotes().size();

    if (preparing)
    {
        rehearseButton.setButtonText ("Start now");
        cueNoteButton.setEnabled (false);
        return;                     // the countdown owns the label meanwhile
    }

    rehearseButton.setButtonText (running ? "Stop" : "Rehearse");
    cueNoteButton.setEnabled (running && rehearsal.getTargetNote() >= 0);

    if (total == 0)
    {
        progressLabel.setText ("nothing to rehearse yet", juce::dontSendNotification);
        return;
    }

    if (! running)
    {
        progressLabel.setText (juce::String (total) + " notes ready",
                               juce::dontSendNotification);
        return;
    }

    const auto index = rehearsal.getTargetIndex();

    if (index < 0)
    {
        auto text = "done - " + juce::String (total) + " notes";

        if (rehearsal.getMode() == practice::RehearsalMode::inTime)
        {
            int hits = 0, misses = 0;
            double average = 0.0;
            rehearsal.getTimingSummary (hits, misses, average);

            text = "done - " + juce::String (hits) + " hit";

            if (misses > 0)
                text << ", " << misses << " missed";

            if (hits > 0)
                text << ", average " << (average >= 0.0 ? "+" : "")
                     << juce::String (average, 0) << " ms";
        }
        else if (rehearsal.getTotalWrongNotes() > 0)
        {
            text << ", " << rehearsal.getTotalWrongNotes() << " wrong";
        }

        progressLabel.setText (text, juce::dontSendNotification);
        return;
    }

    if (rehearsal.getMode() == practice::RehearsalMode::inTime
         && metronome.getTransportMs() < currentCountInMs)
    {
        const auto remaining = currentCountInMs - metronome.getTransportMs();
        progressLabel.setText ("count-in... " + juce::String (remaining / 1000.0, 1) + "s",
                               juce::dontSendNotification);
        return;
    }

    auto text = "note " + juce::String (index + 1) + " of " + juce::String (total)
                  + ": " + scale.spell (rehearsal.getTargetNote()).toString();

    if (rehearsal.getMode() == practice::RehearsalMode::inTime)
    {
        int hits = 0, misses = 0;
        double average = 0.0;
        rehearsal.getTimingSummary (hits, misses, average);

        if (hits + misses > 0)
            text << "   " << hits << " hit";

        if (misses > 0)
            text << ", " << misses << " missed";

        if (hits > 0)
            text << ", " << (average >= 0.0 ? "+" : "") << juce::String (average, 0) << " ms";
    }
    else if (rehearsal.getTotalWrongNotes() > 0)
    {
        text << "   (" << rehearsal.getTotalWrongNotes() << " wrong)";
    }

    progressLabel.setText (text, juce::dontSendNotification);
}

/** Drains the note-ons the audio thread captured and hands them to the engine.

    Notes the player itself is sounding are skipped, otherwise listening back
    would rehearse the tune for you.
*/
void MainComponent::feedMidiToRehearsal()
{
    const auto writeIndex = midiRingWrite.load (std::memory_order_acquire);

    // If we ever fell far enough behind to be lapped, drop the backlog rather
    // than replay stale onsets.
    if (writeIndex - midiRingRead > (uint32_t) midiRingSize)
        midiRingRead = writeIndex - midiRingSize;

    const auto timed = rehearsal.getMode() == practice::RehearsalMode::inTime;
    const auto wallClock = juce::Time::getMillisecondCounterHiRes();

    while (midiRingRead != writeIndex)
    {
        const auto event = midiRing[(size_t) (midiRingRead % midiRingSize)];
        ++midiRingRead;

        if (rehearsal.isRunning() && ! melodyPlayer.isPlaying())
            rehearsal.handleMidiNote (event.note, timed ? event.transportMs : wallClock);
    }
}

void MainComponent::updateScale()
{
    scale = theory::Scale (rootCombo.getSelectedId() - 1, scaleCombo.getSelectedId() - 1);

    keyboard.setScale (scale);
    notation.setScale (scale);
    melodyStaff.setScale (scale);
    repaint();
}

void MainComponent::updateMode()
{
    notation.setVisible (! tuneMode);
    melodyStaff.setVisible (tuneMode);
    melodyStaff.setEditEnabled (tuneMode);

    if (! tuneMode)
    {
        rehearsal.stop();
        melodyPlayer.stop();
        metronome.stop();
        melodyStaff.setPlayheadTick (-1);
        preparing = false;
    }

    hidePanels();

    for (auto* button : valueButtons)
        button->setVisible (tuneMode);

    // Only what is on a row. Anything that lives in a panel is shown and
    // hidden by the panel, which is the point of it being there.
    juce::Component* tuneControls[] =
    {
        &dotToggle, &restToggle, &clefCombo, &timeSigCombo, &tuneCombo, &nameEditor,
        &saveTuneButton, &newTuneButton, &deleteTuneButton,
        &clefLabel, &timeSigLabel, &tuneLabel, &entryHintLabel, &fileButton,
        &rehearseButton, &playTuneButton, &cueNoteButton, &progressLabel,
        &modeCombo, &tempoLabel, &tempoSlider, &clickToggle, &matchingButton,
        &generateButton, &gradeCombo, &intervalCombo, &rhythmCombo, &keyLevelCombo,
        &lengthCombo, &exerciseLabel, &exerciseOptionsButton,
        &hearKeyButton, &rhythmOnlyToggle, &prepareToggle
    };

    for (auto* control : tuneControls)
        control->setVisible (tuneMode);

    liveModeButton.setToggleState (! tuneMode, juce::dontSendNotification);
    tuneModeButton.setToggleState (tuneMode, juce::dontSendNotification);

    resized();
    repaint();
}

/** A short tune that deliberately exercises the awkward parts of the engraver:
    a note tied across a bar line, an accidental, dotted and flagged values, and
    a final barline. */
void MainComponent::buildDemoMelody()
{
    melody = model::Melody();
    melody.setName ("Demo");
    melody.setTimeSignature ({ 4, 4 });
    melody.setBarCount (4);
    melody.setTempoBpm (96.0);

    const auto quarter = model::ticksPerQuarter;
    const auto eighth  = quarter / 2;
    const auto half    = quarter * 2;

    melody.placeEvent (0,               quarter, 60, false);   // C4
    melody.placeEvent (quarter,         quarter, 62, false);   // D4
    melody.placeEvent (quarter * 2,     quarter, 64, false);   // E4
    melody.placeEvent (quarter * 3,     quarter, 65, false);   // F4

    melody.placeEvent (quarter * 4,     half,    67, false);   // G4
    melody.placeEvent (quarter * 6,     eighth,  69, false);   // A4
    melody.placeEvent (quarter * 6 + eighth, eighth, 71, false);
    melody.placeEvent (quarter * 7,     half,    72, false);   // C5, ties over the bar line

    melody.placeEvent (quarter * 9,     eighth,  69, false);
    melody.placeEvent (quarter * 9 + eighth, eighth, 67, false);
    melody.placeEvent (quarter * 10,    quarter, 66, false);   // F#4, needs an accidental
    melody.placeEvent (quarter * 11,    quarter, 64, false);

    melody.placeEvent (quarter * 12,    quarter + eighth, 62, false);   // dotted quarter
    melody.placeEvent (quarter * 13 + eighth, eighth, 62, false);
    melody.placeEvent (quarter * 14,    half,    60, false);

    melodyStaff.setMelody (melody);
}

//==============================================================================
void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        deviceManager,
        0, 2,      // input channels: 0 to 2
        0, 2,      // output channels: 0 to 2
        true,      // show MIDI input options
        false,     // no MIDI output
        true,      // stereo pairs
        false);    // show advanced options inline

    selector->setSize (500, 450);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio / MIDI Settings";
    options.dialogBackgroundColour = palette::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}

void MainComponent::showPluginScanner()
{
    auto list = std::make_unique<juce::PluginListComponent> (pluginHost.getFormatManager(),
                                                             pluginHost.getKnownPluginList(),
                                                             pluginHost.getDeadMansPedalFile(),
                                                             pluginHost.getPropertiesFile(),
                                                             true);
    list->setSize (700, 500);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (list.release());
    options.dialogTitle = "VST3 Instruments";
    options.dialogBackgroundColour = palette::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &pluginHost.getKnownPluginList())
        refreshPluginCombo();
}

void MainComponent::refreshPluginCombo()
{
    const auto selected = pluginCombo.getSelectedId();

    pluginCombo.clear (juce::dontSendNotification);

    const auto types = pluginHost.getKnownPluginList().getTypes();

    for (int i = 0; i < types.size(); ++i)
        if (types[i].isInstrument)
            pluginCombo.addItem (types[i].name, i + 1);

    if (selected > 0)
        pluginCombo.setSelectedId (selected, juce::dontSendNotification);
}

void MainComponent::loadSelectedPlugin()
{
    const auto index = pluginCombo.getSelectedId() - 1;
    const auto types = pluginHost.getKnownPluginList().getTypes();

    if (index < 0 || index >= types.size())
        return;

    const auto error = pluginHost.loadPlugin (types[index]);

    if (error.isNotEmpty())
    {
        pluginCombo.setSelectedId (0, juce::dontSendNotification);

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Could not load plugin", error);
    }
}

//==============================================================================
void MainComponent::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto sampleRate = device->getCurrentSampleRate();
    const auto blockSize  = device->getCurrentBufferSizeSamples();

    analyser.prepare (sampleRate);
    metronome.prepare (sampleRate);
    synth.prepare (sampleRate, blockSize);
    pluginHost.prepare (sampleRate, blockSize);

    currentSampleRate = sampleRate;
    midiCollector.reset (sampleRate);
    incomingMidi.ensureSize (2048);
}

void MainComponent::audioDeviceStopped()
{
    synth.allNotesOff();
    pluginHost.releaseResources();
    analyser.reset();
}

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                      int numInputChannels,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    analyser.pushAudio (inputChannelData, numInputChannels, numSamples);

    juce::AudioBuffer<float> output (outputChannelData, numOutputChannels, numSamples);
    output.clear();

    incomingMidi.clear();
    midiCollector.removeNextBlockOfMessages (incomingMidi, numSamples);

    // Merges in anything clicked on the on-screen keyboard, and updates the
    // shared key state from the hardware messages in the same pass.
    keyboardState.processNextMidiBuffer (incomingMidi, 0, numSamples, true);

    // Timestamp note-ons against the transport before the metronome advances
    // it, so each one carries the moment it actually happened.
    const auto transportAtBlockStart = metronome.getTransportMs();
    const auto sampleRate = juce::jmax (1.0, currentSampleRate);

    for (const auto metadata : incomingMidi)
    {
        const auto message = metadata.getMessage();

        if (! message.isNoteOn())
            continue;

        const auto index = midiRingWrite.load (std::memory_order_relaxed);
        auto& slot = midiRing[(size_t) (index % midiRingSize)];
        slot.note        = message.getNoteNumber();
        slot.transportMs = transportAtBlockStart + metadata.samplePosition * 1000.0 / sampleRate;
        midiRingWrite.store (index + 1, std::memory_order_release);
    }

    synth.renderNextBlock (output, incomingMidi);
    pluginHost.process (output, incomingMidi);
    metronome.process (output);
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    midiCollector.addMessageToQueue (message);
}

//==============================================================================
void MainComponent::timerCallback()
{
    snapshot = analyser.getSnapshot();

    heldMidiNotes.clearQuick();

    for (int note = 0; note < 128; ++note)
        if (keyboardState.isNoteOnForChannels (0xffff, note))
            heldMidiNotes.add (note);

    const auto timedMode = rehearsal.getMode() == practice::RehearsalMode::inTime;
    const auto transportMs = metronome.getTransportMs();

    feedMidiToRehearsal();

    if (preparing)
    {
        const auto remaining = preparationEndsAtMs - juce::Time::getMillisecondCounterHiRes();

        if (remaining <= 0.0)
        {
            beginRehearsal();
        }
        else
        {
            // The checklist is the technique. Reading it every time is the
            // point - it is what stops the eye starting at bar one, note one.
            progressLabel.setText ("read it through - "
                                     + juce::String ((int) std::ceil (remaining / 1000.0))
                                     + "s: key, metre, tempo, shape, the awkward bar",
                                   juce::dontSendNotification);
        }
    }

    if (rehearsal.isRunning() && timedMode)
    {
        rehearsal.advanceTransport (transportMs);

        const auto tickNow = (int) ((transportMs - currentCountInMs) / currentMsPerTick);
        melodyStaff.setPlayheadTick (transportMs >= currentCountInMs ? tickNow : -1);
    }

    if (rehearsal.isRunning() && ! melodyPlayer.isPlaying())
        rehearsal.handleVoicePitch (snapshot.hasPitch, snapshot.nearestNote, snapshot.cents,
                                    snapshot.confidence,
                                    timedMode ? transportMs
                                              : juce::Time::getMillisecondCounterHiRes());

    const auto detected = snapshot.hasPitch ? snapshot.nearestNote : -1;

    notation.setMidiNotes (heldMidiNotes);
    notation.setDetectedNote (detected);
    notation.setChordNotes (snapshot.chordNotes);

    keyboard.setDetectedNote (detected);
    keyboard.setChordNotes (snapshot.chordNotes);

    pluginEditorButton.setEnabled (pluginHost.hasPlugin());
    pluginUnloadButton.setEnabled (pluginHost.hasPlugin());

    repaint (readoutBounds);
}

int MainComponent::getFocusNote() const
{
    if (snapshot.hasPitch)
        return snapshot.nearestNote;

    return heldMidiNotes.isEmpty() ? -1 : heldMidiNotes.getLast();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);
    paintReadout (g, readoutBounds);
}

void MainComponent::paintReadout (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    if (bounds.isEmpty())
        return;

    g.setColour (palette::panel);
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

    auto area = bounds.reduced (16, 10);
    const auto focusNote = getFocusNote();

    // Left: the big note name, coloured by where it came from.
    auto nameArea = area.removeFromLeft (juce::jmin (220, area.getWidth() / 3));

    if (focusNote >= 0)
    {
        const auto spelled = scale.spell (focusNote);

        g.setColour (snapshot.hasPitch ? palette::detectedNote : palette::midiNote);
        g.setFont (juce::Font (52.0f, juce::Font::bold));
        g.drawText (spelled.toString(), nameArea, juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (palette::inkDim);
        g.setFont (juce::Font (22.0f));
        g.drawText ("--", nameArea, juce::Justification::centredLeft, false);
    }

    // Middle: the tuning meter, only meaningful for a detected pitch.
    auto tunerArea = area.removeFromLeft (juce::jmin (320, area.getWidth() / 2));
    paintTuner (g, tunerArea.toFloat().reduced (6.0f, 18.0f));

    // Right: what the note means in the current scale.
    g.setFont (juce::Font (14.0f));

    juce::StringArray lines;

    if (focusNote >= 0)
    {
        const auto degree = scale.degreeName (focusNote);

        lines.add (degree.isNotEmpty()
                     ? "Degree " + degree + " of " + scale.getName()
                     : "Outside " + scale.getName());

        lines.add (scale.intervalName (focusNote) + " above " + scale.spell (60 + scale.getRootPitchClass()).toString (false));
    }
    else
    {
        lines.add (scale.getName());
    }

    if (snapshot.hasPitch)
        lines.add (juce::String (snapshot.frequency, 1) + " Hz   "
                     + juce::String (snapshot.levelDb, 1) + " dB   conf "
                     + juce::String (snapshot.confidence, 2));
    else
        lines.add (juce::String (snapshot.levelDb, 1) + " dB   (no pitch)");

    if (! snapshot.chordNotes.empty())
    {
        juce::StringArray names;

        for (auto note : snapshot.chordNotes)
            names.add (scale.spell (note).toString (false));

        lines.add ("Heard: " + names.joinIntoString (" "));
    }

    auto textArea = area;

    for (int i = 0; i < lines.size(); ++i)
    {
        g.setColour (i == 0 ? palette::ink : palette::inkDim);
        g.drawText (lines[i], textArea.removeFromTop (20), juce::Justification::centredLeft, false);
    }
}

void MainComponent::paintTuner (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    if (bounds.getWidth() < 40.0f)
        return;

    const auto centreX = bounds.getCentreX();
    const auto trackY  = bounds.getCentreY();

    g.setColour (palette::background);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Tick marks every 10 cents across a +/- 50 cent span.
    g.setColour (palette::inkDim.withAlpha (0.4f));

    for (int cents = -50; cents <= 50; cents += 10)
    {
        const auto x = centreX + bounds.getWidth() * 0.5f * (float) cents / 50.0f;
        const auto height = cents == 0 ? bounds.getHeight() * 0.5f : bounds.getHeight() * 0.25f;

        g.fillRect (x - 0.5f, trackY - height * 0.5f, 1.0f, height);
    }

    if (! snapshot.hasPitch)
    {
        g.setColour (palette::inkDim);
        g.setFont (juce::Font (11.0f));
        g.drawText ("cents", bounds, juce::Justification::centredBottom, false);
        return;
    }

    const auto cents = juce::jlimit (-50.0, 50.0, snapshot.cents);
    const auto x = centreX + bounds.getWidth() * 0.5f * (float) cents / 50.0f;

    // Green within five cents, sliding to red at the edges - the same rule a
    // hardware tuner uses.
    const auto error = juce::jlimit (0.0f, 1.0f, (float) std::abs (cents) / 25.0f);
    const auto colour = palette::inTune.interpolatedWith (palette::outOfTune, error);

    g.setColour (colour);
    g.fillRoundedRectangle (juce::Rectangle<float> (x - 2.0f, bounds.getY(),
                                                    4.0f, bounds.getHeight()), 2.0f);

    g.setFont (juce::Font (11.0f));
    g.drawText ((cents >= 0.0 ? "+" : "") + juce::String (cents, 1) + " cents",
                bounds, juce::Justification::centredBottom, false);
}

//==============================================================================
void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    // Top row: device settings and scale selection.
    auto topRow = area.removeFromTop (30);

    audioSettingsButton.setBounds (topRow.removeFromLeft (160));
    topRow.removeFromLeft (12);

    liveModeButton.setBounds (topRow.removeFromLeft (60));
    tuneModeButton.setBounds (topRow.removeFromLeft (68));
    topRow.removeFromLeft (12);

    instrumentButton.setBounds (topRow.removeFromLeft (110));
    topRow.removeFromLeft (16);

    rootLabel.setBounds (topRow.removeFromLeft (34));
    rootCombo.setBounds (topRow.removeFromLeft (70));
    topRow.removeFromLeft (10);

    scaleLabel.setBounds (topRow.removeFromLeft (38));
    scaleCombo.setBounds (topRow.removeFromLeft (150));
    topRow.removeFromLeft (16);

    scaleHighlightToggle.setBounds (topRow.removeFromLeft (150));
    chordToggle.setBounds (topRow.removeFromLeft (130));

    area.removeFromTop (8);

    if (tuneMode)
    {
        layoutTuneControls (area.removeFromTop (30));
        area.removeFromTop (6);
        layoutExerciseControls (area.removeFromTop (28));
        area.removeFromTop (6);
        layoutRehearsalControls (area.removeFromTop (28));
        area.removeFromTop (8);
    }

    readoutBounds = area.removeFromTop (100);

    area.removeFromTop (10);

    // The keyboard keeps a fixed slice at the bottom; the staff takes the rest.
    keyboard.setBounds (area.removeFromBottom (juce::jmax (110, area.getHeight() / 4)));

    // Stretch the keys to fill that slice. Without this the keyboard draws at
    // its default key width and leaves a blank strip down the right-hand side.
    const auto numWhiteKeys = [this]
    {
        int count = 0;

        for (int note = lowestKeyboardNote; note <= highestKeyboardNote; ++note)
            if (! juce::MidiMessage::isMidiNoteBlack (note))
                ++count;

        return count;
    }();

    if (numWhiteKeys > 0)
        keyboard.setKeyWidth ((float) keyboard.getWidth() / (float) numWhiteKeys);

    area.removeFromBottom (10);
    notation.setBounds (area);
    melodyStaff.setBounds (area);
}
