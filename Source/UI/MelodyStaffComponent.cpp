#include "MelodyStaffComponent.h"
#include "Beaming.h"
#include "MusicFont.h"
#include "Palette.h"
#include <cmath>
#include <limits>

namespace ui
{

namespace
{
    /** Vertical budget for one system, in staff spaces: the staff itself is 4,
        the rest is room for ledger lines, stems and flags. */
    constexpr float systemHeightSpaces = 12.0f;

    /** How big one staff space is on screen. Fixed rather than fitted, because
        a readable staff matters more than seeing the whole tune at once - past
        a few systems, fitting means shrinking to illegibility. */
    constexpr float screenStaffSpace = 13.0f;

    /** Classic proportional spacing: width grows with duration, but far more
        slowly than duration does, so a whole note is wider than a quarter
        without being sixteen times wider. */
    float widthForTicks (int ticks, float staffSpace)
    {
        const auto quarters = (double) ticks / (double) model::ticksPerQuarter;

        return staffSpace * 3.2f * (float) std::pow (juce::jmax (0.05, quarters), 0.6);
    }

    juce_wchar noteheadGlyphForBaseTicks (int baseTicks)
    {
        if (baseTicks >= 3840) return musicFont::noteheadWhole;
        if (baseTicks >= 1920) return musicFont::noteheadHalf;

        return musicFont::noteheadBlack;
    }

