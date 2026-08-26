# Melody mode - design and plan

Write down short tunes, then rehearse them one note at a time by singing or
playing. This document is the working plan; it exists so a later session can
resume without re-deriving the design.

## Decisions taken

| Decision | Choice |
|---|---|
| Rhythm | Full: bar lines, time signature, beams |
| Note entry | Click onto the staff, professional step-time model |
| Voice matching | User-settable: octave-agnostic toggle, cents tolerance |
| Timing grading | Not now, but the door stays open (see below) |
| Staff | Single staff per melody with a clef property, not a grand staff |
| Tuplets | Excluded from v1 - they complicate every layout stage |

A melody is monophonic, so a single staff with a selectable clef is both the
professional convention and much cheaper to lay out than braced systems. The
live snapshot view keeps its grand staff.

## Note entry follows the professional model

MuseScore, Sibelius, Dorico and Finale all converge on the same thing, so we
copy it:

- A **caret** marks the current rhythmic position
- **Duration is chosen first**, then pitch is placed
- A **shadow note** follows the pointer showing where it will land
- The caret **advances** by the chosen duration
- Measures are **always full** - empty space is rests, and placing a note
  replaces a rest
- A note overflowing a bar is **split and tied**, never left overfull

Shortcuts to mirror: `1`-`7` note values, `.` dot, `T` tie, `R` repeat,
up/down semitone, Ctrl+up/down octave, Backspace deletes back to a rest,
`Esc` exits input mode.

"Measures are always full" is the key invariant - it makes bar validity a
property of the model rather than something to check and repair.

## Keeping timing grading possible

Rehearsal is step-advance: it waits for the right pitch, however long you take.
But one detail must be recorded now or timing grading becomes impossible later.

The voice matcher only confirms a note after ~250 ms of stability. If we record
**only** the confirmation time, every note looks late by a variable amount and
no later grader can undo that. So `RehearsalEngine` records, per note:

- **first-arrival time** - when the pitch first reached the target
- **confirmation time** - when stability was satisfied
- source (MIDI or voice), attempt count, cents error at match

Tempo is stored in the model regardless, since playback needs it. Keep
"did it happen" separate from "when did it happen" so a grader can consume the
same event stream without touching the matcher.

## Components

