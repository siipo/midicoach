/*  Tests for the staff editor: tools, selection, hotkeys and undo.

    These drive the real key handler with real KeyPress objects rather than
    poking at private state, because the thing worth checking is exactly what a
    keystroke does - which is also the part that is tedious and unreliable to
    verify by clicking around a running window.

    Build target: EditorTests. Returns non-zero if anything fails.
*/

#include <JuceHeader.h>
#include "../Source/UI/MelodyStaffComponent.h"

#include <iostream>

namespace
{
    int failures = 0;
    int checks   = 0;

    void check (bool condition, const juce::String& what)
    {
        ++checks;

        if (! condition)
        {
            ++failures;
            std::cout << "  FAIL: " << what << std::endl;
        }
    }

    void checkEqual (int actual, int expected, const juce::String& what)
    {
        check (actual == expected,
               what + " (expected " + juce::String (expected)
                    + ", got " + juce::String (actual) + ")");
    }

    /** Four quarter notes in one bar, which is enough to select across. */
    model::Melody makeTune()
    {
        model::Melody melody;
        melody.setTimeSignature ({ 4, 4 });
        melody.setBarCount (1);

        const auto quarter = model::ticksPerQuarter;

        for (int i = 0; i < 4; ++i)
            melody.placeEvent (i * quarter, quarter, 60 + i * 2, false);

        return melody;
    }

    juce::KeyPress press (int code, juce::ModifierKeys mods = juce::ModifierKeys())
    {
        return juce::KeyPress (code, mods, 0);
    }