    juce_wchar restGlyphForBaseTicks (int baseTicks)
    {
        if (baseTicks >= 3840) return musicFont::restWhole;
        if (baseTicks >= 1920) return musicFont::restHalf;
        if (baseTicks >= 960)  return musicFont::restQuarter;
        if (baseTicks >= 480)  return musicFont::rest8th;

        return musicFont::rest16th;
    }
}

//==============================================================================
MelodyStaffComponent::MelodyStaffComponent()
{
    setOpaque (false);
    setWantsKeyboardFocus (true);
}

void MelodyStaffComponent::setMelody (const model::Melody& newMelody)
{
    // Replacing the music wholesale - loading, generating, importing - is not an
    // edit, and undoing back into a tune you have left would be surprising.
    undoStack.clear();
    redoStack.clear();
    selectionStart  = 0;
    selectionLength = 0;

    melody = newMelody;
    rebuildLayout();
    repaint();
}

void MelodyStaffComponent::setScale (const theory::Scale& newScale)
{
    scale = newScale;
    rebuildLayout();
    repaint();
}

void MelodyStaffComponent::setTargetIndex (int rehearsalIndex)
{
    if (rehearsalIndex >= 0 && rehearsalIndex != targetIndex)
        for (const auto& event : laidOut)
            if (event.colourIndex == rehearsalIndex)
            {
                keepTickVisible (event.startTick);
                break;
            }

    if (targetIndex != rehearsalIndex)
    {
        targetIndex = rehearsalIndex;
        repaint();
    }
}

void MelodyStaffComponent::setCompletedCount (int count)
{
    if (completedCount != count)
    {
        completedCount = count;
        repaint();
    }
}

void MelodyStaffComponent::setPrintMode (bool shouldPrint)
{
    printMode = shouldPrint;
    repaint();
}

juce::Colour MelodyStaffComponent::inkColour() const
{
    return printMode ? juce::Colours::black : palette::ink;
}

juce::Colour MelodyStaffComponent::lineColour() const
{
    return printMode ? juce::Colours::black : palette::staffLine;
}

void MelodyStaffComponent::setPlayheadTick (int tick)
{
    if (tick >= 0 && tick != playheadTick)
        keepTickVisible (tick);

    if (playheadTick != tick)
    {
        playheadTick = tick;
        repaint();
    }
}

/** Interpolates within the slot the tick falls in, so the playhead slides
    smoothly rather than jumping from note to note. */
bool MelodyStaffComponent::positionForTick (int tick, float& x, int& systemIndex) const
{
    for (const auto& event : laidOut)
    {
        if (tick < event.startTick || tick >= event.startTick + event.lengthTicks)
            continue;

        const auto through = event.lengthTicks > 0
            ? (float) (tick - event.startTick) / (float) event.lengthTicks
            : 0.0f;

        x = event.slotX + through * event.width;
        systemIndex = event.systemIndex;

        return true;
    }

    return false;
}

void MelodyStaffComponent::drawPlayhead (juce::Graphics& g) const
{
    float x = 0.0f;
    int systemIndex = 0;

    if (printMode || playheadTick < 0 || ! positionForTick (playheadTick, x, systemIndex))
        return;

    if (systemIndex < 0 || systemIndex >= (int) systems.size())
        return;

    const auto geometry = geometryForSystem (systems[(size_t) systemIndex]);

    g.setColour (palette::inTune.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (x, geometry.topLineY() - staffSpace,
                                        juce::jmax (2.0f, staffSpace * 0.14f),
                                        geometry.bottomLineY - geometry.topLineY()
                                          + staffSpace * 2.0f));
}

void MelodyStaffComponent::resized()
{
    rebuildLayout();
}

//==============================================================================
void MelodyStaffComponent::rebuildLayout()
{
    laidOut.clear();
    systems.clear();
    measureRight.clear();

    const auto bounds = getLocalBounds().toFloat().reduced (10.0f, 8.0f);

    if (bounds.getWidth() < 60.0f || bounds.getHeight() < 40.0f)
        return;

    const auto& events    = melody.getEvents();
    const auto  barTicks  = melody.getTimeSignature().barTicks();
    const auto  measures  = melody.getBarCount();
    const auto  treble    = melody.isTrebleClef();

    if (events.empty() || measures <= 0)
        return;

    // --- resolve every event to a glyph and a staff position ------------------
    {
        // The numbering comes from the model rather than being worked out again
        // here, so the colours on the staff and the engine's idea of "note 3"
        // cannot drift apart.
        const auto rehearsalIndices = melody.getRehearsalIndexPerEvent();
        size_t eventIndex = 0;

        for (const auto& event : events)
        {
            LaidOutEvent laid;
            laid.startTick     = event.startTick;
            laid.lengthTicks   = event.lengthTicks;
            laid.isRest        = event.isRest;
            laid.tiedToNext    = event.tiedToNext;
            laid.midiNote      = event.midiNote;
            laid.measureIndex  = event.startTick / barTicks;
            laid.isFullBarRest = event.isRest && event.lengthTicks == barTicks;

            if (const auto* value = model::findNoteValue (event.lengthTicks))
            {
                laid.baseTicks = value->baseTicks;
                laid.dots      = value->dots;
            }
            else
            {
                laid.baseTicks = event.lengthTicks;
            }

            laid.flags = event.isRest ? 0 : model::flagCountForBaseTicks (laid.baseTicks);

            if (! event.isRest)
                laid.spelled = scale.spell (event.midiNote);

            if (eventIndex < rehearsalIndices.size())
                laid.rehearsalIndex = rehearsalIndices[eventIndex];

            ++eventIndex;
            laidOut.push_back (laid);
        }

        // The tail of a tie is part of the same note, so it should be coloured
        // as that note rather than left looking untouched.
        int owning = -1;

        for (auto& laid : laidOut)
        {
            if (laid.isRest)
            {
                owning = -1;
                laid.colourIndex = -1;
                continue;
            }

            if (laid.rehearsalIndex >= 0)
                owning = laid.rehearsalIndex;

            laid.colourIndex = owning;
        }
    }

    // --- choose a staff size, then pack measures into systems -----------------
    // The two depend on each other: the size decides how much fits on a line,
    // and the number of lines decides how tall the size may be. A couple of
    // passes settles it.
    // On screen the staff is capped so a short tune doesn't look absurd in a
    // tall panel. On paper the opposite is wanted: fill the sheet.
    // On paper the staff fills the sheet: it is sized from the page height and
    // shrunk until every system fits. On screen it must not be, because a tune
    // long enough to need twenty systems would end up unreadable - so the size
    // is fixed at something comfortable and the component grows instead, and
    // whatever holds it scrolls.
    staffSpace = printMode ? juce::jlimit (5.0f, 48.0f,
                                           bounds.getHeight() / (2.0f * systemHeightSpaces))
                           : screenStaffSpace;

    std::vector<float> measureWidths;

    for (int pass = 0; pass < 4; ++pass)
    {
        const auto font = musicFont::fontForStaffSpace (staffSpace);
        const auto accidentalWidth = musicFont::getGlyphWidth (font, musicFont::accidentalSharp) * 1.2f;

        measureWidths.assign ((size_t) measures, staffSpace * 0.9f);

        for (auto& laid : laidOut)
        {
            // An accidental hangs to the left of its notehead, so the room for
            // it has to come before the note rather than after it - otherwise
            // it reaches back into whatever was written previously.
            laid.accidentalAdvance = (! laid.isRest && laid.spelled.needsAccidental)
                                       ? accidentalWidth : 0.0f;

            laid.width = juce::jmax (staffSpace * 1.9f, widthForTicks (laid.lengthTicks, staffSpace))
                           + laid.accidentalAdvance;

            if (laid.measureIndex < measures)
                measureWidths[(size_t) laid.measureIndex] += laid.width;
        }

        const auto furnitureWidth = staffLayout::getClefWidth (font, treble) * 1.3f
                                      + staffLayout::getKeySignatureWidth (font, scale.getKeySignature())
                                      + staffSpace * 1.2f;

        const auto timeSignatureWidth =
            staffLayout::getTimeSignatureWidth (font, melody.getTimeSignature().numerator,
                                                melody.getTimeSignature().denominator) + staffSpace * 0.8f;

        // Pack greedily, always taking at least one measure so a very wide bar
        // cannot stall the loop.
        systems.clear();
        SystemInfo current;
        current.firstMeasure = 0;
        current.lastMeasure  = -1;

        auto available = bounds.getWidth() - furnitureWidth - timeSignatureWidth;
        auto used = 0.0f;

        for (int measure = 0; measure < measures; ++measure)
        {
            const auto width = measureWidths[(size_t) measure];

            if (current.lastMeasure >= current.firstMeasure && used + width > available)
            {
                systems.push_back (current);
                current.firstMeasure = measure;
                current.lastMeasure  = measure;
                used = width;
                available = bounds.getWidth() - furnitureWidth;   // no time signature after line 1
            }
            else
            {
                current.lastMeasure = measure;
                used += width;
            }
        }

        systems.push_back (current);

        // Only a page has to be fitted into. On screen the height follows the
        // music rather than the music being squeezed into the height.
        if (! printMode)
            break;

        const auto requiredHeight = (float) systems.size() * systemHeightSpaces * staffSpace;

        if (requiredHeight <= bounds.getHeight() || staffSpace <= 5.0f)
            break;

        staffSpace = juce::jmax (5.0f, bounds.getHeight()
                                         / ((float) systems.size() * systemHeightSpaces));
    }

    // --- place everything horizontally ---------------------------------------
    const auto font = musicFont::fontForStaffSpace (staffSpace);
    const auto systemHeight = systemHeightSpaces * staffSpace;

    measureRight.assign ((size_t) measures, 0.0f);

    for (size_t systemIndex = 0; systemIndex < systems.size(); ++systemIndex)
    {
        auto& system = systems[systemIndex];

        // Sit the staff a little below the middle of its slice, leaving more
        // room above for ledger lines and stems than below.
        system.bottomLineY = bounds.getY() + (float) systemIndex * systemHeight + systemHeight * 0.62f;

        auto x = bounds.getX() + staffSpace * 0.4f
              + staffLayout::getClefWidth (font, treble) * 1.3f
              + staffLayout::getKeySignatureWidth (font, scale.getKeySignature())
              + staffSpace * 0.6f;

        if (systemIndex == 0)
            x += staffLayout::getTimeSignatureWidth (font, melody.getTimeSignature().numerator,
                                                     melody.getTimeSignature().denominator)
                   + staffSpace * 0.8f;

        system.contentLeft  = x;
        system.contentRight = bounds.getRight();

        // Natural width of this system's music, then stretch it to fill the
        // line - except on the last system, where stretching sparse music
        // looks wrong.
        auto natural = 0.0f;

        for (int measure = system.firstMeasure; measure <= system.lastMeasure; ++measure)
            natural += measureWidths[(size_t) measure];

        const auto span = system.contentRight - system.contentLeft;
        auto stretch = 1.0f;

        // The last system is normally left unstretched, because spreading a
        // couple of stray bars across a full line looks wrong on screen. A
        // printed page is justified throughout, the way engraved music is.
        if (natural > 1.0f && (printMode || systemIndex + 1 < systems.size()))
            stretch = juce::jlimit (1.0f, 2.5f, span / natural);
        else if (natural > span && natural > 1.0f)
            stretch = span / natural;

        auto penX = system.contentLeft;

        for (int measure = system.firstMeasure; measure <= system.lastMeasure; ++measure)
        {
            penX += staffSpace * 0.9f * stretch;

            for (auto& laid : laidOut)
            {
                if (laid.measureIndex != measure)
                    continue;

                laid.systemIndex = (int) systemIndex;
                laid.slotX = penX;
                laid.x = penX + laid.accidentalAdvance * stretch;
                penX += laid.width * stretch;
            }

            measureRight[(size_t) measure] = penX;
        }
    }

    buildBeams();

    contentHeight = (int) std::ceil ((float) systems.size() * systemHeight
                                       + (bounds.getY() - getLocalBounds().toFloat().getY()) * 2.0f);

    // Grow to whatever the music needs. The guard matters: setSize re-enters
    // through resized(), and without it a tune whose height keeps changing
    // would recurse rather than settle.
    if (! printMode && ! resizingToContent && contentHeight > 0 && contentHeight != getHeight())
    {
        const juce::ScopedValueSetter<bool> guard (resizingToContent, true);
        setSize (getWidth(), contentHeight);
    }
}

/** Asks whoever owns the scrolling to bring the system holding this tick into
    view. Silent when the tick is not on any system, which is the normal case
    while a tune is being rebuilt. */
void MelodyStaffComponent::keepTickVisible (int tick)
{
    if (onKeepVisible == nullptr || printMode || systems.empty())
        return;

    float x = 0.0f;
    int systemIndex = 0;

    if (! positionForTick (tick, x, systemIndex))
        return;

    if (systemIndex < 0 || systemIndex >= (int) systems.size())
        return;

    const auto systemHeight = systemHeightSpaces * staffSpace;
    const auto top = systems[(size_t) systemIndex].bottomLineY - systemHeight * 0.62f;

    onKeepVisible (juce::Rectangle<int> (0, (int) top, getWidth(), (int) std::ceil (systemHeight)));
}

/** Works out which notes share a beam, and where that beam sits.

    Eighths and shorter are beamed together rather than flagged individually -
    that is simply how music is written, and a page of separate flags is harder
    to read because the beam is what shows the beat. The grouping rule is the
    one the model already uses to split notes: beam within a beat, which is a
    quarter in the simple metres and a dotted quarter in the compound ones. So
    6/8 gets its three eighths under one beam and 4/4 gets two, which is what
    each metre is supposed to look like.

    Rests break a beam, as does a barline, a system break, and a note that runs
    past the end of its beat.
*/
void MelodyStaffComponent::buildBeams()
{
    beams.clear();

    for (auto& event : laidOut)
    {
        event.beamIndex = -1;
        event.stemUp    = isStemUp (event);
    }

    if (systems.empty() || laidOut.empty())
        return;

    const auto font          = musicFont::fontForStaffSpace (staffSpace);
    const auto noteheadWidth = musicFont::getGlyphWidth (font, musicFont::noteheadBlack);
    const auto thickness     = staffSpace * staffLayout::stemThickness;

    const auto middleStep = (melody.isTrebleClef() ? staffLayout::trebleBottomStep
                                                   : staffLayout::bassBottomStep) + 4;

    std::vector<beaming::Candidate> candidates;
    candidates.reserve (laidOut.size());

    for (const auto& event : laidOut)
        candidates.push_back ({ event.startTick, event.lengthTicks, event.flags,
                                event.isRest, event.systemIndex });

    for (const auto& grouped : beaming::group (candidates, melody.getTimeSignature()))
    {
        std::vector<size_t> run;

        for (auto index : grouped)
            run.push_back ((size_t) index);

        const auto systemIndex = laidOut[run.front()].systemIndex;

        if (systemIndex < 0 || systemIndex >= (int) systems.size())
            continue;

        const auto geometry = geometryForSystem (systems[(size_t) systemIndex]);

        // The whole group points the same way, decided by whichever note is
        // furthest from the middle line - the note that would look worst with
        // its stem on the wrong side.
        auto furthest = 0;

        for (auto index : run)
        {
            const auto distance = laidOut[index].spelled.diatonicStep() - middleStep;

            if (std::abs (distance) > std::abs (furthest))
                furthest = distance;
        }

        BeamGroup beam;
        beam.events = run;
        beam.stemUp = furthest < 0;

        const auto away = beam.stemUp ? -1.0f : 1.0f;

        auto stemX = [this, &beam, noteheadWidth, thickness] (size_t index)
        {
            return beam.stemUp ? laidOut[index].x + noteheadWidth - thickness
                               : laidOut[index].x;
        };

        auto unbeamedEnd = [this, &geometry, away] (size_t index)
        {
            return geometry.yForStep (laidOut[index].spelled.diatonicStep())
                     + away * staffSpace * staffLayout::stemLength;
        };

        beam.startX = stemX (run.front());
        beam.endX   = stemX (run.back());
        beam.startY = unbeamedEnd (run.front());
        beam.endY   = unbeamedEnd (run.back());

        // Engraved beams tilt, but gently: following the notes exactly would
        // give a near-vertical beam whenever the line leaps.
        const auto tilt = juce::jmin (staffSpace * 1.5f, (beam.endX - beam.startX) * 0.25f);
        auto rise = juce::jlimit (-tilt, tilt, beam.endY - beam.startY);
        beam.endY = beam.startY + rise;

        // Then lift the whole beam until no stem under it is stubby.
        const auto shortestStem = staffSpace * 2.5f;
        const auto span = beam.endX - beam.startX;
        auto lift = 0.0f;

        for (auto index : run)
        {
            const auto along = span > 0.01f ? (stemX (index) - beam.startX) / span : 0.0f;
            const auto beamY = beam.startY + rise * along;
            const auto noteY = geometry.yForStep (laidOut[index].spelled.diatonicStep());
            const auto stem  = beam.stemUp ? noteY - beamY : beamY - noteY;

            if (stem < shortestStem)
                lift = juce::jmax (lift, shortestStem - stem);
        }

        beam.startY += away * lift;
        beam.endY   += away * lift;

        for (auto index : run)
        {
            laidOut[index].beamIndex = (int) beams.size();
            laidOut[index].stemUp    = beam.stemUp;
        }

        beams.push_back (std::move (beam));
    }
}

/** Draws the stems up to each beam, then the beams themselves.

    Secondary beams only span the notes that actually need them, and a lone
    sixteenth inside a beamed group gets a stub pointing back towards the beat,
    which is the convention.
*/
void MelodyStaffComponent::drawBeams (juce::Graphics& g, const StaffGeometry& geometry,
                                      int systemIndex) const
{
    const auto font          = musicFont::fontForStaffSpace (staffSpace);
    const auto noteheadWidth = musicFont::getGlyphWidth (font, musicFont::noteheadBlack);
    const auto thickness     = staffSpace * staffLayout::stemThickness;
    const auto beamDepth     = staffSpace * staffLayout::beamThickness;
    const auto beamGap       = beamDepth + staffSpace * 0.25f;

    for (const auto& beam : beams)
    {
        if (beam.events.empty() || laidOut[beam.events.front()].systemIndex != systemIndex)
            continue;

        const auto span = beam.endX - beam.startX;
        const auto rise = beam.endY - beam.startY;

        auto stemX = [this, &beam, noteheadWidth, thickness] (size_t index)
        {
            return beam.stemUp ? laidOut[index].x + noteheadWidth - thickness
                               : laidOut[index].x;
        };

        auto beamYAt = [&beam, span, rise] (float x)
        {
            return span > 0.01f ? beam.startY + rise * (x - beam.startX) / span
                                : beam.startY;
        };

        // --- stems ------------------------------------------------------------
        for (auto index : beam.events)
        {
            const auto& event = laidOut[index];
            const auto  x     = stemX (index);
            const auto  noteY = geometry.yForStep (event.spelled.diatonicStep());
            const auto  beamY = beamYAt (x);

            g.setColour (colourForEvent (event));
            g.fillRect (juce::Rectangle<float> (x, juce::jmin (noteY, beamY),
                                                thickness, std::abs (beamY - noteY)));
        }

        // --- the beams themselves ---------------------------------------------
        const auto inward = beam.stemUp ? 1.0f : -1.0f;

        auto drawBeamSegment = [&] (float fromX, float toX, int level)
        {
            const auto offset = inward * (float) level * beamGap;

            const auto edge = [&] (float x) { return beamYAt (x) + offset; };
            const auto far  = [&] (float x) { return edge (x) + inward * beamDepth; };

            juce::Path quad;
            quad.startNewSubPath (fromX, edge (fromX));
            quad.lineTo (toX, edge (toX));
            quad.lineTo (toX, far (toX));
            quad.lineTo (fromX, far (fromX));
            quad.closeSubPath();

            g.fillPath (quad);
        };

        g.setColour (colourForEvent (laidOut[beam.events.front()]));
        drawBeamSegment (beam.startX, beam.endX + thickness, 0);

        // Secondary beams: one for each extra flag, spanning only the runs of
        // notes short enough to have it.
        auto deepest = 1;

        for (auto index : beam.events)
            deepest = juce::jmax (deepest, laidOut[index].flags);

        for (int level = 1; level < deepest; ++level)
        {
            size_t at = 0;

            while (at < beam.events.size())
            {
                if (laidOut[beam.events[at]].flags <= level)
                {
                    ++at;
                    continue;
                }

                auto last = at;

                while (last + 1 < beam.events.size()
                        && laidOut[beam.events[last + 1]].flags > level)
                    ++last;

                const auto first = stemX (beam.events[at]);

                if (last > at)
                {
                    drawBeamSegment (first, stemX (beam.events[last]) + thickness, level);
                }
                else
                {
                    // A stub, pointing back towards the beat unless this is the
                    // first note under the beam, where there is nothing behind
                    // it to point at.
                    const auto stub = staffSpace * 0.9f;

                    if (at > 0)
                        drawBeamSegment (first - stub, first + thickness, level);
                    else
                        drawBeamSegment (first, first + stub, level);
                }

                at = last + 1;
            }
        }
    }
}

StaffGeometry MelodyStaffComponent::geometryForSystem (const SystemInfo& system) const
{
    StaffGeometry geometry;
    geometry.staffSpace     = staffSpace;
    geometry.bottomLineY    = system.bottomLineY;
    geometry.bottomLineStep = melody.isTrebleClef() ? staffLayout::trebleBottomStep
                                                    : staffLayout::bassBottomStep;
    geometry.left  = getLocalBounds().toFloat().reduced (10.0f, 8.0f).getX();
    geometry.right = system.contentRight;

    return geometry;
}

//==============================================================================
bool MelodyStaffComponent::isStemUp (const LaidOutEvent& event) const
{
    const auto bottomStep = melody.isTrebleClef() ? staffLayout::trebleBottomStep
                                                  : staffLayout::bassBottomStep;

    // Notes below the middle line point up, notes on or above it point down.
    return event.spelled.diatonicStep() < bottomStep + 4;
}

juce::Colour MelodyStaffComponent::colourForEvent (const LaidOutEvent& event) const
{
    // On a printed page everything is simply black; the target and completed
    // colours only mean something on screen during rehearsal.
    if (printMode)
        return juce::Colours::black;

    if (event.isRest)
        return palette::inkDim;

    if (event.colourIndex >= 0 && event.colourIndex == targetIndex)
        return palette::detectedNote;

    if (event.colourIndex >= 0 && event.colourIndex < completedCount)
        return palette::midiNote;

    return palette::ink;
}

//==============================================================================
void MelodyStaffComponent::paint (juce::Graphics& g)
{
    if (printMode)
        g.fillAll (juce::Colours::white);
    else
        g.setColour (palette::panel),
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    if (systems.empty() || laidOut.empty())
    {
        g.setColour (palette::inkDim);
        g.setFont (juce::Font (14.0f));
        g.drawText ("no tune", getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    const auto font = musicFont::fontForStaffSpace (staffSpace);

    for (size_t systemIndex = 0; systemIndex < systems.size(); ++systemIndex)
    {
        const auto& system   = systems[systemIndex];
        const auto  geometry = geometryForSystem (system);

        drawSelection (g, geometry, (int) systemIndex);
        drawSystemFurniture (g, system, font, systemIndex == 0);
        drawBarLines (g, geometry, system);
        drawBeams (g, geometry, (int) systemIndex);

        for (size_t i = 0; i < laidOut.size(); ++i)
        {
            const auto& event = laidOut[i];

            if (event.systemIndex != (int) systemIndex)
                continue;

            drawEvent (g, geometry, font, event);

            if (event.tiedToNext && i + 1 < laidOut.size())
            {
                const auto& next = laidOut[i + 1];

                if (next.systemIndex == event.systemIndex)
                    drawTie (g, geometry, event, next, event.stemUp);
            }
        }
    }

    drawPlayhead (g);

    // Only while writing: a caret means "the next thing you type lands here",
    // which is not true when the click selects instead.
    if (editEnabled && ! printMode && tool == StaffTool::write)
    {
        drawCaret (g);
        drawShadowNote (g, font);
    }
}

//==============================================================================
void MelodyStaffComponent::setEditEnabled (bool shouldBeEnabled)
{
    if (editEnabled == shouldBeEnabled)
        return;

    editEnabled = shouldBeEnabled;
    hover = {};

    if (editEnabled)
        grabKeyboardFocus();

    repaint();
}

void MelodyStaffComponent::setNoteValueTicks (int ticks)
{
    noteValueTicks = juce::jmax (model::ticksPerQuarter / 4, ticks);
    repaint();
}

void MelodyStaffComponent::setDotted (bool shouldBeDotted)
{
    if (dotted == shouldBeDotted)
        return;

    // A dot adds half as much again, so toggling it rescales the written length
    // rather than picking a different value from the palette.
    const auto undotted = dotted ? (noteValueTicks * 2) / 3 : noteValueTicks;

    dotted = shouldBeDotted;
    noteValueTicks = dotted ? (undotted * 3) / 2 : undotted;

    repaint();
}

void MelodyStaffComponent::setRestMode (bool shouldWriteRests)
{
    restMode = shouldWriteRests;
    repaint();
}

//==============================================================================
const MelodyStaffComponent::LaidOutEvent* MelodyStaffComponent::eventForTick (int tick) const
{
    for (const auto& event : laidOut)
        if (tick >= event.startTick && tick < event.startTick + event.lengthTicks)
            return &event;

    return laidOut.empty() ? nullptr : &laidOut.back();
}

MelodyStaffComponent::HitPosition MelodyStaffComponent::hitTest (juce::Point<float> position) const
{
    HitPosition result;

    if (systems.empty() || laidOut.empty())
        return result;

    // Nearest system vertically. Staves sit far enough apart that "nearest" and
    // "inside" amount to the same thing.
    size_t bestSystem = 0;
    auto bestDistance = std::numeric_limits<float>::max();

    for (size_t i = 0; i < systems.size(); ++i)
    {
        const auto centre = systems[i].bottomLineY - 2.0f * staffSpace;
        const auto distance = std::abs (position.y - centre);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestSystem = i;
        }
    }

    const auto geometry = geometryForSystem (systems[bestSystem]);

    // The last slot starting at or before the pointer. Because bars are always
    // full, every x inside a system belongs to some slot, so a click always
    // lands somewhere sensible.
    const LaidOutEvent* best = nullptr;

    for (const auto& event : laidOut)
    {
        if (event.systemIndex != (int) bestSystem)
            continue;

        if (best == nullptr || position.x >= event.slotX)
            best = &event;
    }

    if (best == nullptr)
        return result;

    result.valid       = true;
    result.tick        = best->startTick;
    result.slotX       = best->slotX;
    result.systemIndex = (int) bestSystem;
    result.step        = geometry.stepForY (position.y);

    return result;
}

//==============================================================================
void MelodyStaffComponent::mouseMove (const juce::MouseEvent& event)
{
    if (tool == StaffTool::select)
    {
        if (hover.valid)
        {
            hover = {};
            repaint();
        }

        return;
    }

    if (! editEnabled)
        return;

    const auto previous = hover;
    hover = hitTest (event.position.toFloat());

    if (previous.valid != hover.valid || previous.tick != hover.tick
         || previous.step != hover.step)
        repaint();
}

void MelodyStaffComponent::mouseExit (const juce::MouseEvent&)
{
    if (hover.valid)
    {
        hover = {};
        repaint();
    }
}

void MelodyStaffComponent::mouseDown (const juce::MouseEvent& event)
{
    if (editEnabled && tool == StaffTool::select)
    {
        const auto hit = hitTest (event.position.toFloat());

        if (! hit.valid)
            return;

        const auto* target = eventForTick (hit.tick);
        const auto length = target != nullptr ? target->lengthTicks : 0;

        // Shift stretches the selection from where it started, the way a shifted
        // click extends a range in a list.
        if (event.mods.isShiftDown() && hasSelection())
        {
            const auto from = juce::jmin (selectionStart, hit.tick);
            const auto to   = juce::jmax (selectionStart + selectionLength, hit.tick + length);
            setSelection (from, to - from);
        }
        else
        {
            setSelection (hit.tick, length);
        }

        grabKeyboardFocus();
        return;
    }

    if (! editEnabled)
        return;

    grabKeyboardFocus();

    const auto hit = hitTest (event.position.toFloat());

    if (hit.valid)
        placeAt (hit.tick, scale.noteForDiatonicStep (hit.step), restMode);
}

void MelodyStaffComponent::pushUndo()
{
    undoStack.push_back (melody);

    if (undoStack.size() > maxUndoDepth)
        undoStack.erase (undoStack.begin());

    // A fresh edit is a new branch: whatever was undone is no longer reachable.
    redoStack.clear();
}

void MelodyStaffComponent::notifyEditState()
{
    if (onEditStateChanged != nullptr)
        onEditStateChanged();
}

void MelodyStaffComponent::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (melody);
    melody = undoStack.back();
    undoStack.pop_back();

    clearSelection();
    rebuildLayout();
    repaint();
    notifyEditState();

    if (onMelodyChanged != nullptr)
        onMelodyChanged();
}

void MelodyStaffComponent::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (melody);
    melody = redoStack.back();
    redoStack.pop_back();