| Path | Role |
|---|---|
| `Source/Model/Melody` | Ticks at 960 PPQ, time signature, tempo, rests, ties, transpose. JSON in `%APPDATA%\MidiCoach\`. |
| `Source/UI/StaffLayout` | Staff geometry and glyph drawing, extracted from `NotationComponent` and shared by both views. |
| `Source/UI/MelodyStaffComponent` | The engraving pipeline and the melody view. |
| `Source/Practice/RehearsalEngine` | Target state machine; MIDI predicate first, voice predicate second. |
| `Source/Audio/MelodyPlayer` | Reference playback through the existing `PianoSynth`. |

## Engraving pipeline

1. **Bar splitting** - events crossing a bar line become tied pieces.
2. **Duration decomposition** - a length becomes legal note values, tying notes
   and splitting rests. v1 rule: split at bar lines, then greedy largest-fit.
   Syncopation may render unconventionally; refine here later.
3. **Glyph selection** - notehead, stem, flags, dots, rest glyph.
4. **Horizontal spacing** - width proportional to roughly duration^0.6, plus
   fixed room for accidentals.
5. **System breaking** - wrap measures into lines, repeating clef and key
   signature at each line start.
6. **Beaming** - eighths and shorter are joined under a beam rather than
   flagged one by one, because the beam is what shows the beat: a page of
   separate flags says the same thing and is harder to read.

Engraving constants are hardcoded to convention rather than parsed from SMuFL
metadata, which we do not ship: stem length 3.5 spaces, stem thickness 0.12,
beam thickness 0.5.

All required glyphs are already in the embedded Bravura - noteheads, flags,
rests, time-signature digits, augmentation dot. Beams and bar lines are drawn
as paths. **No new assets needed.**

### How the beaming works

The grouping rule lives in [`Source/UI/Beaming.cpp`](../Source/UI/Beaming.cpp),
away from the drawing, because which notes share a beam is a fact about the
music and can then be tested without a window. Notes group by the beat the metre
is felt in - a quarter in the simple metres, a dotted quarter in the compound
ones - which is the same unit the model already uses to decide where a long note
must be split, so the two can never disagree about where a beat begins. That is
what gives 6/8 its three eighths to a beam and 4/4 its two. A beam is broken by
a rest, a barline, a line break, and by a note that outlasts its own beat; a run
of one is not a beam at all and keeps its flag.

The drawing side then has to answer a question a single note cannot: where the
beam will be by the time it gets there. So the whole group is resolved at layout
time - one stem direction for all of them, taken from whichever note sits
furthest from the middle line; a slope that follows the outer notes but is
limited, since following them exactly gives a near-vertical beam whenever the
line leaps; and then the beam is lifted until no stem under it is stubby.
Secondary beams span only the runs of notes short enough to need them, and a
lone sixteenth inside a group gets a stub pointing back towards the beat.

## The editor's two tools

Note entry was originally the only thing a click could do, which made the staff
hostile in one specific way: clicking a note you meant only to look at
overwrote it. Every notation editor separates the two, so this does too.

**Select** is the resting state and **Write** is the mode you deliberately
enter, with `N` to enter it and `Esc` to leave - MuseScore's bindings, and close
enough to Sibelius and Dorico that anyone who has used notation software already
knows them. The caret only appears while writing, because a caret means "the
next thing you type lands here", which is not true when a click selects.

A selection is a span of ticks rather than a list of notes, which is what lets
it survive the layout being rebuilt underneath it. Arrows move it a note at a
time, shifted arrows stretch it, `ctrl+A` takes the lot; up and down transpose
it by a semitone or, with ctrl, an octave; delete turns it back into rests.

**Undo came with it, and had to.** A selection that can flatten a whole phrase
in one keystroke is a liability without one. Every mutation goes through a
single `pushUndo`, which is the only thing to remember when adding another one.
Loading, generating or importing a tune clears the history instead of pushing to
it: undoing back into a piece you have left would be worse than not undoing at
all. A transpose that is refused - because it would take a note past the end of
the MIDI range - deliberately leaves no undo step, since an edit that changed
nothing should not need taking back.

The editor tests drive the real key handler with real `KeyPress` objects rather
than poking at private state. That is partly rigour and partly hard experience:
the first attempt to verify shifted arrows by automating a running window
reported that they did not work, when in fact the automation was not delivering
the modifier. The unit test found the truth in seconds.

## Seeing yourself: the live note and the run review

Two separate displays, both driven by data the app already had and was throwing
away.

**The live note** (`MelodyStaffComponent::drawLiveNote`) draws whatever is
arriving from the keyboard or the microphone beside the note being read. It is
deliberately independent of rehearsal: it works before a run starts, which is
when someone is finding their way into the tune. Rehearsal advances only on a
correct note, so a wrong note was previously invisible - the cursor simply did
not move, which looks identical whether you sang a wrong note or nothing at
all.

The note is drawn at its real pitch, on an opaque plate with an edge so it
cannot be mistaken for engraved music, and a line joins the written note to it
whenever the two differ. That line is the readout: no line means you are on the
note. A sung note is additionally nudged off the line by however many cents it
missed by, capped at a semitone, so flat-but-recognisable and flat-enough-to-be
-a-different-note look different.

The note is named on the plate - `C5`, and for the microphone the cents error
next to it. That name is why Tunes no longer reserves a readout strip above the
staff: it said the same thing, a hundred pixels further from where the eye
already was. `readoutBounds` is empty in tune mode and the staff takes the room.
Live mode keeps the full readout, tuner meter and all, because there the readout
*is* the page.

Colour follows the source, matching the live view: blue for MIDI, orange for the
microphone.

## A run that ends, and one that goes round again

`checkComplete()` used to fire `onComplete` and leave `running` true, so a
finished run went on waiting for notes that had all been answered - and the
transport, the click and the Stop button all carried on with it. It now clears
`running`. Every caller has `checkComplete()` as its last statement, so this
cannot surprise one of them half way through.

Two things had to move with it, and both were caught by tests rather than by
looking at the app:

- `getCompletedCount()` reads `targetIndex` in step mode, so clearing that as
  well silently reported nothing completed. Only `running` is cleared;
  `getTargetIndex()` already returns -1 once it is.
- `isComplete()` was guarded on `running`, which made it false the moment the
  run it describes had finished. The guard is gone - "did that run reach the
  end?" is a question asked *after* the run, and `outcomes.empty()` is the
  guard that matters.

**Loop** then restarts it. `MainComponent` holds `loopPending` and
`loopRestartAtMs`; `onComplete` arms them, `timerCallback` fires
`beginRehearsal()` when the pause is up, and pressing Stop during the pause
cancels rather than skipping it. The pause is two seconds - long enough to read
the marked-up score, short enough to still feel continuous - and the countdown
runs in the progress label.

**The run review** (`setReview`, `drawReviewMarks`) marks the score up after a
run. The joining trick is that `LaidOutEvent::colourIndex` already carries "the
note this belongs to, tails included", so a tied note is marked across both
halves without any extra work.

The rule is *only mark trouble*. Colouring every note makes the page uniform
and the eye has nowhere to land; the whole value is going straight to the two
bars that went wrong.

| State | Notehead | Mark |
|---|---|---|
| clean | ink | none |
| wrong note first | amber | `?`, or the number of tries |
| missed | red | a cross |
| early / late | ink | a triangle pointing the way it went |
| never reached | dim | none |

Marks live in a lane above the staff. The lane is real layout - `systemSliceSpaces()`
grows the system when a review is showing - rather than something squeezed into
the gap, because ledger lines and beams already live up there. Printed pages
never get the lane, so an exported PDF stays clean.

Every state is distinguished by shape as well as colour: red and green are not
a safe pair to depend on, and the palette already spends both.

The late threshold is not invented here - it comes from `TimingSettings::goodMs`,
the same window the engine scores with, so the marks and the printed summary
can never disagree.

A review is cleared by anything that makes it untrue: starting another run,
editing the notes (`pushUndo`), or loading a different tune (`setMelody`).

`ui::NoteReview` and `ui::LiveNote` are plain structs in the UI layer, so the
staff can be drawn and tested without knowing `practice::` exists - the same
separation `RehearsalEngine` keeps from the audio side. `MainComponent` is where
the two meet.

## Where the controls live

Most settings are chosen once and then left alone - which instrument, what
counts as the right note, how wide the exercise range is - and a row of them
permanently on screen is a row of noise between the reader and the staff. Three
of those groups fold into panels behind **Instrument...**, **Options...** and
**Matching...**, which took the control area from seven rows to four and gave
the staff the difference.

What stays on a row is what gets touched every session: the note-entry palette
and the tune library, the grade and its four dials, and the transport. A panel
closes when another opens, and when anything else is clicked - which is why the
component listens for clicks globally rather than only its own: the click that
should dismiss a panel almost always lands on the staff or a button, and those
would otherwise swallow it.

## Status

**Everything planned is done, plus tempo, click, in-time mode and the
sight-reading generator.**

### The range follows the clef

Choosing a clef sets the exercise range to what that clef reads comfortably:
**the five lines plus one ledger line either side**. Nothing inside it needs
more than a single ledger line, which is what makes it comfortable to read.

| Clef | Range | |
|---|---|---|
| Treble | C4 - A5 | the part of the piano the right hand lives in |
| Bass | E2 - C4 | a bass singer's working compass, and a double bass at written pitch |

The two meet exactly at middle C, which is right: middle C is one ledger line
below the treble staff and one above the bass. `theory::getComfortableRange`
derives both from the staff positions rather than hardcoding pitches, so the
range cannot drift away from the staff it is meant to suit. The range remains
adjustable afterwards - the clef only sets the default.

Before this, a treble exercise fitted to a low range spent half its time on
ledger lines below the staff, and choosing bass clef did nothing to the pitches
at all.

### Where the exercise material comes from

The generator samples statistics taken from **855 traditional folk tunes**
(the Nottingham Music Database, in ABC). `Tools/extract_corpus_stats.py` reads
the corpus offline and writes `Source/Practice/CorpusData.cpp`; that generated
file is what ships. **The corpus is not redistributed and no melody from it is
reproduced** - what comes out is aggregate: how often each bar rhythm occurs,
how one scale degree tends to move to the next, which formulas tunes cadence
with, and which degree they start on.

To regenerate after changing the corpus or the parser:

```bash
python Tools/extract_corpus_stats.py <folder-of-abc-files>
```

The parser is deliberately lossy - tuplets, odd metres and bars that do not add
up are skipped rather than guessed at, because a thousand tunes is far more
than enough and a wrong reading pollutes the statistics silently.

What the corpus contributes that hand-written tables did not:

- **93 distinct 4/4 bar rhythms** against the 12 written by hand, weighted by
  how common they really are
- **Real openings**: tunes start on the fifth or the third far more often than
  on the tonic, which no one would guess writing a table from scratch
- **Real cadences**, including repeated-tonic endings, which the hand-written
  version had ruled out
- Compound time that behaves like compound time - a dotted quarter in 6/8 is
  the beat, not a dotted figure

Statistics alone are not enough, though. A Markov chain over corpus intervals
knows nothing about where in the range it has got to, and its characteristic
failure is a phrase that wanders and never arrives. Three things sit on top:

- **Phrase forms** - a b a, a a b, a b c and so on - rather than one fixed
  pattern
- **Motivic restatement**, where a bar repeats an earlier one. This restates it
  at *its own* pitch, not from wherever the line has got to: picking up from the
  previous bar's end made each repeat start lower than the last, and the whole
  line sank down the staff.
- A **tessitura pull** damping motion away from the middle of the compass, in
  proportion to how far out it already is

The compass narrows with the interval level, so a steps-only exercise stays in
a ninth rather than walking across a twelfth one step at a time.

### Generated sight-reading exercises

`Source/Practice/ExerciseGenerator` writes short exercises that hold together
musically. Picking notes at random from a scale is the failure mode to avoid:
it produces something unpleasant that teaches nothing. Real exercises have a
grammar underneath, and the generator follows it.

- A **structural skeleton** drawn from the shapes real phrases use - arches,
  descending lines, neighbour-tone cadences, and gap-fill, where a leap is
  answered by stepwise motion back through the space it opened. Gap-fill is the
  single most reliable device for making a generated leap sound intended.
- The skeleton is **elaborated** with passing and neighbour tones until it has
  as many notes as the rhythm needs, which is how melodies actually get from one
  structural note to the next.
- **Rhythm comes from a vocabulary of real cells**, and the opening cell repeats
  partway through, because repetition is most of what makes a phrase sound
  deliberate rather than arbitrary.
- Strong beats land on **chord tones** of a functional progression, and every
  exercise ends with a proper **cadence** on the tonic, approached by step.
- Minor raises the seventh only where it steps up to the tonic, and never
  straight after the sixth, which is the augmented second natural minor exists
  to avoid.

### Grades, dials and switches

Reading is taught at a **grade**, not at four independent settings, so there is
a grade menu running 1 to 8 that sets everything at once. It is a preset and
nothing more: every dial stays where you can reach it, and moving one drops the
menu back to Custom rather than leaving it claiming something untrue.

The eight levels are a synthesis of what graded syllabuses and college aural
courses actually introduce, and in what order - keys outward from C one
accidental at a time, intervals outward from the step, metres from simple to
compound, and length from four bars to sixteen.

| Grade | Keys | Intervals | Metres added | Bars | Switched on |
|---|---|---|---|---|---|
| 1 | C major | steps | 4/4, 3/4 | 4 | |
| 2 | 1 sharp or flat | thirds | | 4 | |
| 3 | 2, and minor keys | fourths | 2/4 | 8 | rests |
| 4 | 3 sharps or flats | the whole triad | | 8 | upbeat |
| 5 | 4 sharps or flats | sixths | 6/8 | 8 | |
| 6 | 5 sharps or flats | sevenths | 2/2 | 12 | ties over barlines |
| 7 | 6 sharps or flats | the octave | 9/8 | 12 | syncopation |
| 8 | any key | octaves, accidentals | 12/8 | 16 | notes outside the key |

Each of those switches is separate from the grade, because a reader is usually
weak at one particular thing - upbeats, or holding a note over a barline -
rather than at a whole grade of them. The metre can also be pinned to one time
signature for the week the lesson is about that metre, and the minor scale can
be written in its natural, harmonic or melodic form.

Some of the content those switches produce is worth describing, because it is
not obvious how a generator gets it:

- **Rests** go where a singer would breathe, which is the end of a phrase -
  every fourth bar most of the time, and elsewhere just often enough that you
  cannot learn where to expect one.
- **The upbeat** is written as a full bar that opens with rests. The model's own
  rule is that every bar is full; a bar of rests followed by the pickup reads
  identically to a short bar and does not need the rule bent.
- **Ties over the barline and syncopation** are the same written thing - one
  note lasting into where the next would have started - so both are made by
  giving one note the length of two and letting the engraver split and tie it.
  A merge that would open a leap the level does not allow, or a tritone, is
  refused rather than repaired.
- **Accidentals** are the three chromatic inflections that turn up first in real
  music: the fourth raised on its way to the fifth, the seventh lowered on its
  way down to the sixth, the tonic raised on its way up to the second. Each
  leans a semitone in the direction the line was already going, so none of them
  opens an interval the level did not ask for.
- **The longer compound metres** are not in the corpus in any useful quantity -
  slip jigs are rare and nothing at all is in 12/8 - so 9/8 and 12/8 bars are
  built from the dotted-quarter beat-groups the corpus's 6/8 bars are made of.
  Cut common time takes 4/4's rhythms unchanged, because alla breve differs in
  how it is felt and beamed, not in what fills the bar.

Everything is generated to fit a **vocal range you set**, which is what makes a
random key usable instead of occasionally landing somewhere unsingable.

### Phrase structure at length

A sixteen-bar exercise needs more than bar-to-bar coherence or it wanders for
sixteen bars. Bars group into fours, and the groups restate each other - a b a
b, a a b a, the shape most songs have - while within a group individual bars
quote each other. The harmony is a four-bar cycle repeated underneath, so a
restated bar lands on the harmony it was written over.

There is also a floor on how much is actually on the page. An exercise can
legitimately draw a whole bar's worth of note in every bar and end up with
nothing to read, so the emptiest bars are rewritten busier until the line
averages more than a note a bar - which keeps the phrase structure already
chosen, rather than throwing the seed away.

**Variety is asserted, not assumed.** The first version of this generator
produced exactly 16 distinct rhythms in 200 exercises at level 1 - four rhythm
cells and a fixed bar pattern, a ceiling no amount of extra randomness could
lift. Every grade now yields at least 189 distinct melodies out of 200, and 104
to 200 distinct rhythms, and the test fails if it drops back.

**The property tests are the reason this works.** A generator cannot be checked
by looking at one example; the interesting failures are the rare seeds. Musical
invariants are asserted over 25,536 exercises - every combination of the four
dials, and every grade - and each feature is checked both to appear when it is
asked for and to stay away when it is not. Doing so caught seven real bugs that
eyeballing would never have found:

- the leap-clamping pass destroying the cadence, because a `7 -> 1` leading-note
  resolution looks like a plunge of a seventh if degrees are compared naively
- range fitting octave-shifting individual notes, opening 11-semitone leaps in a
  steps-only exercise
- minor keys built on the major root's pitch class, so "up to four accidentals"
  produced D sharp minor
- diatonic tritone leaps between the fourth and seventh degrees
- the harmonic minor's raised seventh making tritones against the fourth that
  the degrees alone cannot see, and the repair pass writing the note back
  without its raise, so the two descriptions of the same note disagreed
- a raised seventh at the top of the range being raised straight out of it
- a merge for a tie or a syncopation swallowing a note and putting its two
  neighbours next to each other at any distance at all


### Practising the way a lesson does

Better material only helps if it is used the way reading is actually taught.
Three things a teacher does every time now have buttons.

**Hear key** plays a cadence in the exercise's key - tonic, subdominant,
dominant, home - and then the note you start on. Sight-singing cold tests pitch
memory rather than reading, which is why every lesson and every exam puts the
key in your ear first. In a minor key the dominant takes its raised third,
because that is what makes a minor cadence sound like one.

**Look first** gives half a minute to read the exercise through before it
starts, with the checklist on screen: key, metre, tempo, shape, the awkward bar.
That is the exam's own allowance, and reading it every time is the point - it is
what stops the eye starting at bar one, note one. Pressing the button again
means "I am ready" and starts immediately.

**Rhythm only** accepts any pitch and judges only whether each note arrived, so
the line can be tapped, played on one key, or sung on whatever note is
comfortable. Rhythm before notes is the oldest piece of sight-reading teaching
there is: you cannot read a line whose rhythm you have not already worked out,
and trying to do both at once is what makes a reader stall. It is a real mode
rather than a lax one - the cursor still advances one note at a time, one held
note still cannot claim two, and in time the timing windows still apply.
Intonation simply stops being judged, which matters if the cents tolerance has
been tightened.

Step by step remains the default and is unaffected by any of it.

### MIDI and PDF

**File...** on the tune row imports a MIDI file, exports the current tune as
MIDI, or writes it out as a PDF score.

Export is straightforward - the model already counts in ticks per quarter, so it
maps onto a MIDI file almost directly. **Import is the awkward direction**: a
MIDI file can hold anything, while a tune here is a single monophonic line. The
choices it makes are deliberate, and it reports them back rather than silently
mangling the file:

- the busiest track is taken, which on a typical file is the melody
- where notes sound together the top one is kept, the usual way to reduce a
  texture to a line
- lengths are rounded to values the engraver can actually draw
- leading silence is trimmed, and a genuinely low line switches to bass clef

PDF has no JUCE support, so `Source/Export/ScorePdf` writes one directly. The
page is a single high-resolution rendering rather than vector drawing: emitting
the glyphs as vectors would mean embedding Bravura as a CID font, which is a
great deal of machinery for a practice sheet. At roughly 200 dpi on A4 it prints
indistinguishably, and the PDF is five objects. `MelodyStaffComponent` grew a
print mode for it - black on white, no caret or playhead, a larger staff, and
justified to the page width the way engraved music is.

The export test counts dark pixels on the page. Checking the PDF header would
pass just as happily on a blank sheet, which is exactly what a print-mode
palette bug would produce.

### Tempo, metronome and in-time rehearsal

### MIDI and PDF

**File...** on the tune row imports a MIDI file, exports the current tune as
MIDI, or writes it out as a PDF score.

Export is straightforward - the model already counts in ticks per quarter, so it
maps onto a MIDI file almost directly. **Import is the awkward direction**: a
MIDI file can hold anything, while a tune here is a single monophonic line. The
choices it makes are deliberate, and it reports them back rather than silently
mangling the file:

- the busiest track is taken, which on a typical file is the melody
- where notes sound together the top one is kept, the usual way to reduce a
  texture to a line
- lengths are rounded to values the engraver can actually draw
- leading silence is trimmed, and a genuinely low line switches to bass clef

PDF has no JUCE support, so `Source/Export/ScorePdf` writes one directly. The
page is a single high-resolution rendering rather than vector drawing: emitting
the glyphs as vectors would mean embedding Bravura as a CID font, which is a
great deal of machinery for a practice sheet. At roughly 200 dpi on A4 it prints
indistinguishably, and the PDF is five objects. `MelodyStaffComponent` grew a
print mode for it - black on white, no caret or playhead, a larger staff, and
justified to the page width the way engraved music is.

The export test counts dark pixels on the page. Checking the PDF header would
pass just as happily on a blank sheet, which is exactly what a print-mode
palette bug would produce.

### Tempo, metronome and in-time rehearsal

The mode selector offers **Step by step** (the default, unchanged: waits for each
note however long you take) and **In time**, which runs the tune against the
metronome and grades when each note arrived. Tempo is per tune and is saved with
it; the click can be silenced without stopping the transport.

Three decisions worth keeping:

- **The transport comes from the audio clock**, not a message-thread stopwatch.
  `Metronome` counts samples in the audio callback and everything else reads
  that, so the click you hear and the timeline you are graded against cannot
  drift apart.
- **Note-ons are timestamped on the audio thread** and passed to the UI through a
  lock-free ring. Diffing held notes on the 30 Hz timer would smear every onset
  by up to 33 ms, which is meaningless against a 90 ms window.
- **Sung notes are graded from the onset, not the confirmation.** The detector
  only confirms after its stability window, so grading the confirmation would
  mark a perfectly timed note ~300 ms late. `VoiceMatchSettings::latencyMs`
  (default 100 ms) then takes off the detector's own delay. There is a unit test
  for exactly this.

A note is not written off as missed until a late voice confirmation could no
longer still be attributed to it, or every sung note would be missed before the
stability window had a chance to finish.

Verified in the app: count-in of one bar, transport advancing correctly,
unplayed notes retired as missed at the right moment, and five notes played
against the click reported as "5 hit, -28 ms" - a believable measurement rather
than timer noise. Timing logic is covered by `RehearsalTests` (30 checks).

**Still unverified: singing.** Everything about the voice path is guesswork until
someone sings at it - the stability window, the tolerance, and especially
`latencyMs`, which directly biases every timing score.

### Earlier phases

Phase 3 added the rehearsal engine, reference playback, and the voice-matching
settings. Rehearse steps through the tune one note at a time, colouring the
current target orange and completed notes blue. Wrong notes are counted but do
not advance. Play tune and Cue note both sound through the existing piano, and
the on-screen keys follow along.

Verified with the MIDI path: rehearsing the 15-note demo and playing the first
eight notes advanced it to "note 9 of 15: A4", with every note matched an octave
away from where it was written - which is the octave-agnostic rule doing its
job. A deliberate wrong note incremented the count and left the target where it
was.

Two design points worth keeping:

- MIDI reaches the engine by diffing the held-note list on the UI timer, not by
  listening to `MidiKeyboardState` directly. The state changes on the audio
  thread, and the engine must not be called from there.
- Notes the player itself is sounding are ignored, or listening back would
  rehearse the tune for you.

**Not verified: the voice path.** It is implemented and wired, but confirming it
needs a human at a microphone. The stability window, tolerance and confidence
gate are all guesses until someone sings at it.

### Phase 1 and 2 notes

Phase 2 added note entry and the tune library. Picking a duration from the
palette and clicking the staff writes a note: the click's height gives the
pitch (read through the key signature, so a click on the top space in D major
is an F sharp) and its horizontal position picks the slot to overwrite. The
caret then advances. Tunes save as readable JSON, one file each, under
`%APPDATA%\MidiCoach\Tunes`.

Verified end to end: clicking out a rising line produced exactly
60 62 64 65 67 69 71 72 - a C major scale with the E-F and B-C semitones
correct - and it round-tripped through save with its metadata intact.

Shortcuts implemented (`1`-`5` values, `.` dot, `R` rest, arrows to move the
caret and nudge the last note, Ctrl+arrow for octaves, Backspace to delete
back). These are wired but were not driven in an automated test - worth a
manual pass.

Still to do in this area: no undo, no way to change the bar count except by
writing past the end, and clicking an existing note overwrites it rather than
selecting it.

### Phase 1 notes

**Phase 1 is done.** `Source/Model/Melody` holds the tune, `Source/UI/StaffLayout`
holds the shared staff primitives, and `Source/UI/MelodyStaffComponent` engraves
it. A Live/Tunes switch in the toolbar swaps the melody staff for the live
snapshot view, currently showing a hardcoded demo tune.

Verified working: clef, time signature, note values from whole to sixteenth,
dots, flags, rests, ledger lines, accidentals spelled against the key, ties
across bar lines, bar lines, and the final thin-thick barline. The model has a
console test target covering the invariant:

```bash
cmake --build build --config Release --target MelodyTests
```

Known rough edges to revisit, none blocking:

- Stems sit a hair away from their noteheads
- Spacing inside a bar can leave a gap before the barline
- Rests use greedy decomposition, so a rest from beat 2 of 4/4 renders as one
  dotted half rather than the conventional quarter-plus-half

Next: **Phase 4** is really just tuning the voice matcher against a real
singer. Everything else on the original plan is in.

## Phasing

1. ~~**Model + engraving core**~~ - done.
2. ~~**Click entry** - caret, duration palette, shadow note, save/load.~~ - done.
3. ~~**Rehearsal + playback** - state machine, MIDI matching, reference playback.~~ - done.
4. **Voice matching** - built and wired; needs tuning against a real voice.
5. ~~**Beaming**~~ - done. Tuplets remain out of scope.
6. ~~**Run review and live input display**~~ - done. The natural follow-ons are
   keeping the reviews rather than discarding them at the end of the run, and
   using what they say to set the grade: the four dials mean errors can be
   attributed to intervals, rhythm, key or length separately, which one
   difficulty slider cannot do.

Beaming sat after rehearsal deliberately: it is cosmetic and did not block
practising, which is why it waited until the material was worth reading.

## Reusable from the existing app

- `AudioAnalyser::getSnapshot()` - smoothed note/cents/confidence, the matcher input
- `theory::Scale::spell()` - enharmonic spelling against the key
- `NotationComponent` positioning - `yForStep`, ledger lines, accidental columns
- `PianoSynth` + `MidiMessageCollector` - the playback path
- `%APPDATA%\MidiCoach\` - already established for settings
