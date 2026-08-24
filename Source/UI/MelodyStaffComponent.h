#pragma once

#include <JuceHeader.h>
#include "../Model/Melody.h"
#include "../Theory/MusicTheory.h"
#include "StaffLayout.h"
#include <functional>
#include <vector>

namespace ui
{

/** Engraves a melody on a single staff, and lets you write one by clicking.

    A melody is monophonic, so one staff with a chosen clef is both the
    conventional way to write it and far cheaper to lay out than braced
    systems. Bars are wrapped into as many systems as the width needs, and each
    system repeats the clef and key signature the way printed music does.

    Note entry follows the step-time model every notation editor uses: pick a
    duration first, then click for pitch. The click's horizontal position picks
    the slot to overwrite and its height picks the pitch, after which the caret
    moves on by the chosen duration.
*/
/** What a click on the staff does.

    Every notation editor separates these two, and for good reason: with only
    an input tool, clicking a note you meant to look at overwrites it. Select is
    the resting state and note input is the mode you deliberately enter, exactly
    as MuseScore, Sibelius and Dorico all do it.
*/
enum class StaffTool { select, write };

//==============================================================================
class MelodyStaffComponent : public juce::Component
{
public:
    MelodyStaffComponent();

    void setMelody (const model::Melody& newMelody);
    const model::Melody& getMelody() const noexcept { return melody; }

    void setScale (const theory::Scale& newScale);

    /** Which rehearsal note is the current target, or -1 for none. Rests and
        the tails of tied notes have no rehearsal index. */
    void setTargetIndex (int rehearsalIndex);

    /** Rehearsal notes already completed, drawn in a settled colour. */
    void setCompletedCount (int count);

    /** Where the transport has reached, in ticks, or -1 to hide the playhead. */
    void setPlayheadTick (int tick);

    /** Black on white, and none of the editing furniture: what gets exported to
        a page rather than shown on screen. */
    void setPrintMode (bool shouldPrint);

    /** How tall the music actually needs to be, in pixels.

        On screen the staff is a fixed readable size and the component grows to
        whatever the music needs, so that a long tune scrolls inside a viewport
        rather than being squeezed until it cannot be read. On paper it is the
        other way round and this is simply the page height.
    */
    int getContentHeight() const noexcept { return contentHeight; }

    /** Asks the owner to scroll so this rectangle is on screen. Fired when the
        caret or the playhead moves somewhere that may be out of view. */
    std::function<void (juce::Rectangle<int> area)> onKeepVisible;

    //==============================================================================
    void setEditEnabled (bool shouldBeEnabled);
    bool isEditEnabled() const noexcept          { return editEnabled; }

    void setTool (StaffTool newTool);
    StaffTool getTool() const noexcept           { return tool; }

    /** The selected span, in ticks. Empty when nothing is selected. */
    bool hasSelection() const noexcept           { return selectionLength > 0; }
    void selectAll();
    void clearSelection();

    /** Undo covers every edit that changes the music - writing, erasing,
        transposing, changing the length or the metre. Without it a selection
        that can delete a whole phrase in one keystroke is a liability. */
    void undo();
    void redo();
    bool canUndo() const noexcept                { return ! undoStack.empty(); }
    bool canRedo() const noexcept                { return ! redoStack.empty(); }

    /** Fired when the tool, the selection or the undo depth changes, so the
        owner can keep its buttons honest. */
    std::function<void()> onEditStateChanged;

    /** The duration the next click will write, dot included. */
    void setNoteValueTicks (int ticks);
    int  getNoteValueTicks() const noexcept      { return noteValueTicks; }

    void setDotted (bool shouldBeDotted);
    bool isDotted() const noexcept               { return dotted; }

    void setRestMode (bool shouldWriteRests);
    bool isRestMode() const noexcept             { return restMode; }

    int  getCaretTick() const noexcept           { return caretTick; }

    /** Fired after any edit, so the owner can update its own controls. */
    std::function<void()> onMelodyChanged;