    clearSelection();
    rebuildLayout();
    repaint();
    notifyEditState();

    if (onMelodyChanged != nullptr)
        onMelodyChanged();
}

//==============================================================================
void MelodyStaffComponent::setTool (StaffTool newTool)
{
    if (tool == newTool)
        return;

    tool = newTool;

    // The two tools do not share a cursor: a caret means "the next thing you
    // type lands here", which is only true while writing.
    if (tool == StaffTool::select)
        hover = {};
    else
        clearSelection();

    repaint();
    notifyEditState();
}

void MelodyStaffComponent::setSelection (int startTick, int lengthTicks)
{
    selectionStart  = juce::jlimit (0, melody.getTotalTicks(), startTick);
    selectionLength = juce::jlimit (0, melody.getTotalTicks() - selectionStart, lengthTicks);

    if (selectionLength > 0)
        keepTickVisible (selectionStart);

    repaint();
    notifyEditState();
}

void MelodyStaffComponent::selectAll()
{
    setTool (StaffTool::select);
    setSelection (0, melody.getTotalTicks());
}

void MelodyStaffComponent::clearSelection()
{
    selectionStart  = 0;
    selectionLength = 0;
    repaint();
    notifyEditState();
}

/** Moves the selection one written event at a time, or stretches it. */
void MelodyStaffComponent::moveSelection (int direction, bool extend)
{
    if (laidOut.empty())
        return;

    if (! hasSelection())
    {
        setSelection (laidOut.front().startTick, laidOut.front().lengthTicks);
        return;
    }

    const auto end = selectionStart + selectionLength;

    if (extend)
    {
        // Growing and shrinking both act on the far end, which is what a shifted
        // arrow does everywhere else.
        if (direction > 0)
        {
            for (const auto& event : laidOut)
                if (event.startTick >= end)
                {
                    setSelection (selectionStart, event.endTick() - selectionStart);
                    return;
                }
        }
        else
        {
            for (auto it = laidOut.rbegin(); it != laidOut.rend(); ++it)
                if (it->endTick() < end && it->endTick() > selectionStart)
                {
                    setSelection (selectionStart, it->endTick() - selectionStart);
                    return;
                }
        }

        return;
    }

    if (direction > 0)
    {
        for (const auto& event : laidOut)
            if (event.startTick >= end)
            {
                setSelection (event.startTick, event.lengthTicks);
                return;
            }
    }
    else
    {
        for (auto it = laidOut.rbegin(); it != laidOut.rend(); ++it)
            if (it->startTick < selectionStart)
            {
                setSelection (it->startTick, it->lengthTicks);
                return;
            }
    }
}

