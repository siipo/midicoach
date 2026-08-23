"""Turn a corpus of ABC folk tunes into the statistics the exercise generator uses.

Run offline, not at build time. It emits Source/Practice/CorpusData.cpp, which is
what ships - the corpus itself is not redistributed, and neither is any melody
from it. What comes out the other side is aggregate: how often each bar-rhythm
occurs, how likely one scale degree is to move to another, and which formulas
tunes actually cadence with.

    python Tools/extract_corpus_stats.py <folder-of-abc-files>

The parser is deliberately lossy. Anything it cannot read confidently - tuplets,
odd metres, bars that do not add up - is skipped rather than guessed at, because
a thousand tunes is far more than enough and a wrong reading pollutes the
statistics silently.
"""

import collections
import glob
import os
import re
import sys

TICKS_PER_QUARTER = 960

# The note values the engraver can draw. A bar made of anything else is no use.
WRITABLE = {240, 480, 720, 960, 1440, 1920, 2880, 3840}

# Metres the generator can write in, and their bar length in ticks.
METRES = {
    (4, 4): 4 * TICKS_PER_QUARTER,
    (3, 4): 3 * TICKS_PER_QUARTER,
    (2, 4): 2 * TICKS_PER_QUARTER,
    (6, 8): 3 * TICKS_PER_QUARTER,
}

LETTERS = "CDEFGAB"


def strip_decorations(text):
    text = re.sub(r'"[^"]*"', "", text)      # chord symbols
    text = re.sub(r"!.*?!", "", text)        # decorations
    text = re.sub(r"\{[^}]*\}", "", text)    # grace notes
    text = re.sub(r"%.*", "", text)          # comments
    text = re.sub(r"\[[123](?=[^\]]*$)", "", text)
    text = re.sub(r"\[[A-Za-z]:[^\]]*\]", "", text)   # inline fields
    return text


def parse_key(value):
    """Returns the tonic's letter index, or None if unreadable."""
    value = value.strip()
    match = re.match(r"^([A-Ga-g])([#b]?)", value)
    if not match:
        return None
    return LETTERS.index(match.group(1).upper())


def parse_unit_length(value, metre):
    match = re.match(r"\s*(\d+)\s*/\s*(\d+)", value or "")
    if match:
        return int(match.group(1)), int(match.group(2))
    # ABC's default: 1/16 for short metres, otherwise 1/8.
    if metre and metre[0] / metre[1] < 0.75:
        return 1, 16
    return 1, 8


TOKEN = re.compile(r"""
    (?P<accidental>\^{1,2}|_{1,2}|=)?
    (?P<letter>[A-Ga-gz])
    (?P<octave>[,']*)
    (?P<numerator>\d+)?
    (?P<slash>/+)?
    (?P<denominator>\d+)?
""", re.VERBOSE)


