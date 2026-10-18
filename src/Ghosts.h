#pragma once

// The rules: who is incorporeal right now, and which attacks and effects still reach them.
namespace Ghosts
{
	enum class State : std::uint8_t
	{
		kNotGhost,
		kExcluded,     // exclusion lists, a follower or your summon (unless the settings include those)
		kFriendly,     // not hostile to the player (only when "only hostile ghosts" is on)
		kRevealed,     // has a reveal effect
		kInYrden,      // inside a Yrden sign
		kIncorporeal,  // weapons pass through, damaging magic does nothing
	};

	State       GetState(RE::Actor* a_actor);
	bool        InYrden(RE::Actor* a_actor);
	// a ghost that should look solid: inside Yrden or revealed (also friendly ghosts, it's only the look)
	bool        IsMaterialized(RE::Actor* a_actor);
	const char* StateName(State a_state);

	// true: the attack / effect works as in vanilla; false: it passes through (gates are called from the hooks)
	bool MeleeHits(RE::Actor* a_attacker, RE::Actor* a_victim, RE::Projectile* a_projectile, bool a_leftHand);
	bool ProjectileHits(RE::Projectile* a_projectile, RE::TESObjectREFR* a_target);
	bool HitLands(RE::Actor* a_victim, const RE::HitData& a_hit);
	bool EffectApplies(RE::Actor* a_target, const RE::MagicTarget::AddTargetData& a_data);
}