void MelodyStaffComponent::transposeSelection (int semitones)
{
    if (! hasSelection())
        return;

    pushUndo();

    if (! melody.transposeRange (selectionStart, selectionLength, semitones))
    {
        undoStack.pop_back();     // nothing moved, so there is nothing to undo
        return;
    }

    rebuildLayout();
    repaint();
    notifyEditState();

    if (onMelodyChanged != nullptr)
        onMelodyChanged();
}

void MelodyStaffComponent::deleteSelection()
{
    if (! hasSelection())
        return;

    pushUndo();
    melody.eraseRange (selectionStart, selectionLength);

    rebuildLayout();
    repaint();
    notifyEditState();

    if (onMelodyChanged != nullptr)
        onMelodyChanged();
}

//==============================================================================
void MelodyStaffComponent::placeAt (int tick, int midiNote, bool rest)
{
    pushUndo();
    melody.placeEvent (tick, noteValueTicks, midiNote, rest);

    lastPlacedTick = tick;
    setCaretTick (tick + noteValueTicks);

    rebuildLayout();
    repaint();

    if (! rest && onNotePreview != nullptr)
        onNotePreview (midiNote);

    if (onMelodyChanged != nullptr)
        onMelodyChanged();
}

void MelodyStaffComponent::setCaretTick (int tick)
{
    // The caret may sit one slot past the end; writing there is what grows the
    // tune by a bar.
    caretTick = juce::jlimit (0, melody.getTotalTicks(), tick);
    keepTickVisible (caretTick);
}