def parse_body(body, unit_length, tonic_letter):
    """Yields bars, each a list of (degree, ticks). degree is None for a rest."""
    unit_numerator, unit_denominator = unit_length

    for raw_bar in re.split(r"\|+", body):
        if not raw_bar.strip():
            continue

        notes = []
        broken = []          # pending > or < applied between two notes
        position = 0
        ok = True

        while position < len(raw_bar):
            character = raw_bar[position]

            if character in "><":
                run = 0
                while position < len(raw_bar) and raw_bar[position] == character:
                    run += 1
                    position += 1
                broken.append((character, run, len(notes)))
                continue

            if character == "-":       # tie: fold into the previous note
                position += 1
                if notes:
                    notes[-1] = (notes[-1][0], notes[-1][1], True)
                continue

            match = TOKEN.match(raw_bar, position)
            if not match or not match.group("letter"):
                position += 1
                continue

            position = match.end()

            multiplier = float(match.group("numerator") or 1)
            if match.group("slash"):
                divisor = match.group("denominator")
                multiplier /= float(divisor) if divisor else 2 ** len(match.group("slash"))
            elif match.group("denominator"):
                ok = False
                break

            quarters = multiplier * (unit_numerator / unit_denominator) * 4
            ticks = quarters * TICKS_PER_QUARTER

            letter = match.group("letter")
            if letter == "z":
                degree = None
            else:
                index = LETTERS.index(letter.upper())
                octave = 1 if letter.islower() else 0
                octave += match.group("octave").count("'")
                octave -= match.group("octave").count(",")
                degree = (index - tonic_letter) + octave * 7

            notes.append((degree, ticks, False))

        if not ok or not notes:
            continue

        # Broken rhythm: a>b dots the first and halves the second.
        for character, run, index in broken:
            if index == 0 or index >= len(notes):
                continue
            factor = 1.5 if run == 1 else 1.75
            first, second = index - 1, index
            if character == "<":
                first, second = second, first
            notes[first] = (notes[first][0], notes[first][1] * factor, notes[first][2])
            notes[second] = (notes[second][0], notes[second][1] * (2 - factor), notes[second][2])

        # Fold tied notes together.
        folded = []
        for degree, ticks, tied in notes:
            if folded and folded[-1][2] and folded[-1][0] == degree:
                folded[-1] = (degree, folded[-1][1] + ticks, tied)
            else:
                folded.append((degree, ticks, tied))

        yield [(degree, int(round(ticks))) for degree, ticks, _ in folded]


def parse_tunes(text):
    for chunk in re.split(r"\nX:", "\n" + text):
        if not chunk.strip():
            continue

        lines = chunk.splitlines()
        metre = None
        unit = None
        tonic = None
        body = []
        in_body = False

        for line in lines:
            field = re.match(r"^([A-Za-z]):(.*)$", line)
            if field and len(field.group(1)) == 1:
                name, value = field.group(1), field.group(2)
                if name == "M":
                    match = re.match(r"\s*(\d+)\s*/\s*(\d+)", value)
                    if match:
                        metre = (int(match.group(1)), int(match.group(2)))
                    in_body = False
                elif name == "L":
                    unit = value
                elif name == "K":
                    tonic = parse_key(value)
                    in_body = True
                continue

            if in_body:
                body.append(line)

        if metre is None or tonic is None or not body:
            continue

        yield metre, parse_unit_length(unit, metre), tonic, strip_decorations("\n".join(body))


