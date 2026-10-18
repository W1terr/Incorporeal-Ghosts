"""Writes IncorporealGhosts.esp (ESL flagged, masters Skyrim.esm and W3S.esl - The Witcher Signs is required -, no
edits to other records):

  KYWD  IG_GhostWeapon       weapons / ammo with it hurt incorporeal ghosts (like silver)
  KYWD  IG_Incorporeal       actors / races with it are incorporeal ghosts
  KYWD  IG_NotIncorporeal    actors / races with it never are (wins over everything else)
  SNDR  IG_PassThroughSD     an attack passing through a ghost (tools/make_sounds.py), positional

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
OWN = 0x02000000  # this plugin (two masters)

# Skyrim.esm forms
CATEGORY_SFX = 0x172A1           # AudioCategorySFX (the "Effects" volume slider)
OUTPUT_MONO_3D = 0xD78B4         # SOMMono01400_verb: positional with reverb, 150 - 1400 units
DESCRIPTOR_STANDARD = 0x1EEF540A  # BGSStandardSoundDef

# keep in sync with src/Forms.h
GHOST_WEAPON = 0x800
INCORPOREAL = 0x801
NOT_INCORPOREAL = 0x802
PASS_THROUGH = 0x803


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


def main():
    keywords = [kywd(GHOST_WEAPON, "IG_GhostWeapon"), kywd(INCORPOREAL, "IG_Incorporeal"),
                kywd(NOT_INCORPOREAL, "IG_NotIncorporeal")]
    waves = sorted(SOUNDS.glob("passthrough_*.wav"))
    if not waves:
        sys.exit("no sounds: run make_sounds.py first")
    sounds = [sndr(PASS_THROUGH, "IG_PassThroughSD", ["Data\\Sound\\FX\\IncorporealGhosts\\" + w.name for w in waves],
                   OUTPUT_MONO_3D, priority=160)]
    body = group("KYWD", keywords) + group("SNDR", sounds)
    count = len(keywords) + len(sounds) + 2
    header = record("TES4", 0, [
        sub("HEDR", struct.pack("<f", 1.71) + struct.pack("<i", count) + u32(0x900)),
        sub("CNAM", "W1ter"),
        sub("SNAM", "Incorporeal Ghosts"),
        sub("MAST", "Skyrim.esm"), sub("DATA", b"\0" * 8),
        sub("MAST", "W3S.esl"), sub("DATA", b"\0" * 8),
    ], flags=0x200)  # ESL
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + body)
    print(f"wrote {OUT} ({len(keywords)} keywords, {len(sounds)} sound descriptor with {len(waves)} files)")


if __name__ == "__main__":
    main()