void MelodyStaffComponent::moveCaret (int direction)
{
    if (direction < 0)
    {
        for (auto it = laidOut.rbegin(); it != laidOut.rend(); ++it)
        {
            if (it->startTick < caretTick)
            {
                setCaretTick (it->startTick);
                break;
            }
        }
    }
    else
    {
        for (const auto& event : laidOut)
        {
            if (event.startTick > caretTick)
            {
                setCaretTick (event.startTick);
                break;
            }
        }
    }

    repaint();
}

void MelodyStaffComponent::nudgeLastNote (int semitones)
{
    pushUndo();

    if (lastPlacedTick < 0)
        return;

    const auto* event = eventForTick (lastPlacedTick);

    if (event == nullptr || event->isRest)
        return;

    const auto start  = event->startTick;
    const auto length = event->lengthTicks;
    const auto note   = juce::jlimit (12, 120, event->midiNote + semitones);

    melody.placeEvent (start, length, note, false);

    rebuildLayout();
    repaint();

    if (onNotePreview != nullptr)
        onNotePreview (note);

    if (onMelodyChanged != nullptr)
        onMelodyChanged();
}

void MelodyStaffComponent::deleteBeforeCaret()
{
    pushUndo();

    const LaidOutEvent* target = nullptr;

    for (const auto& event : laidOut)
        if (event.startTick < caretTick)
            target = &event;

    if (target == nullptr)
        return;

    const auto tick   = target->startTick;
    const auto length = target->lengthTicks;

    melody.placeEvent (tick, length, 0, true);
    setCaretTick (tick);
    lastPlacedTick = -1;

    rebuildLayout();
    repaint();

    if (onMelodyChanged != nullptr)
        onMelodyChanged();
}