def classify(metre, lengths):
    """How hard a bar is to read.

    Metre-aware on purpose. A dotted quarter in 6/8 is the beat, not a dotted
    figure, and calling it "dotted" would leave compound time with no easy
    rhythms at all.
    """
    if len(lengths) == 1:
        return 1                        # one note filling the bar

    if 240 in lengths:
        return 4                        # sixteenths

    if metre[1] == 8 and metre[0] % 3 == 0:
        if all(t == 1440 for t in lengths):
            return 1                    # just the beats
        if 720 in lengths:
            return 3                    # a real dot inside the beat
        return 2

    if any(t in (720, 1440, 2880) for t in lengths):
        return 3

    return 1 if min(lengths) >= 960 else 2


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    folder = sys.argv[1]
    files = sorted(glob.glob(os.path.join(folder, "*.abc")))

    if not files:
        raise SystemExit("no .abc files in " + folder)

    rhythms = {metre: collections.Counter() for metre in METRES}
    transitions = collections.Counter()     # (fromDegree0to6, interval) -> count
    cadences = collections.Counter()        # (a, b) leading to the tonic
    openings = collections.Counter()

    tunes = 0
    usable_bars = 0

    for path in files:
        with open(path, "r", encoding="utf-8", errors="ignore") as handle:
            text = handle.read()

        for metre, unit, tonic, body in parse_tunes(text):
            if metre not in METRES:
                continue
            if re.search(r"\(\d", body):        # tuplets: skip the tune entirely
                continue

            tunes += 1
            bar_ticks = METRES[metre]
            melody = []

            for bar in parse_body(body, unit, tonic):
                lengths = [ticks for _, ticks in bar]

                if sum(lengths) == bar_ticks and all(t in WRITABLE for t in lengths) \
                        and all(degree is not None for degree, _ in bar):
                    rhythms[metre][tuple(lengths)] += 1
                    usable_bars += 1

                melody.extend(degree for degree, _ in bar if degree is not None)

            if len(melody) < 6:
                continue

            openings[melody[0]] += 1

            for previous, current in zip(melody, melody[1:]):
                interval = current - previous
                if -7 <= interval <= 7:
                    transitions[(previous % 7, interval)] += 1

            # Tunes that finish on the tonic tell us how tunes actually land.
            if melody[-1] % 7 == 0:
                cadences[(melody[-3] - melody[-1], melody[-2] - melody[-1])] += 1

    print("read %d tunes, %d usable bars" % (tunes, usable_bars))

    for metre in METRES:
        print("  %d/%d: %d distinct bar rhythms" % (metre[0], metre[1], len(rhythms[metre])))

    # ---- emit ---------------------------------------------------------------
    out = []
    out.append("// Generated by Tools/extract_corpus_stats.py - do not edit by hand.")
    out.append("//")
    out.append("// Aggregate statistics from %d traditional folk tunes: how often each" % tunes)
    out.append("// bar rhythm occurs, how one scale degree tends to move to the next, and")
    out.append("// the formulas tunes actually cadence with. No melody is reproduced here.")
    out.append("")
    out.append('#include "CorpusData.h"')
    out.append("")
    out.append("namespace practice")
    out.append("{")
    out.append("namespace corpus")
    out.append("{")
    out.append("")

    out.append("const std::vector<RhythmCell>& getRhythmCells()")
    out.append("{")
    out.append("    static const std::vector<RhythmCell> cells")
    out.append("    {")

    for metre in sorted(METRES):
        for lengths, count in rhythms[metre].most_common(56):
            if count < 3:
                continue
            level = classify(metre, lengths)
            values = ", ".join(str(t) for t in lengths)
            out.append("        { %d, %d, %d, %d, { %s } },"
                       % (metre[0], metre[1], level, count, values))

    out.append("    };")
    out.append("")
    out.append("    return cells;")
    out.append("}")
    out.append("")

    out.append("const std::vector<Transition>& getTransitions()")
    out.append("{")
    out.append("    static const std::vector<Transition> transitions")
    out.append("    {")
    for (degree, interval), count in sorted(transitions.items()):
        if count < 5:
            continue
        out.append("        { %d, %d, %d }," % (degree, interval, count))
    out.append("    };")
    out.append("")
    out.append("    return transitions;")
    out.append("}")
    out.append("")

    out.append("const std::vector<Cadence>& getCadences()")
    out.append("{")
    out.append("    static const std::vector<Cadence> cadences")
    out.append("    {")
    for (a, b), count in cadences.most_common(28):
        if count < 4 or not (-7 <= a <= 7 and -7 <= b <= 7):
            continue
        out.append("        { %d, %d, %d }," % (a, b, count))
    out.append("    };")
    out.append("")
    out.append("    return cadences;")
    out.append("}")
    out.append("")

    out.append("const std::vector<Opening>& getOpenings()")
    out.append("{")
    out.append("    static const std::vector<Opening> openings")
    out.append("    {")
    for degree, count in openings.most_common(14):
        if count < 4 or not (-7 <= degree <= 9):
            continue
        out.append("        { %d, %d }," % (degree, count))
    out.append("    };")
    out.append("")
    out.append("    return openings;")
    out.append("}")
    out.append("")
    out.append("} // namespace corpus")
    out.append("} // namespace practice")
    out.append("")

    destination = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "..", "Source", "Practice", "CorpusData.cpp")

    with open(os.path.normpath(destination), "w", encoding="utf-8") as handle:
        handle.write("\n".join(out))

    print("wrote", os.path.normpath(destination))


if __name__ == "__main__":
    main()