    const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };
    const juce::ModifierKeys command { juce::ModifierKeys::commandModifier };
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "EditorTests" << std::endl;

    // A staff big enough to lay something out on. Nothing here is drawn, but the
    // layout has to run for the selection to have events to move between.
    auto makeStaff = [] (ui::MelodyStaffComponent& staff)
    {
        staff.setSize (900, 260);
        staff.setEditEnabled (true);
        staff.setMelody (makeTune());
    };

    // -- select is the resting tool --------------------------------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        check (staff.getTool() == ui::StaffTool::select,
               "the editor starts in select, not in note input");
        check (! staff.hasSelection(), "with nothing selected");
    }

    // -- N and Escape move between the tools -----------------------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        check (staff.keyPressed (press ('N')), "N is handled");
        check (staff.getTool() == ui::StaffTool::write, "and enters note input");

        check (staff.keyPressed (press (juce::KeyPress::escapeKey)), "escape is handled");
        check (staff.getTool() == ui::StaffTool::select, "and leaves note input");

        staff.keyPressed (press ('N'));
        staff.keyPressed (press ('N'));
        check (staff.getTool() == ui::StaffTool::select, "N toggles back out too");
    }

    // -- arrows move the selection, shifted arrows stretch it -------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        staff.keyPressed (press (juce::KeyPress::rightKey));
        check (staff.hasSelection(), "the first arrow selects something");

        // Moving keeps the selection one note wide; extending does not. The two
        // are told apart by what the transpose then does, which is the only
        // externally visible difference.
        staff.keyPressed (press (juce::KeyPress::rightKey));
        staff.keyPressed (press (juce::KeyPress::upKey));

        auto moved = 0;

        for (auto note : staff.getMelody().getRehearsalNotes())
            if (note % 2 != 0)
                ++moved;

        checkEqual (moved, 1, "a plain arrow leaves one note selected");
    }

    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        staff.keyPressed (press (juce::KeyPress::rightKey));
        staff.keyPressed (press (juce::KeyPress::rightKey, shift));
        staff.keyPressed (press (juce::KeyPress::rightKey, shift));
        staff.keyPressed (press (juce::KeyPress::upKey));

        auto moved = 0;

        for (auto note : staff.getMelody().getRehearsalNotes())
            if (note % 2 != 0)
                ++moved;

        checkEqual (moved, 3, "shifted arrows extend the selection a note at a time");
    }

    // -- transposing ------------------------------------------------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        staff.keyPressed (press ((int) 'A', command));
        check (staff.hasSelection(), "ctrl+A selects the tune");

        staff.keyPressed (press (juce::KeyPress::upKey, command));

        const auto notes = staff.getMelody().getRehearsalNotes();
        checkEqual ((int) notes.size(), 4, "all four notes are still there");

        if (notes.size() == 4)
            checkEqual (notes[0], 72, "ctrl+up moves the selection an octave");

        staff.keyPressed (press (juce::KeyPress::downKey));
        checkEqual (staff.getMelody().getRehearsalNotes()[0], 71, "a plain arrow is a semitone");
    }

    // -- delete turns the selection into rests ----------------------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        staff.keyPressed (press ((int) 'A', command));
        staff.keyPressed (press (juce::KeyPress::deleteKey));

        checkEqual ((int) staff.getMelody().getRehearsalNotes().size(), 0,
                    "deleting the whole selection leaves no notes");

        auto rests = 0;

        for (const auto& event : staff.getMelody().getEvents())
            if (event.isRest)
                ++rests;

        check (rests > 0, "and leaves rests in their place");
    }

    // -- undo, which is what makes a destructive selection safe ------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        check (! staff.canUndo(), "a freshly loaded tune has nothing to undo");

        staff.keyPressed (press ((int) 'A', command));
        staff.keyPressed (press (juce::KeyPress::deleteKey));

        check (staff.canUndo(), "deleting can be undone");
        checkEqual ((int) staff.getMelody().getRehearsalNotes().size(), 0, "and did delete");

        staff.keyPressed (press ((int) 'Z', command));
        checkEqual ((int) staff.getMelody().getRehearsalNotes().size(), 4,
                    "undo brings the notes back");
        check (staff.canRedo(), "and can be redone");

        staff.keyPressed (press ((int) 'Y', command));
        checkEqual ((int) staff.getMelody().getRehearsalNotes().size(), 0, "redo deletes again");

        staff.keyPressed (press ((int) 'Z', command));
        check (staff.canRedo(), "undoing again leaves something to redo");

        // Editing after an undo abandons the branch that was undone, which is
        // what every editor does and what stops redo replaying a history that
        // no longer leads anywhere.
        staff.keyPressed (press ((int) 'A', command));
        staff.keyPressed (press (juce::KeyPress::upKey));
        check (! staff.canRedo(), "a fresh edit after undoing clears the redo branch");
    }

    // -- undo covers a transpose too, and stacks --------------------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        staff.keyPressed (press ((int) 'A', command));
        staff.keyPressed (press (juce::KeyPress::upKey));
        staff.keyPressed (press (juce::KeyPress::upKey));
        staff.keyPressed (press (juce::KeyPress::upKey));

        checkEqual (staff.getMelody().getRehearsalNotes()[0], 63, "three semitones up");

        staff.keyPressed (press ((int) 'Z', command));
        staff.keyPressed (press ((int) 'Z', command));

        checkEqual (staff.getMelody().getRehearsalNotes()[0], 61,
                    "each transpose is its own step back");
    }

    // -- a refused transpose is not an undo step --------------------------------
    // Pressing up against the ceiling should do nothing at all, rather than
    // quietly filling the undo stack with edits that changed nothing.
    {
        ui::MelodyStaffComponent staff;
        staff.setSize (900, 260);
        staff.setEditEnabled (true);

        model::Melody high;
        high.setBarCount (1);
        high.placeEvent (0, model::ticksPerQuarter, 127, false);
        staff.setMelody (high);

        staff.keyPressed (press ((int) 'A', command));
        staff.keyPressed (press (juce::KeyPress::upKey));

        check (! staff.canUndo(), "a transpose that could not happen is not undoable");
        checkEqual (staff.getMelody().getRehearsalNotes()[0], 127, "and nothing moved");
    }

    // -- loading a tune is not an edit ------------------------------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        staff.keyPressed (press ((int) 'A', command));
        staff.keyPressed (press (juce::KeyPress::upKey));
        check (staff.canUndo(), "there is an edit to undo");

        staff.setMelody (makeTune());
        check (! staff.canUndo(), "loading a different tune clears the history");
        check (! staff.hasSelection(), "and the selection with it");
    }

    // -- the pen still works in either tool -------------------------------------
    {
        ui::MelodyStaffComponent staff;
        makeStaff (staff);

        staff.keyPressed (press ('4'));
        checkEqual (staff.getNoteValueTicks(), model::ticksPerQuarter / 2,
                    "4 picks an eighth while selecting");

        staff.keyPressed (press ('.'));
        check (staff.isDotted(), "and the dot applies");

        checkEqual (staff.getNoteValueTicks(), (model::ticksPerQuarter / 2) * 3 / 2,
                    "which lengthens the value by half");

        staff.keyPressed (press ('R'));
        check (staff.isRestMode(), "R switches to writing rests");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES") << ": "
              << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
