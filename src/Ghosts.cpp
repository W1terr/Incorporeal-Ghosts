#include "Ghosts.h"

#include "Detect.h"
#include "Feedback.h"
#include "Forms.h"
#include "Settings.h"
#include "Yrden.h"

namespace Ghosts
{
	namespace
	{
		using Archetype = RE::EffectArchetypes::ArchetypeID;
		using SpellType = RE::MagicSystem::SpellType;

		// effects that hurt: damage, drain, absorb, slow, stagger, knock away, disarm
		bool IsDamaging(const RE::EffectSetting* a_effect)
		{
			switch (a_effect->GetArchetype()) {
			case Archetype::kValueModifier:
			case Archetype::kDualValueModifier:
			case Archetype::kPeakValueModifier:
			case Archetype::kAbsorb:
			case Archetype::kAccumulateMagnitude:
			case Archetype::kValueAndParts:
			case Archetype::kConcussion:
			case Archetype::kStagger:
			case Archetype::kDisarm:
				return true;
			default:
				return false;
			}
		}

		// an effect on an incorporeal ghost, whoever cast it
		bool EffectAllowed(const RE::EffectSetting* a_effect)
		{
			if (!a_effect || (!a_effect->IsHostile() && !a_effect->IsDetrimental())) {
				return true;
			}
			if (Detect::IsAllowedMagic(a_effect)) {
				return true;
			}
			switch (Settings::Get().magic) {
			case Settings::MagicImmunity::kNone:
				return true;
			case Settings::MagicImmunity::kAllHostile:
				return false;
			default:
				return !IsDamaging(a_effect);
			}
		}

		// summoned or commanded by the player or a follower (enemies' summoned ghosts are ghosts like any other)
		bool IsPlayerSummon(RE::Actor* a_actor)
		{
			if (!a_actor->IsCommandedActor()) {
				return false;
			}
			const auto commander = a_actor->GetCommandingActor();
			return commander && (commander->IsPlayerRef() || commander->IsPlayerTeammate());
		}

		RE::Actor* AsActor(RE::TESObjectREFR* a_ref)
		{
			return a_ref && a_ref->GetFormType() == RE::FormType::ActorCharacter ? static_cast<RE::Actor*>(a_ref) : nullptr;
		}

		void Trace(std::string_view a_what, RE::Actor* a_ghost, const RE::TESForm* a_with)
		{
			if (Settings::Get().debugLog) {
				logs::info("{} passes through {} [{:08X}] ({})", a_with ? Forms::Name(a_with) : std::string(a_what),
					Forms::Name(a_ghost), a_ghost->GetFormID(), a_what);
			}
		}
	}

	State GetState(RE::Actor* a_actor)
	{
		const auto& s = Settings::Get();
		if (!s.enabled || !a_actor || a_actor->IsPlayerRef() || a_actor->IsDead()) {
			return State::kNotGhost;
		}
		if (Detect::GhostKind(a_actor) == Detect::Kind::kNone) {
			return State::kNotGhost;
		}
		if (Detect::IsExcluded(a_actor) || (!s.affectFollowers && a_actor->IsPlayerTeammate()) ||
			(!s.affectSummons && IsPlayerSummon(a_actor))) {
			return State::kExcluded;
		}
		if (s.onlyHostile && !a_actor->IsInCombat()) {
			auto* const player = RE::PlayerCharacter::GetSingleton();
			if (!player || !a_actor->IsHostileToActor(player)) {
				return State::kFriendly;
			}
		}
		if (Detect::HasRevealEffect(a_actor)) {
			return State::kRevealed;
		}
		return InYrden(a_actor) ? State::kInYrden : State::kIncorporeal;
	}

	bool InYrden(RE::Actor* a_actor)
	{
		if (Yrden::Contains(a_actor->GetPosition())) {
			return true;
		}
		// The Witcher Signs marks actors inside its hazard with an effect that lasts 3 s: only a fallback, for when
		// the sign itself can't be found
		return !Yrden::Any() && Detect::HasHazardEffect(a_actor);
	}

	bool IsMaterialized(RE::Actor* a_actor)
	{
		if (!Settings::Get().enabled || !a_actor || a_actor->IsPlayerRef() || a_actor->IsDead() ||
			Detect::GhostKind(a_actor) == Detect::Kind::kNone || Detect::IsExcluded(a_actor)) {
			return false;
		}
		return Detect::HasRevealEffect(a_actor) || InYrden(a_actor);
	}

	const char* StateName(State a_state)
	{
		switch (a_state) {
		case State::kExcluded:
			return "excluded";
		case State::kFriendly:
			return "friendly (solid)";
		case State::kRevealed:
			return "revealed (vulnerable)";
		case State::kInYrden:
			return "in Yrden (vulnerable)";
		case State::kIncorporeal:
			return "incorporeal";
		default:
			return "not a ghost";
		}
	}

