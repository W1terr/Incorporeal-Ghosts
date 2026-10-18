# Incorporeal Ghosts

Ghosts and Wispmothers become incorporeal, like specters in The Witcher 3: ordinary weapons and damaging magic pass
right through them. Hurt them with ghost weapons (silver, Ghostblade, Dawnbreaker, bound weapons, Turn Undead
enchantments), or trap them in Yrden from The Witcher Signs, where they take physical form and anything can hit them.

## Features

Weapons pass through ghosts and damaging magic does nothing. You have two ways to hurt them:

- **Ghost weapons**: silver weapons, Ghostblade, Dawnbreaker, bound weapons, and weapons with an undead bane
  enchantment (Turn Undead, Auriel's Bow...). They work anywhere, for you and for your followers.
- **Yrden** from [The Witcher Signs](https://www.nexusmods.com/skyrimspecialedition/mods/90645): a ghost inside the
  sign's circle takes physical form: its ghost shader comes off and it fades back to solid (Wispmothers, whose look is
  in their mesh, glow with Yrden's purple instead). Every weapon, spell and shout hurts it until it steps out again,
  when it turns ghostly and incorporeal again.

When an attack passes through a ghost there is no hit, no blood, no stagger and no damage, and a spectral whoosh
plays. Arrows, spells and shouts that would do nothing fly straight through the ghost. The ghosts look and fight
exactly as in vanilla.

## Requirements

- SKSE, Address Library for SKSE Plugins (SE 1.5.97 or AE 1.6.x / 1.7.x)
- The Witcher Signs (W3S.esl): a master of IncorporealGhosts.esp
- Optional: SKSE Menu Framework for the in-game settings and a status page (nearby ghosts and their state, active
  Yrden signs, whether your weapons are ghost weapons)

The menu and messages come in the 9 languages Skyrim ships with (English, French, German, Italian, Spanish, Polish,
Russian, Japanese, Chinese) and follow the game's language unless you pick another one on the Language page.

## How it works

The engine already makes some actors untouchable with its "ghost" flag. Three functions check that flag: the melee
hit, the projectile collision check, and the function that adds magic effects. Incorporeal Ghosts adds its own rule
at the same three places, plus the final weapon hit function as a safety net, so it looks like the engine's own
behavior. The hit functions are hooked at their entry, so every attacker (you, followers, enemies) and combat mods
that run hits themselves (Precision, Valhalla Combat) go through the same rules. Their entry bytes are checked first;
if another mod already hooked an entry, every call to the function is hooked instead (found in the running game).

| | Hooked function (SE / AE id) |
|---|---|
| Melee swings | MeleeHit 37673 / 38627 (every call) |
| Arrows, bolts, spell projectiles, shouts | Projectile collision 43011 / 44202 (every call) |
| Any other weapon hit | ProcessHit 37633 / 38586 (every call) |
| Spells, enchantments, explosions, cloaks, shouts' effects | Character MagicTarget::AddTarget (vtable) |

Yrden: The Witcher Signs places a hazard (`_W3S_YrdenHazard`, W3S.esl 0x820) at the caster's feet. A ghost is inside
when it is within the hazard's radius (14.5 × 22 = 319 units, the engine's own conversion), checked again on every hit.
A ghost that steps out is incorporeal again on the next hit.

## Detection, and making your mod compatible

Everything is data driven: `SKSE\Plugins\IncorporealGhosts.ini` holds the rules, and every `.ini` in
`SKSE\Plugins\IncorporealGhosts\` adds to its lists. A patch is one small file, no plugin needed.

**Vanilla has no single ghost marker** (checked in Skyrim.esm, Update, Dawnguard and Dragonborn):

- The `ActorTypeGhost` keyword (0xD205E) is only on some ghost NPC records. It isn't on any race, and it's missing from
  the Forelhost, Rannveig's Fast, Ansilvund and Volunruud ghosts and the Headless Horseman, among others.
- Wispmothers use `WispRace` (0x13208) without a ghost keyword. Their shades use `WispShadeRace` (0xF1182).
- Every ghost does have a *Ghost Ability* spell: a constant scripted effect that puts a ghost effect shader on it
  (`GhostAbility`, `GhostAbilityNew`, `MGGhostAbility`, the Soul Cairn and Nightingale variants...).
- The NPC record's "Is Ghost" flag marks invulnerable story characters (Daedric Princes...), not ghosts.

So an actor is a ghost if any of these is true:

1. it or its race has a keyword from `[Ghosts] Keywords`: `ActorTypeGhost`, or this mod's `IG_Incorporeal`
2. its race is in `[Ghosts] Races`: WispRace, WispShadeRace, DLC2GhostFrostGiantRace
3. its spell list (NPC, race, or added by script) has a ghost ability. That is every spell with a constant, non-hostile
   scripted effect using a shader from `[Ghosts] AbilityShaders`, plus the spells in `[Ghosts] Spells`
4. its base record is in `[Ghosts] Actors`

In vanilla that covers 57 NPC records. It leaves out ghosts that are friendly to you (they stay solid until they turn
hostile), your followers and your own summons. It also leaves out `[Exclude]` (the Greybeards' practice phantom,
Conjure Familiar's spectral wolf, Waking Nightmare's hallucinated skeevers) and anything with `IG_NotIncorporeal`.

**For mod authors:** a ghost that uses the vanilla GhostAbility, or any ability with a vanilla ghost shader, works
automatically. Otherwise give the NPC or its race `ActorTypeGhost`, or `IG_Incorporeal` (KID / SPID can distribute
it by editor id).

**Ghost weapons:** a weapon, bow, crossbow or ammo is a ghost weapon if:

1. it has a keyword from `[GhostWeapons] Keywords`: `WeapMaterialSilver` (every silver weapon, vanilla or modded), or
   `IG_GhostWeapon`
2. it is in `[GhostWeapons] Weapons` (Ghostblade, which has no keyword)
3. its enchantment is in `[GhostWeapons] Enchantments` (Ghostblade's, Dawnbreaker's), or with *undead bane* on, its
   enchantment has a Turn Undead effect, an effect with `MagicTurnUndead`, or an effect from `EnchantmentEffects`
   (Auriel's Bow's undead sun damage). The weapon record's enchantment and the item's own enchantment (one you put on
   it) both count.

Bound weapons (every weapon flagged as bound: swords, battleaxes, bows and their arrows, daggers, modded ones)
count through the bound weapon setting, on by default.

**Other Yrden-like mods:** add their hazard to `[Yrden] Hazards`, or an effect that reveals ghosts to
`[Yrden] RevealEffects`.

## Settings (ini `[General]` / SKSE Menu Framework)

| Setting | Default | |
|---|---|---|
| bEnabled | 1 | |
| bOnlyHostile | 1 | ghosts not hostile to you stay solid |
| bAffectFollowers / bAffectSummons | 0 | your ghost followers / ghosts you or your followers summon |
| iMagicImmunity | 1 | 0 = vanilla magic, 1 = damage / drain / absorb / stagger do nothing, 2 = no hostile magic |
| bUndeadBane | 1 | Turn Undead / undead bane enchantments make ghost weapons |
| bBoundWeapons | 1 | bound weapons are ghost weapons |
| bPassThroughSound | 1 | |
| bMaterialize / fFadeTime | 1 / 0.4 | ghosts take physical form inside Yrden, over this many seconds |
| fYrdenRadius | 1 | multiplier on Yrden's circle |
| fYrdenAbove / fYrdenBelow | 400 / 150 | how far above / below the sign a ghost still counts (Wispmothers float) |
| bDebugLog | 0 | every pass-through and blocked effect in `IncorporealGhosts.log` |
| sLanguage | auto | menu language: `auto` (the game's), `english`, `french`, `german`, `italian`, `spanish`, `polish`, `russian`, `japanese`, `chinese` |

## Building

xmake + CommonLibSSE-NG (`lib/commonlibsse-ng`), `build.bat`. The plugin and sounds are generated:
`tools/make_sounds.py` (layers vanilla sounds, needs numpy + scipy and the game's BSAs), `tools/build_esp.py`, then
`tools/package.py <build dir> <output dir>`. Translations are in `res/SKSE/Plugins/IncorporealGhosts/lang`;
`tools/check_lang.py` lists texts missing from them.