bool MelodyStaffComponent::keyPressed (const juce::KeyPress& key)
{
    if (! editEnabled)
        return false;

    const auto code        = key.getKeyCode();
    const auto mods        = key.getModifiers();
    const auto withCommand = mods.isCommandDown();
    const auto erase       = key.isKeyCode (juce::KeyPress::backspaceKey)
                               || key.isKeyCode (juce::KeyPress::deleteKey);

    // --- whatever the tool ---------------------------------------------------
    if (withCommand && code == 'Z')
    {
        mods.isShiftDown() ? redo() : undo();
        return true;
    }

    if (withCommand && code == 'Y') { redo(); return true; }
    if (withCommand && code == 'A') { selectAll(); return true; }

    // N for note input is MuseScore's binding, and Sibelius and Dorico both put
    // escape on the way back out. Following them costs nothing and means anyone
    // who has used notation software already knows this.
    if (code == 'N')
    {
        if (tool == StaffTool::write)
        {
            setTool (StaffTool::select);
        }
        else
        {
            // Carry on from whatever was selected rather than from wherever the
            // caret was left the last time.
            if (hasSelection())
                setCaretTick (selectionStart);

            setTool (StaffTool::write);
        }

        if (onMelodyChanged != nullptr)
            onMelodyChanged();

        return true;
    }

    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (tool == StaffTool::write)
        {
            setTool (StaffTool::select);

            if (onMelodyChanged != nullptr)
                onMelodyChanged();
        }
        else
        {
            clearSelection();
        }

        return true;
    }

    // The pen: which value the next note will be written as. Worth having in
    // either tool, so the duration can be lined up before entering input.
    static const int valueTicks[5] = { 3840, 1920, 960, 480, 240 };

    for (int i = 0; i < 5; ++i)
    {
        if (code == ('1' + i))
        {
            dotted = false;
            setNoteValueTicks (valueTicks[i]);

            if (onMelodyChanged != nullptr)
                onMelodyChanged();

            return true;
        }
    }

    if (code == '.')
    {
        setDotted (! dotted);

        if (onMelodyChanged != nullptr)
            onMelodyChanged();

        return true;
    }

    if (code == 'R')
    {
        setRestMode (! restMode);

        if (onMelodyChanged != nullptr)
            onMelodyChanged();

        return true;
    }

    // --- selecting -----------------------------------------------------------
    if (tool == StaffTool::select)
    {
        if (key.isKeyCode (juce::KeyPress::leftKey))
        {
            moveSelection (-1, mods.isShiftDown());
            return true;
        }

        if (key.isKeyCode (juce::KeyPress::rightKey))
        {
            moveSelection (1, mods.isShiftDown());
            return true;
        }

        if (key.isKeyCode (juce::KeyPress::upKey))
        {
            transposeSelection (withCommand ? 12 : 1);
            return true;
        }

        if (key.isKeyCode (juce::KeyPress::downKey))
        {
            transposeSelection (withCommand ? -12 : -1);
            return true;
        }

        if (erase)
        {
            deleteSelection();
            return true;
        }

        return false;
    }

    // --- writing -------------------------------------------------------------
    if (key.isKeyCode (juce::KeyPress::leftKey))  { moveCaret (-1); return true; }
    if (key.isKeyCode (juce::KeyPress::rightKey)) { moveCaret (1);  return true; }
    if (key.isKeyCode (juce::KeyPress::upKey))    { nudgeLastNote (withCommand ? 12 : 1);   return true; }
    if (key.isKeyCode (juce::KeyPress::downKey))  { nudgeLastNote (withCommand ? -12 : -1); return true; }

    if (erase)
    {
        deleteBeforeCaret();
        return true;
    }

    return false;
}

