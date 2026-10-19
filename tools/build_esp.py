"""Writes IncorporealGhosts.esp (ESL flagged, master Skyrim.esm only - The Witcher Signs is optional, the DLL finds its
Yrden at runtime -, no edits to other records):

  KYWD  IG_GhostWeapon       weapons / ammo with it hurt incorporeal ghosts (like silver)
  KYWD  IG_Incorporeal       actors / races with it are incorporeal ghosts
  KYWD  IG_NotIncorporeal    actors / races with it never are (wins over everything else)
  SNDR  IG_PassThroughSD     an attack passing through a ghost (tools/make_sounds.py), positional
  MGEF  IG_SpectralCoatingEffect   the Spectral Poison's effect: only a name and a description, it never reaches anyone
                                   (the DLL takes the poison off the weapon and runs its own 12 s spectral coating)
  ALCH  IG_SpectralPoison          the poison (Ectoplasm + Salt Pile + Deathbell, sold by apothecaries)
  BOOK  IG_RecipeSpectralPoison    its recipe note, like the vanilla potion recipes (sold by apothecaries)

The keywords can be added by other plugins or distributed with KID / SPID by editor ID. The DLL finds the forms by
local form id (see src/Forms.h).
usage: python build_esp.py [output.esp]
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "res" / "IncorporealGhosts.esp"
SOUNDS = ROOT / "res" / "Sound" / "FX" / "IncorporealGhosts"
OWN = 0x01000000  # this plugin (one master)

# Skyrim.esm forms
CATEGORY_SFX = 0x172A1           # AudioCategorySFX (the "Effects" volume slider)
OUTPUT_MONO_3D = 0xD78B4         # SOMMono01400_verb: positional with reverb, 150 - 1400 units
DESCRIPTOR_STANDARD = 0x1EEF540A  # BGSStandardSoundDef

# keep in sync with src/Forms.h
GHOST_WEAPON = 0x800
INCORPOREAL = 0x801
NOT_INCORPOREAL = 0x802
PASS_THROUGH = 0x803
COATING_EFFECT = 0x804
POISON = 0x805
RECIPE = 0x806

# Skyrim.esm forms for the poison and the recipe (taken from vanilla poisons and potion recipe notes)
VENDOR_ITEM_POISON = 0x08CDED
VENDOR_ITEM_RECIPE = 0x0F5CB0
POTION_UP, POTION_DOWN, POISON_USE = 0x03EDBD, 0x03EDC0, 0x106614  # ITMPotionUpSD, ITMPotionDownSD, poison use
NOTE_UP, NOTE_ART = 0x0C7A54, 0x01541C                             # ITMNoteUp, HighPolyNote03
POISON_MODEL = r"Clutter\Potions\PoisonWeaknessMagicka.nif"
NOTE_MODEL = r"Clutter\Books\Note03.nif"
POISON_VALUE, RECIPE_VALUE = 120, 75

POISON_NAME = "Spectral Poison"
RECIPE_NAME = "Recipe: Spectral Poison"
EFFECT_DESCRIPTION = "For 12 seconds the poisoned weapon strikes ghosts and Wispmothers as if they had a body."
RECIPE_TEXT = (
    "<p align='center'><font size='26'>Spectral Poison</font></p>\n\n"
    "Ectoplasm, a pinch of salt and a crushed Deathbell flower, combined at an alchemy lab.\n\n"
    "Coat a blade or a bow with it and for twelve heartbeats it will bite into the dead that walk without flesh, "
    "even where no sign holds them."
)


def sub(typ, data):
    if isinstance(data, str):
        data = data.encode("cp1252") + b"\0"
    return typ.encode() + struct.pack("<H", len(data)) + data


def u32(v):
    return struct.pack("<I", v)


def record(typ, formid, subs, flags=0):
    body = b"".join(subs)
    return typ.encode() + struct.pack("<IIIIHH", len(body), flags, formid, 0, 44, 0) + body


def group(label, contents):
    body = b"".join(contents)
    return b"GRUP" + struct.pack("<I", 24 + len(body)) + label.encode() + struct.pack("<iHHHH", 0, 0, 0, 0, 0) + body


def kywd(local, edid):
    return record("KYWD", OWN | local, [sub("EDID", edid)])


def sndr(local, edid, files, output, priority=128, attenuation_db=0.0):
    subs = [sub("EDID", edid), sub("CNAM", u32(DESCRIPTOR_STANDARD)), sub("GNAM", u32(CATEGORY_SFX))]
    subs += [sub("ANAM", f) for f in files]
    subs += [
        sub("ONAM", u32(output)),
        sub("LNAM", bytes([0, 0, 0, 0])),
        # frequency shift, frequency variance, priority, db variance, static attenuation (1/100 dB)
        sub("BNAM", struct.pack("<bbBBH", 0, 6, priority, 0, int(attenuation_db * 100))),
    ]
    return record("SNDR", OWN | local, subs)


def keywords_of(*ids):
    return [sub("KSIZ", u32(len(ids))), sub("KWDA", b"".join(u32(i) for i in ids))]


def mgef_coating():
    # flags: hostile, detrimental, no duration, no magnitude, no area, no hit effect - nothing for perks to scale
    flags = 0x1 | 0x4 | 0x200 | 0x400 | 0x800 | 0x08000000
    data = bytearray(152)
    struct.pack_into("<I", data, 0, flags)
    struct.pack_into("<i", data, 12, -1)          # magic skill: none
    struct.pack_into("<i", data, 16, -1)          # resist value: none
    struct.pack_into("<I", data, 64, 1)           # archetype: script (no script attached)
    struct.pack_into("<i", data, 68, -1)          # actor value: none
    struct.pack_into("<I", data, 80, 1)           # casting type: fire and forget
    struct.pack_into("<I", data, 84, 0)           # delivery: self (like vanilla poisons)
    struct.pack_into("<i", data, 88, -1)          # second actor value: none
    struct.pack_into("<f", data, 116, 1.0)        # dual casting scale
    return record("MGEF", OWN | COATING_EFFECT, [
        sub("EDID", "IG_SpectralCoatingEffect"),
        sub("FULL", POISON_NAME),
        sub("DATA", bytes(data)),
        sub("DNAM", EFFECT_DESCRIPTION),
    ])


def alch_poison():
    return record("ALCH", OWN | POISON, [
        sub("EDID", "IG_SpectralPoison"),
        sub("OBND", struct.pack("<6h", -3, -3, 0, 3, 3, 11)),
        sub("FULL", POISON_NAME),
        *keywords_of(VENDOR_ITEM_POISON),
        sub("MODL", POISON_MODEL),
        sub("YNAM", u32(POTION_UP)),
        sub("ZNAM", u32(POTION_DOWN)),
        sub("DATA", struct.pack("<f", 0.5)),
        # value, flags (no auto value | poison), addiction, addiction chance, use sound
        sub("ENIT", struct.pack("<IIIfI", POISON_VALUE, 0x1 | 0x20000, 0, 0.0, POISON_USE)),
        sub("EFID", u32(OWN | COATING_EFFECT)),
        sub("EFIT", struct.pack("<fII", 0.0, 0, 12)),
    ])


def book_recipe():
    return record("BOOK", OWN | RECIPE, [
        sub("EDID", "IG_RecipeSpectralPoison"),
        sub("OBND", struct.pack("<6h", 0, 0, 0, 0, 0, 0)),
        sub("FULL", RECIPE_NAME),
        sub("MODL", NOTE_MODEL),
        sub("DESC", RECIPE_TEXT),
        sub("YNAM", u32(NOTE_UP)),
        *keywords_of(VENDOR_ITEM_RECIPE),
        # flags, type, unused, teaches (none), value, weight - as the vanilla potion recipes
        sub("DATA", struct.pack("<BBHiIf", 0, 0, 0, -1, RECIPE_VALUE, 0.0)),
        sub("INAM", u32(NOTE_ART)),
        sub("CNAM", u32(0)),
    ])


def main():
    keywords = [kywd(GHOST_WEAPON, "IG_GhostWeapon"), kywd(INCORPOREAL, "IG_Incorporeal"),
                kywd(NOT_INCORPOREAL, "IG_NotIncorporeal")]
    waves = sorted(SOUNDS.glob("passthrough_*.wav"))
    if not waves:
        sys.exit("no sounds: run make_sounds.py first")
    sounds = [sndr(PASS_THROUGH, "IG_PassThroughSD", ["Data\\Sound\\FX\\IncorporealGhosts\\" + w.name for w in waves],
                   OUTPUT_MONO_3D, priority=160)]
    groups = [("KYWD", keywords), ("SNDR", sounds), ("MGEF", [mgef_coating()]), ("ALCH", [alch_poison()]),
              ("BOOK", [book_recipe()])]
    body = b"".join(group(label, recs) for label, recs in groups)
    count = sum(len(recs) + 1 for _, recs in groups)
    header = record("TES4", 0, [
        sub("HEDR", struct.pack("<f", 1.71) + struct.pack("<i", count) + u32(0x900)),
        sub("CNAM", "W1ter"),
        sub("SNAM", "Incorporeal Ghosts"),
        sub("MAST", "Skyrim.esm"), sub("DATA", b"\0" * 8),
    ], flags=0x200)  # ESL
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + body)
    print(f"wrote {OUT} ({len(keywords)} keywords, {len(sounds)} sound descriptor with {len(waves)} files, poison, effect, recipe)")


if __name__ == "__main__":
    main()
