"""Convert Bravura.otf (CFF outlines) into a TrueType-flavoured font we can ship.

JUCE loads embedded fonts on Windows through GDI's AddFontMemResourceEx, which
does not handle OpenType/CFF ("OTTO") fonts - GDI silently substitutes another
font, and every SMuFL codepoint then resolves to "glyph not found". Feeding it
TrueType-flavoured outlines instead makes the same font load correctly.

The output is renamed, and that is a licensing requirement rather than a
preference. Bravura is under the SIL Open Font License 1.1 with the Reserved
Font Name "Bravura", and OFL clause 3 forbids a Modified Version from carrying
the reserved name. Converting the outlines makes this a Modified Version, so the
name table is rewritten before it is saved. The copyright and licence entries
are deliberately left untouched, because OFL clause 1 requires them to travel
with the font.

Run this once after downloading a new Bravura release:

    python Tools/otf_to_ttf.py Resources/Bravura.otf Resources/MidiCoachMusic.ttf
"""

import sys

from fontTools.ttLib import TTFont, newTable
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen

# What the converted font calls itself. Anything but "Bravura".
FAMILY_NAME = "MidiCoach Music"
POSTSCRIPT_NAME = "MidiCoachMusic"

# name IDs that make up "the primary font name as presented to the users", which
# is the phrase OFL clause 3 uses. Everything else in the table - copyright,
# licence, vendor, version - is left exactly as Steinberg published it.
FAMILY_IDS = (1, 16, 21)          # family, typographic family, WWS family
FULL_NAME_IDS = (4, 18)           # full name, and the Mac compatible full name
UNIQUE_ID = 3
POSTSCRIPT_ID = 6

# Maximum permitted deviation when approximating cubic curves with quadratics,
# in font units. At Bravura's 1000-unit em this is far below one screen pixel.
MAX_ERROR = 1.0


def convert(source_path, destination_path):
    font = TTFont(source_path)

    if "glyf" in font:
        raise SystemExit("%s already has TrueType outlines" % source_path)

    glyph_order = font.getGlyphOrder()
    glyph_set = font.getGlyphSet()

    glyf = newTable("glyf")
    glyf.glyphOrder = glyph_order
    glyf.glyphs = {}

    for name in glyph_order:
        pen = TTGlyphPen(glyph_set)
        glyph_set[name].draw(Cu2QuPen(pen, MAX_ERROR, reverse_direction=True))
        glyf.glyphs[name] = pen.glyph()

    font["glyf"] = glyf

    # loca is rebuilt from glyf on compile, but the table has to exist first.
    font["loca"] = newTable("loca")

    # TrueType outlines are y-up with a different metric origin convention than
    # CFF, so these head fields have to be declared rather than inherited.
    head = font["head"]
    head.glyphDataFormat = 0
    head.indexToLocFormat = 0

    maxp = font["maxp"]
    maxp.tableVersion = 0x00010000
    maxp.maxZones = 1
    maxp.maxTwilightPoints = 0
    maxp.maxStorage = 0
    maxp.maxFunctionDefs = 0
    maxp.maxInstructionDefs = 0
    maxp.maxStackElements = 0
    maxp.maxSizeOfInstructions = 0
    maxp.maxComponentElements = max(
        (len(getattr(g, "components", [])) for g in glyf.glyphs.values()), default=0
    )

    for table in ("CFF ", "VORG"):
        if table in font:
            del font[table]

    rename(font)

    font.sfntVersion = "\000\001\000\000"
    font.save(destination_path)

    print("wrote %s (%d glyphs) as %r" % (destination_path, len(glyph_order), FAMILY_NAME))


def rename(font):
    """Gives the modified font a name of its own, as the OFL requires."""
    name_table = font["name"]

    for record in name_table.names:
        if record.nameID in FAMILY_IDS:
            record.string = FAMILY_NAME
        elif record.nameID in FULL_NAME_IDS:
            record.string = FAMILY_NAME
        elif record.nameID == POSTSCRIPT_ID:
            record.string = POSTSCRIPT_NAME
        elif record.nameID == UNIQUE_ID:
            # Has to stay unique, so keep whatever qualifiers came with it and
            # only swap the part that was the reserved name.
            record.string = record.toUnicode().replace("Bravura", FAMILY_NAME)

    for record in name_table.names:
        if record.nameID in (0, 7, 13, 14):
            continue

        if "Bravura" in record.toUnicode():
            raise SystemExit(
                "name ID %d still says Bravura: %r" % (record.nameID, record.toUnicode())
            )


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: otf_to_ttf.py <input.otf> <output.ttf>")

    convert(sys.argv[1], sys.argv[2])