    /** Fired with the pitch just written, so it can be heard as it is entered. */
    std::function<void (int midiNote)> onNotePreview;

    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    void mouseDown (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    /** One event resolved all the way to where it is drawn. */
    struct LaidOutEvent
    {
        int   startTick      = 0;
        int   lengthTicks    = 0;

        int endTick() const noexcept { return startTick + lengthTicks; }

        int   baseTicks      = 0;
        int   dots           = 0;
        int   flags          = 0;
        bool  isRest         = false;
        bool  tiedToNext     = false;
        bool  isFullBarRest  = false;
        int   midiNote       = 60;
        int   rehearsalIndex = -1;   ///< -1 for rests and tie continuations
        int   colourIndex    = -1;   ///< the note this belongs to, tails included
        int   beamIndex      = -1;   ///< the beam this note is under, or -1
        bool  stemUp         = true;   ///< resolved: a beam decides for its group
        int   measureIndex   = 0;
        int   systemIndex    = 0;
        theory::SpelledNote spelled;
        float slotX          = 0.0f;   ///< start of the slot, before any accidental
        float x              = 0.0f;   ///< left edge of the notehead or rest
        float width          = 0.0f;   ///< advance to the next event, accidental included
        float accidentalAdvance = 0.0f; ///< room reserved in front of the notehead
    };

    /** Notes joined under one beam.

        Everything the drawing needs is worked out once, at layout time, because
        the beam's angle depends on every note under it: you cannot draw one
        note's stem without knowing where the beam will be when it gets there.
    */
    struct BeamGroup
    {
        std::vector<size_t> events;   ///< indices into laidOut, in order
        bool  stemUp = true;
        float startX = 0.0f, endX = 0.0f;   ///< at the stems, not the noteheads
        float startY = 0.0f, endY = 0.0f;   ///< the beam's outer edge
    };

    struct SystemInfo
    {
        int   firstMeasure = 0;
        int   lastMeasure  = 0;
        float bottomLineY  = 0.0f;
        float contentLeft  = 0.0f;
        float contentRight = 0.0f;
    };

    /** Where a point on the staff lands: which slot, and at what pitch. */
    struct HitPosition
    {
        bool  valid       = false;
        int   tick        = 0;
        int   step        = 0;
        int   systemIndex = 0;
        float slotX       = 0.0f;
    };

    void rebuildLayout();
    void buildBeams();
    void keepTickVisible (int tick);

    /** Remembers the current music so the next edit can be taken back. Every
        mutation goes through this, which is why it is the only thing that has
        to be remembered when adding one. */
    void pushUndo();
    void notifyEditState();

    void setSelection (int startTick, int lengthTicks);
    void moveSelection (int direction, bool extend);
    void transposeSelection (int semitones);
    void deleteSelection();
    void drawSelection (juce::Graphics& g, const StaffGeometry& geometry,
                        int systemIndex) const;
    StaffGeometry geometryForSystem (const SystemInfo& system) const;

    void drawSystemFurniture (juce::Graphics& g, const SystemInfo& system,
                              const juce::Font& font, bool isFirstSystem) const;
    void drawEvent (juce::Graphics& g, const StaffGeometry& geometry,
                    const juce::Font& font, const LaidOutEvent& event) const;
    void drawBarLines (juce::Graphics& g, const StaffGeometry& geometry,
                       const SystemInfo& system) const;
    void drawBeams (juce::Graphics& g, const StaffGeometry& geometry, int systemIndex) const;
    void drawTie (juce::Graphics& g, const StaffGeometry& geometry,
                  const LaidOutEvent& from, const LaidOutEvent& to, bool stemUp) const;
    void drawCaret (juce::Graphics& g) const;
    void drawPlayhead (juce::Graphics& g) const;
    bool positionForTick (int tick, float& x, int& systemIndex) const;
    void drawShadowNote (juce::Graphics& g, const juce::Font& font) const;

    HitPosition hitTest (juce::Point<float> position) const;
    const LaidOutEvent* eventForTick (int tick) const;
    void placeAt (int tick, int midiNote, bool rest);
    void setCaretTick (int tick);
    void moveCaret (int direction);
    void nudgeLastNote (int semitones);
    void deleteBeforeCaret();

    juce::Colour colourForEvent (const LaidOutEvent& event) const;
    juce::Colour inkColour() const;
    juce::Colour lineColour() const;
    bool isStemUp (const LaidOutEvent& event) const;

    model::Melody melody;
    theory::Scale scale { 0, 0 };

    int targetIndex    = -1;
    int completedCount = 0;

    bool editEnabled   = false;
    bool printMode     = false;
    int  noteValueTicks = model::ticksPerQuarter;
    bool dotted        = false;
    bool restMode      = false;
    int  caretTick     = 0;
    int  playheadTick  = -1;
    int  lastPlacedTick = -1;
    HitPosition hover;

    float staffSpace = 12.0f;
    std::vector<LaidOutEvent> laidOut;
    std::vector<SystemInfo>   systems;
    int  contentHeight = 0;
    bool resizingToContent = false;

    StaffTool tool = StaffTool::select;
    int selectionStart  = 0;
    int selectionLength = 0;

    std::vector<model::Melody> undoStack, redoStack;
    static constexpr size_t maxUndoDepth = 128;
    std::vector<BeamGroup>    beams;
    std::vector<float>        measureRight;   ///< right edge x of every measure

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyStaffComponent)
};

} // namespace ui