	namespace
	{
		bool SpellReachesGhosts(const RE::MagicItem* a_spell)
		{
			if (!a_spell) {
				return false;
			}
			return std::ranges::any_of(a_spell->effects, [](const RE::Effect* a_effect) {
				return a_effect && EffectAllowed(a_effect->baseEffect);
			});
		}

		bool ProjectileHurtsGhosts(RE::Projectile* a_projectile)
		{
			auto& rd = a_projectile->GetProjectileRuntimeData();
			if (rd.weaponSource || rd.ammoSource) {
				if (Detect::IsGhostAmmo(rd.ammoSource)) {
					return true;
				}
				if (auto* const arrow = a_projectile->As<RE::ArrowProjectile>(); arrow) {
					if (Detect::IsGhostEnchantment(arrow->GetArrowRuntimeData().enchantItem)) {
						return true;
					}
				}
				const auto shooter = rd.shooter.get();
				return Detect::IsGhostWeaponOf(AsActor(shooter.get()), rd.weaponSource);
			}
			// spells, staves, scrolls and shouts: fly through unless something in them reaches ghosts
			return SpellReachesGhosts(rd.spell);
		}
	}

	bool MeleeHits(RE::Actor* a_attacker, RE::Actor* a_victim, RE::Projectile* a_projectile, bool a_leftHand)
	{
		if (GetState(a_victim) != State::kIncorporeal) {
			return true;
		}
		if (a_projectile ? ProjectileHurtsGhosts(a_projectile) : Detect::HandHasGhostWeapon(a_attacker, a_leftHand)) {
			return true;
		}
		Trace("melee", a_victim, a_attacker ? a_attacker->GetEquippedObject(a_leftHand) : nullptr);
		Feedback::PassThrough(a_victim);
		return false;
	}

	bool ProjectileHits(RE::Projectile* a_projectile, RE::TESObjectREFR* a_target)
	{
		auto* const victim = AsActor(a_target);
		if (!victim || !a_projectile || GetState(victim) != State::kIncorporeal || ProjectileHurtsGhosts(a_projectile)) {
			return true;
		}
		Trace("projectile", victim, a_projectile->GetBaseObject());
		Feedback::PassThrough(victim);
		return false;
	}

	bool HitLands(RE::Actor* a_victim, const RE::HitData& a_hit)
	{
		if (GetState(a_victim) != State::kIncorporeal) {
			return true;
		}
		if (const auto source = a_hit.sourceRef.get(); source) {
			if (auto* const projectile = source->As<RE::Projectile>(); projectile) {
				if (ProjectileHurtsGhosts(projectile)) {
					return true;
				}
				Trace("hit", a_victim, projectile->GetBaseObject());
				Feedback::PassThrough(a_victim);
				return false;
			}
		}
		const auto attacker = a_hit.aggressor.get();
		if (a_hit.weapon && Detect::IsGhostWeaponOf(attacker.get(), a_hit.weapon)) {
			return true;
		}
		// an arrow / bolt hit without its projectile: the shooter's ammo
		if (a_hit.weapon && attacker && (a_hit.weapon->IsBow() || a_hit.weapon->IsCrossbow()) && Detect::IsGhostAmmo(attacker->GetCurrentAmmo())) {
			return true;
		}
		Trace("hit", a_victim, a_hit.weapon);
		Feedback::PassThrough(a_victim);
		return false;
	}

	bool EffectApplies(RE::Actor* a_target, const RE::MagicTarget::AddTargetData& a_data)
	{
		const auto* effect = a_data.effect ? a_data.effect->baseEffect : nullptr;
		if (!effect || (!effect->IsHostile() && !effect->IsDetrimental()) || a_data.caster == a_target) {
			return true;
		}
		if (GetState(a_target) != State::kIncorporeal) {
			return true;
		}
		// enchantments and poisons riding on a ghost weapon or ghost ammo
		if (const auto* item = a_data.magicItem; item) {
			const auto type = item->GetSpellType();
			if (type == SpellType::kEnchantment || type == SpellType::kPoison) {
				if (const auto* source = a_data.source; source) {
					if (const auto* weapon = source->As<RE::TESObjectWEAP>(); weapon && Detect::IsGhostWeaponOf(AsActor(a_data.caster), weapon)) {
						return true;
					}
					if (Detect::IsGhostAmmo(source->As<RE::TESAmmo>())) {
						return true;
					}
				}
				if (type == SpellType::kEnchantment && Detect::IsGhostEnchantment(item->As<RE::EnchantmentItem>())) {
					return true;
				}
			}
		}
		if (EffectAllowed(effect)) {
			return true;
		}
		if (Settings::Get().debugLog) {
			logs::info("{} ({}) has no effect on {} [{:08X}]", Forms::Name(effect), a_data.magicItem ? Forms::Name(a_data.magicItem) : "?",
				Forms::Name(a_target), a_target->GetFormID());
		}
		return false;
	}
}