//==============================================================================
void MelodyStaffComponent::drawCaret (juce::Graphics& g) const
{
    if (systems.empty())
        return;

    const LaidOutEvent* event = nullptr;

    for (const auto& candidate : laidOut)
        if (candidate.startTick == caretTick)
            event = &candidate;

    float x = 0.0f;
    int systemIndex = 0;

    if (event != nullptr)
    {
        x = event->slotX;
        systemIndex = event->systemIndex;
    }
    else if (! measureRight.empty())
    {
        // Caret parked past the last note, ready to extend the tune.
        x = measureRight.back();
        systemIndex = (int) systems.size() - 1;
    }
    else
    {
        return;
    }

    const auto geometry = geometryForSystem (systems[(size_t) systemIndex]);

    g.setColour (palette::detectedNote.withAlpha (0.75f));
    g.fillRect (juce::Rectangle<float> (x - staffSpace * 0.28f,
                                        geometry.topLineY() - staffSpace * 0.8f,
                                        juce::jmax (2.0f, staffSpace * 0.16f),
                                        geometry.bottomLineY - geometry.topLineY()
                                          + staffSpace * 1.6f));
}

void MelodyStaffComponent::drawShadowNote (juce::Graphics& g, const juce::Font& font) const
{
    if (! hover.valid || systems.empty())
        return;

    const auto geometry = geometryForSystem (systems[(size_t) hover.systemIndex]);
    const auto* slot = eventForTick (hover.tick);
    const auto x = slot != nullptr ? slot->x : hover.slotX;

    if (restMode)
    {
        g.setColour (palette::ink.withAlpha (0.35f));
        musicFont::drawGlyph (g, font, musicFont::restQuarter, x,
                              geometry.yForStep (geometry.bottomLineStep + 4));
        return;
    }

    const auto y = geometry.yForStep (hover.step);
    const auto glyph = noteheadGlyphForBaseTicks (dotted ? (noteValueTicks * 2) / 3
                                                         : noteValueTicks);
    const auto noteheadWidth = musicFont::getGlyphWidth (font, glyph);

    g.setColour (palette::staffLine.withAlpha (0.5f));
    staffLayout::drawLedgerLines (g, geometry, hover.step, x, noteheadWidth);

    g.setColour (palette::detectedNote.withAlpha (0.45f));
    musicFont::drawGlyph (g, font, glyph, x, y);
}

void MelodyStaffComponent::drawSystemFurniture (juce::Graphics& g, const SystemInfo& system,
                                                const juce::Font& font, bool isFirstSystem) const
{
    const auto geometry = geometryForSystem (system);
    const auto treble   = melody.isTrebleClef();

    staffLayout::drawStaffLines (g, geometry, lineColour());

    auto x = geometry.left + staffSpace * 0.4f;

    g.setColour (inkColour());
    staffLayout::drawClef (g, geometry, font, x, treble);
    x += staffLayout::getClefWidth (font, treble) * 1.3f;

    x += staffLayout::drawKeySignature (g, geometry, font, x, scale.getKeySignature(),
                                        treble, inkColour());
    x += staffSpace * 0.6f;

    if (isFirstSystem)
        staffLayout::drawTimeSignature (g, geometry, font, x,
                                        melody.getTimeSignature().numerator,
                                        melody.getTimeSignature().denominator,
                                        inkColour());
}

