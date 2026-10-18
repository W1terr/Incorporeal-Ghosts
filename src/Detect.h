#pragma once

// What is a ghost and what is a ghost weapon. Built from the ini lists at data load; safe to ask from any thread.
namespace Detect
{
	// why an actor counts as a ghost
	enum class Kind : std::uint8_t
	{
		kNone,
		kListed,   // [Ghosts] Actors
		kKeyword,  // [Ghosts] Keywords on the actor or its race
		kRace,     // [Ghosts] Races
		kAbility,  // a ghost ability in its spell lists ([Ghosts] Spells / AbilityShaders)
	};

	void Init();

	Kind        GhostKind(RE::Actor* a_actor);  // cached for a few seconds per actor
	bool        IsExcluded(RE::Actor* a_actor);
	const char* KindName(Kind a_kind);

	bool IsGhostWeapon(const RE::TESObjectWEAP* a_weapon, const RE::EnchantmentItem* a_instanceEnchantment);
	bool IsGhostAmmo(const RE::TESAmmo* a_ammo);
	bool IsGhostEnchantment(const RE::EnchantmentItem* a_enchantment);
	// the weapon in that hand (with its own enchantment, e.g. one the player put on it)
	bool HandHasGhostWeapon(RE::Actor* a_actor, bool a_leftHand);
	// a weapon as the source of a hit: its enchantment from whichever hand holds it
	bool IsGhostWeaponOf(RE::Actor* a_owner, const RE::TESObjectWEAP* a_weapon);

	bool IsAllowedMagic(const RE::EffectSetting* a_effect);  // [Magic] AllowedKeywords
	bool HasRevealEffect(RE::Actor* a_actor);                 // [Yrden] RevealEffects
	bool HasHazardEffect(RE::Actor* a_actor);                 // [Yrden] HazardEffects
	bool IsYrdenHazard(const RE::TESForm* a_base);            // [Yrden] Hazards
	bool HasYrdenHazards();

	bool IsGhostShader(const RE::TESEffectShader* a_shader);  // [Ghosts] AbilityShaders
	bool IsGhostEffect(const RE::EffectSetting* a_effect);     // an effect of a ghost ability
}
