#pragma once

#include <JuceHeader.h>
#include "Analysis/AudioAnalyser.h"
#include "Audio/PianoSynth.h"
#include "Audio/MelodyPlayer.h"
#include "Audio/Metronome.h"
#include "Audio/PluginHost.h"
#include "Practice/ExerciseGenerator.h"
#include "UI/SettingsPanel.h"
#include "Practice/RehearsalEngine.h"
#include "Theory/MusicTheory.h"
#include "Model/Melody.h"
#include "Model/MidiIO.h"
#include "Model/TuneLibrary.h"
#include "Export/ScorePdf.h"
#include "UI/NoteValueButton.h"
#include "UI/MelodyStaffComponent.h"
#include "UI/NotationComponent.h"
#include "UI/ScaleKeyboardComponent.h"

/** The whole application: audio in, MIDI in, sound out, and everything drawn.

    One audio callback does all the real-time work. Input goes straight to the
    analyser's ring buffer for pitch detection; output is the built-in piano
    plus a hosted instrument, both driven by the same MIDI stream so the
    on-screen keys, a MIDI controller and a plugin all stay in step.
*/
class MainComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      public juce::MidiInputCallback,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // MidiInputCallback
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

private:
    void timerCallback() override;

    /** Fired by the known plugin list, including while a scan is running. */
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    void buildControls();
    void updateScale();
    void showAudioSettings();
    void showPluginScanner();
    void refreshPluginCombo();
    void loadSelectedPlugin();

    void updateMode();
    void buildDemoMelody();

    void buildTuneControls();
    void layoutTuneControls (juce::Rectangle<int> row);
    void syncTuneControls();
    void refreshTuneList();
    void saveCurrentTune();
    void loadSelectedTune();
    void newTune();
    void deleteSelectedTune();
    void applyTuneSettings();
    void previewNote (int midiNote);

    void buildRehearsalControls();
    void layoutRehearsalControls (juce::Rectangle<int> row);
    void syncRehearsalControls();
    void toggleRehearsal();
    void refreshRehearsalNotes();
    void feedMidiToRehearsal();
    void startTimedRehearsal();
    void buildExerciseControls();
    void layoutExerciseControls (juce::Rectangle<int> row);
    void buildPanels();
    void togglePanel (ui::SettingsPanel& panel, juce::Component& anchor);
    void hidePanels();
    void generateExercise();
    void applyClefRangeDefaults();
    void applyGradePreset();
    void markLevelsCustom();
    void hearKey();
    void startPreparation();
    void beginRehearsal();
    void showFileMenu();
    void importMidi();
    void exportMidi();
    void exportPdf();
    void applyTempoAndMetre();

    void paintReadout (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void paintTuner (juce::Graphics& g, juce::Rectangle<float> bounds) const;

    /** The note the readout is talking about: what we heard if anything, and
        otherwise the top note being held down. */
    int getFocusNote() const;

    //==============================================================================
    juce::AudioDeviceManager deviceManager;

    analysis::AudioAnalyser analyser;
    audio::PianoSynth synth;
    audio::PluginHost pluginHost;

    juce::MidiKeyboardState keyboardState;
    juce::MidiMessageCollector midiCollector;
    juce::MidiBuffer incomingMidi;

    theory::Scale scale { 0, 0 };

    //==============================================================================
    ui::NotationComponent notation;
    ui::MelodyStaffComponent melodyStaff;
    ui::ScaleKeyboardComponent keyboard { keyboardState };

    model::Melody melody;
    audio::MelodyPlayer melodyPlayer { keyboardState };
    practice::RehearsalEngine rehearsal;
    audio::Metronome metronome;

    /** Note-ons captured on the audio thread with a transport timestamp.
        Diffing held notes on the UI timer would smear every onset by up to a
        tick, which is far too coarse to grade rhythm against. */
    struct TimedMidiEvent { int note = 0; double transportMs = 0.0; };
    static constexpr int midiRingSize = 256;
    std::array<TimedMidiEvent, midiRingSize> midiRing {};
    std::atomic<uint32_t> midiRingWrite { 0 };
    uint32_t midiRingRead = 0;

    double currentMsPerTick = 1.0;
    double currentCountInMs = 0.0;
    double currentSampleRate = 44100.0;

    juce::TextButton audioSettingsButton { "Audio / MIDI Settings" };
    juce::TextButton pluginScanButton    { "Plugins..." };
    juce::TextButton pluginEditorButton  { "Editor" };
    juce::TextButton pluginUnloadButton  { "Unload" };

    juce::ComboBox rootCombo, scaleCombo, pluginCombo;
    juce::Label rootLabel, scaleLabel;

    // Settings that are chosen once and then left alone. Keeping them a click
    // away rather than permanently on screen is what makes room for the staff.
    ui::SettingsPanel instrumentPanel { "Instrument" };
    ui::SettingsPanel exerciseOptionsPanel { "Exercise options" };
    ui::SettingsPanel matchingPanel { "What counts as the right note" };

    juce::TextButton instrumentButton      { "Instrument..." };
    juce::TextButton exerciseOptionsButton { "Options..." };
    juce::TextButton matchingButton        { "Matching..." };

    juce::OwnedArray<ui::NoteValueButton> valueButtons;
    juce::ToggleButton dotToggle  { "Dot" };
    juce::ToggleButton restToggle { "Rest" };
    juce::ComboBox clefCombo, timeSigCombo, tuneCombo;
    juce::TextEditor nameEditor;
    juce::TextButton saveTuneButton   { "Save" };
    juce::TextButton newTuneButton    { "New" };
    juce::TextButton deleteTuneButton { "Delete" };
    juce::Label clefLabel, timeSigLabel, tuneLabel, entryHintLabel;

    juce::TextButton fileButton { "File..." };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TextButton generateButton { "Generate" };
    juce::ComboBox gradeCombo;
    juce::ComboBox intervalCombo, rhythmCombo, keyLevelCombo, lengthCombo;
    juce::ComboBox exerciseKeyCombo, rangeLowCombo, rangeHighCombo;
    juce::ComboBox metreCombo, minorFormCombo;
    juce::ToggleButton restsToggle       { "Rests" };
    juce::ToggleButton upbeatToggle      { "Upbeat" };
    juce::ToggleButton tiesToggle        { "Ties" };
    juce::ToggleButton syncopationToggle { "Syncopation" };
    juce::ToggleButton chromaticToggle   { "Accidentals" };
    juce::Label exerciseLabel;

    juce::ComboBox modeCombo;
    juce::Slider tempoSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label tempoLabel;
    juce::ToggleButton clickToggle { "Click" };

    juce::TextButton rehearseButton { "Rehearse" };
    juce::TextButton playTuneButton { "Play tune" };
    juce::TextButton cueNoteButton  { "Cue note" };
    juce::TextButton hearKeyButton  { "Hear key" };
    juce::ToggleButton rhythmOnlyToggle { "Rhythm only" };
    juce::ToggleButton prepareToggle    { "Look first" };

    /** Half a minute to read the exercise through before it starts, which is
        both what an exam allows and the part of sight-reading that is actually
        a technique: key, metre, tempo, shape, and the awkward bar. */
    double preparationEndsAtMs = 0.0;
    bool   preparing = false;
    juce::Label progressLabel;
    juce::ToggleButton anyOctaveToggle { "Any octave" };
    juce::ToggleButton voiceSourceToggle { "Voice" };
    juce::ToggleButton midiSourceToggle  { "MIDI" };
    juce::Slider toleranceSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label toleranceLabel;

    juce::TextButton liveModeButton  { "Live" };
    juce::TextButton tuneModeButton  { "Tunes" };
    bool tuneMode = false;

    juce::ToggleButton chordToggle          { "Chord detection" };
    juce::ToggleButton scaleHighlightToggle { "Shade scale on keys" };
    juce::ToggleButton synthToggle          { "Built-in piano" };
    juce::Slider synthGainSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    //==============================================================================
    analysis::AnalysisSnapshot snapshot;
    juce::Array<int> heldMidiNotes;
    juce::Rectangle<int> readoutBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