void MelodyStaffComponent::drawBarLines (juce::Graphics& g, const StaffGeometry& geometry,
                                         const SystemInfo& system) const
{
    const auto thickness = geometry.lineThickness();
    const auto top       = geometry.topLineY();
    const auto height    = geometry.bottomLineY - top;

    g.setColour (lineColour());

    for (int measure = system.firstMeasure; measure <= system.lastMeasure; ++measure)
    {
        if (measure >= (int) measureRight.size())
            continue;

        const auto x = measureRight[(size_t) measure];
        const auto isFinal = measure == melody.getBarCount() - 1;

        if (isFinal)
        {
            // Thin then thick, the conventional end of a piece.
            g.fillRect (juce::Rectangle<float> (x - staffSpace * 0.45f, top, thickness, height));
            g.fillRect (juce::Rectangle<float> (x - staffSpace * 0.2f, top,
                                                thickness * 3.0f, height));
        }
        else
        {
            g.fillRect (juce::Rectangle<float> (x - staffSpace * 0.2f, top, thickness, height));
        }
    }
}

//==============================================================================
void MelodyStaffComponent::drawEvent (juce::Graphics& g, const StaffGeometry& geometry,
                                      const juce::Font& font, const LaidOutEvent& event) const
{
    const auto colour = colourForEvent (event);
    g.setColour (colour);

    if (event.isRest)
    {
        const auto glyph = event.isFullBarRest ? musicFont::restWhole
                                               : restGlyphForBaseTicks (event.baseTicks);

        // A whole rest hangs from the fourth line; everything else sits on the
        // middle line.
        const auto step = (glyph == musicFont::restWhole) ? geometry.bottomLineStep + 6
                                                          : geometry.bottomLineStep + 4;

        musicFont::drawGlyph (g, font, glyph, event.x, geometry.yForStep (step));
        return;
    }

    const auto step          = event.spelled.diatonicStep();
    const auto y             = geometry.yForStep (step);
    const auto noteheadGlyph = noteheadGlyphForBaseTicks (event.baseTicks);
    const auto noteheadWidth = musicFont::getGlyphWidth (font, noteheadGlyph);

    if (event.spelled.needsAccidental)
    {
        const auto glyph = staffLayout::getAccidentalGlyph (event.spelled.alter);
        const auto width = musicFont::getGlyphWidth (font, glyph);

        musicFont::drawGlyph (g, font, glyph, event.x - width * 1.15f, y);
    }

    staffLayout::drawLedgerLines (g, geometry, step, event.x, noteheadWidth, lineColour());

    g.setColour (colour);
    musicFont::drawGlyph (g, font, noteheadGlyph, event.x, y);

    // --- stem and flags -------------------------------------------------------
    // A beamed note gets its stem from the beam, which knows where it has to
    // reach; and a beam replaces the flag, which is the whole point of it.
    if (event.baseTicks < 3840 && event.beamIndex < 0)
    {
        const auto up        = event.stemUp;
        const auto thickness = staffSpace * staffLayout::stemThickness;
        const auto length    = staffSpace * staffLayout::stemLength;
        const auto stemX     = up ? event.x + noteheadWidth - thickness : event.x;
        const auto stemTopY  = up ? y - length : y;
        const auto stemEndY  = up ? y - length : y + length;

        g.fillRect (juce::Rectangle<float> (stemX, juce::jmin (stemTopY, y),
                                            thickness, length));

        // Bravura's sixteenth flag glyph already draws both flags, so exactly
        // one glyph goes on the stem however many flags the value has.
        if (event.flags > 0)
        {
            const auto glyph = event.flags >= 2
                ? (up ? musicFont::flag16thUp : musicFont::flag16thDown)
                : (up ? musicFont::flag8thUp  : musicFont::flag8thDown);

            musicFont::drawGlyph (g, font, glyph, up ? stemX + thickness : stemX, stemEndY);
        }
    }

    // --- augmentation dot -----------------------------------------------------
    if (event.dots > 0)
    {
        // A dot never sits on a line, so a note on a line pushes it up a space.
        const auto onLine  = ((step - geometry.bottomLineStep) % 2) == 0;
        const auto dotStep = onLine ? step + 1 : step;

        musicFont::drawGlyph (g, font, musicFont::augmentationDot,
                              event.x + noteheadWidth * 1.25f, geometry.yForStep (dotStep));
    }
}

/** A wash behind the selected notes, one band per system it reaches across. */
void MelodyStaffComponent::drawSelection (juce::Graphics& g, const StaffGeometry& geometry,
                                          int systemIndex) const
{
    if (! hasSelection() || printMode)
        return;

    const auto end = selectionStart + selectionLength;

    auto left = 0.0f, right = 0.0f;
    auto found = false;

    for (const auto& event : laidOut)
    {
        if (event.systemIndex != systemIndex
             || event.endTick() <= selectionStart
             || event.startTick >= end)
            continue;

        const auto from = event.slotX;
        const auto to   = event.slotX + event.width;

        left  = found ? juce::jmin (left, from)  : from;
        right = found ? juce::jmax (right, to)   : to;
        found = true;
    }

    if (! found)
        return;

    g.setColour (palette::midiNote.withAlpha (0.20f));
    g.fillRoundedRectangle (left - staffSpace * 0.2f, geometry.topLineY() - staffSpace * 1.6f,
                            right - left + staffSpace * 0.4f, staffSpace * 7.2f, staffSpace * 0.2f);
}

void MelodyStaffComponent::drawTie (juce::Graphics& g, const StaffGeometry& geometry,
                                    const LaidOutEvent& from, const LaidOutEvent& to,
                                    bool stemUp) const
{
    const auto font = musicFont::fontForStaffSpace (staffSpace);
    const auto noteheadWidth = musicFont::getGlyphWidth (font, musicFont::noteheadBlack);

    const auto startX = from.x + noteheadWidth * 0.6f;
    const auto endX   = to.x + noteheadWidth * 0.4f;

    if (endX <= startX)
        return;

    // The tie arcs away from the stem so the two never collide.
    const auto direction = stemUp ? 1.0f : -1.0f;
    const auto y = geometry.yForStep (from.spelled.diatonicStep())
                     + direction * staffSpace * 0.6f;
    const auto peak = y + direction * staffSpace * 0.7f;

    juce::Path tie;
    tie.startNewSubPath (startX, y);
    tie.quadraticTo ((startX + endX) * 0.5f, peak, endX, y);

    g.setColour (printMode ? juce::Colours::black : palette::inkDim);
    g.strokePath (tie, juce::PathStrokeType (staffSpace * 0.12f));
}

} // namespace ui
