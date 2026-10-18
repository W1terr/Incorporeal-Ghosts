#include "Detect.h"

#include "Forms.h"
#include "Poison.h"
#include "Settings.h"

namespace Detect
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr auto kCacheTime = 3s;

		template <class T>
		using Set = std::unordered_set<const T*>;

		struct Data
		{
			std::vector<RE::BGSKeyword*> ghostKeywords;
			std::vector<RE::BGSKeyword*> excludeKeywords;
			std::vector<RE::BGSKeyword*> weaponKeywords;
			std::vector<RE::BGSKeyword*> silverKeywords;
			Set<RE::TESForm>             dawnbreakerWeapons;
			Set<RE::EnchantmentItem>     dawnbreakerEnchantments;
			std::vector<RE::BGSKeyword*> effectKeywords;
			std::vector<RE::BGSKeyword*> magicKeywords;
			Set<RE::TESRace>             ghostRaces;
			Set<RE::TESRace>             excludeRaces;
			Set<RE::TESNPC>              ghostActors;
			Set<RE::TESNPC>              excludeActors;
			Set<RE::SpellItem>           ghostSpells;
			Set<RE::TESForm>             weapons;  // weapons and ammo
			Set<RE::EnchantmentItem>     enchantments;
			Set<RE::EffectSetting>       enchantmentEffects;
			std::vector<RE::EffectSetting*> revealEffects;
			std::vector<RE::EffectSetting*> hazardEffects;
			Set<RE::TESForm>             hazards;
			Set<RE::TESEffectShader>     ghostShaders;
			Set<RE::EffectSetting>       ghostEffects;
		};
		Data data;

		struct Cached
		{
			Kind              kind;
			Clock::time_point time;
		};
		std::unordered_map<RE::FormID, Cached> cache;
		std::mutex                             cacheLock;

		template <class T>
		Set<T> ToSet(const std::vector<T*>& a_forms)
		{
			return { a_forms.begin(), a_forms.end() };
		}

		bool HasAny(const RE::BGSKeywordForm* a_form, const std::vector<RE::BGSKeyword*>& a_keywords)
		{
			return a_form && std::ranges::any_of(a_keywords, [&](const RE::BGSKeyword* a_kw) { return a_form->HasKeyword(a_kw); });
		}

		bool HasAny(const RE::EffectSetting* a_effect, const std::vector<RE::BGSKeyword*>& a_keywords)
		{
			return a_effect && HasAny(static_cast<const RE::BGSKeywordForm*>(a_effect), a_keywords);
		}

		bool HasGhostSpell(const RE::TESSpellList::SpellData* a_spells)
		{
			if (!a_spells || !a_spells->spells) {
				return false;
			}
			for (std::uint32_t i = 0; i < a_spells->numSpells; ++i) {
				if (data.ghostSpells.contains(a_spells->spells[i])) {
					return true;
				}
			}
			return false;
		}

		// the actor's base and the records it was made from (leveled actors get a generated base)
		template <class Fn>
		bool AnyBase(RE::Actor* a_actor, Fn&& a_fn)
		{
			auto* const base = a_actor->GetActorBase();
			if (base && a_fn(base)) {
				return true;
			}
			auto* const templ = a_actor->GetTemplateBase();
			if (templ && templ != base && a_fn(templ)) {
				return true;
			}
			auto* const face = base ? base->faceNPC : nullptr;
			return face && face != base && face != templ && a_fn(face);
		}

		Kind Compute(RE::Actor* a_actor)
		{
			auto* const base = a_actor->GetActorBase();
			auto* const race = a_actor->GetRace();
			if (AnyBase(a_actor, [](const RE::TESNPC* a_npc) { return data.ghostActors.contains(a_npc); })) {
				return Kind::kListed;
			}
			if (HasAny(base, data.ghostKeywords) || HasAny(race, data.ghostKeywords)) {
				return Kind::kKeyword;
			}
			if (race && data.ghostRaces.contains(race)) {
				return Kind::kRace;
			}
			if ((base && HasGhostSpell(base->actorEffects)) || (race && HasGhostSpell(race->actorEffects))) {
				return Kind::kAbility;
			}
			for (const auto* spell : a_actor->GetActorRuntimeData().addedSpells) {
				if (data.ghostSpells.contains(spell)) {
					return Kind::kAbility;
				}
			}
			return Kind::kNone;
		}

		// Ghost abilities: a constant scripted effect that is neither hostile nor detrimental and puts one of the ghost
		// effect shaders on the actor (vanilla GhostAbility, GhostAbilityNew, MGGhostAbility, the Soul Cairn and Nightingale
		// ghosts, and the copies mods make of them)
		void FindGhostSpells(const std::vector<RE::TESEffectShader*>& a_shaders)
		{
			data.ghostShaders = { a_shaders.begin(), a_shaders.end() };
			const auto&                    shaders = data.ghostShaders;
			auto&                          effects = data.ghostEffects;
			auto* const                    handler = RE::TESDataHandler::GetSingleton();
			for (const auto* effect : handler->GetFormArray<RE::EffectSetting>()) {
				if (!effect) {
					continue;
				}
				const auto& d = effect->data;
				if (d.archetype == RE::EffectArchetypes::ArchetypeID::kScript &&
					d.castingType == RE::MagicSystem::CastingType::kConstantEffect &&
					!effect->IsHostile() && !effect->IsDetrimental() &&
					(shaders.contains(d.effectShader) || shaders.contains(d.enchantShader))) {
					effects.insert(effect);
				}
			}
			for (const auto* spell : handler->GetFormArray<RE::SpellItem>()) {
				if (!spell) {
					continue;
				}
				for (const auto* effect : spell->effects) {
					if (effect && effects.contains(effect->baseEffect)) {
						data.ghostSpells.insert(spell);
						break;
					}
				}
			}
			logs::info("Ghost abilities: {} effects with ghost shaders, {} spells", effects.size(), data.ghostSpells.size());
			if (Settings::Get().debugLog) {
				for (const auto* spell : data.ghostSpells) {
					logs::info("  ghost ability {} [{:08X}]", Forms::Name(spell), spell->GetFormID());
				}
			}
		}

		bool EnchantmentByEffects(const RE::EnchantmentItem* a_enchantment)
		{
			for (const auto* effect : a_enchantment->effects) {
				const auto* base = effect ? effect->baseEffect : nullptr;
				if (!base) {
					continue;
				}
				if (data.enchantmentEffects.contains(base) || HasAny(base, data.effectKeywords) ||
					base->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kTurnUndead) {
					return true;
				}
			}
			return false;
		}

		bool HasActiveEffect(RE::Actor* a_actor, const std::vector<RE::EffectSetting*>& a_effects)
		{
			if (a_effects.empty()) {
				return false;
			}
			auto* const target = a_actor->AsMagicTarget();
			return target && std::ranges::any_of(a_effects, [&](RE::EffectSetting* a_effect) { return target->HasMagicEffect(a_effect); });
		}
	}

	void Init()
	{
		const auto& l = Settings::GetLists();
		const auto& own = Forms::Get();
		data = {};

		data.ghostKeywords = Forms::ResolveAll<RE::BGSKeyword>(l.ghostKeywords, "Ghost keyword");
		data.excludeKeywords = Forms::ResolveAll<RE::BGSKeyword>(l.excludeKeywords, "Excluded keyword");
		data.weaponKeywords = Forms::ResolveAll<RE::BGSKeyword>(l.weaponKeywords, "Ghost weapon keyword");
		data.effectKeywords = Forms::ResolveAll<RE::BGSKeyword>(l.enchantmentEffectKeywords, "Enchantment effect keyword");
		data.magicKeywords = Forms::ResolveAll<RE::BGSKeyword>(l.magicKeywords, "Allowed magic keyword");
		auto add = [](std::vector<RE::BGSKeyword*>& a_list, RE::BGSKeyword* a_kw) {
			if (a_kw && std::ranges::find(a_list, a_kw) == a_list.end()) {
				a_list.push_back(a_kw);
			}
		};
		add(data.ghostKeywords, own.incorporeal);
		add(data.excludeKeywords, own.notIncorporeal);
		add(data.weaponKeywords, own.ghostWeapon);

		data.ghostRaces = ToSet(Forms::ResolveAll<RE::TESRace>(l.ghostRaces, "Ghost race"));
		data.excludeRaces = ToSet(Forms::ResolveAll<RE::TESRace>(l.excludeRaces, "Excluded race"));
		data.ghostActors = ToSet(Forms::ResolveAll<RE::TESNPC>(l.ghostActors, "Ghost actor"));
		data.excludeActors = ToSet(Forms::ResolveAll<RE::TESNPC>(l.excludeActors, "Excluded actor"));
		data.ghostSpells = ToSet(Forms::ResolveAll<RE::SpellItem>(l.ghostSpells, "Ghost spell"));
		FindGhostSpells(Forms::ResolveAll<RE::TESEffectShader>(l.ghostShaders, "Ghost ability shader"));

		for (auto* form : Forms::ResolveAll<RE::TESForm>(l.weapons, "Ghost weapon")) {
			if (form->Is(RE::FormType::Weapon, RE::FormType::Ammo)) {
				data.weapons.insert(form);
			} else {
				logs::warn("Ghost weapon {:08X} is not a weapon or ammo", form->GetFormID());
			}
		}
		data.enchantments = ToSet(Forms::ResolveAll<RE::EnchantmentItem>(l.enchantments, "Ghost enchantment"));

		// Dawnbreaker: the weapon itself, and the enchantment it has in this load order (mods like Zim's Immersive
		// Artifacts replace it, and give the same enchantment to their own versions of the sword)
		data.silverKeywords = Forms::ResolveAll<RE::BGSKeyword>(l.silverKeywords, "Silver keyword");
		for (auto* form : Forms::ResolveAll<RE::TESForm>(l.dawnbreaker, "Dawnbreaker")) {
			if (auto* weapon = form->As<RE::TESObjectWEAP>(); weapon) {
				data.dawnbreakerWeapons.insert(weapon);
				if (weapon->formEnchanting) {
					data.dawnbreakerEnchantments.insert(weapon->formEnchanting);
				}
			} else if (auto* enchantment = form->As<RE::EnchantmentItem>(); enchantment) {
				data.dawnbreakerEnchantments.insert(enchantment);
			}
		}
		for (const auto* enchantment : data.dawnbreakerEnchantments) {
			logs::info("Dawnbreaker enchantment: {} [{:08X}]", Forms::Name(enchantment), enchantment->GetFormID());
		}
		data.enchantmentEffects = ToSet(Forms::ResolveAll<RE::EffectSetting>(l.enchantmentEffects, "Ghost enchantment effect"));

		data.revealEffects = Forms::ResolveAll<RE::EffectSetting>(l.revealEffects, "Reveal effect");
		data.hazardEffects = Forms::ResolveAll<RE::EffectSetting>(l.hazardEffects, "Yrden hazard effect");
		for (auto* form : Forms::ResolveAll<RE::TESForm>(l.hazards, "Yrden hazard")) {
			data.hazards.insert(form);
		}

		logs::info("Ghosts: {} keywords, {} races, {} actors, {} abilities; excluded: {} keywords, {} races, {} actors",
			data.ghostKeywords.size(), data.ghostRaces.size(), data.ghostActors.size(), data.ghostSpells.size(),
			data.excludeKeywords.size(), data.excludeRaces.size(), data.excludeActors.size());
		logs::info("Ghost weapons: {} keywords, {} silver keywords, {} Dawnbreakers, {} weapons/ammo, {} enchantments, {} enchantment effects, {} effect keywords",
			data.weaponKeywords.size(), data.silverKeywords.size(), data.dawnbreakerWeapons.size(), data.weapons.size(),
			data.enchantments.size(), data.enchantmentEffects.size(), data.effectKeywords.size());
		logs::info("Yrden: {} hazards, {} hazard effects, {} reveal effects; magic always allowed with {} keywords",
			data.hazards.size(), data.hazardEffects.size(), data.revealEffects.size(), data.magicKeywords.size());

		std::scoped_lock lock(cacheLock);
		cache.clear();
	}

	Kind GhostKind(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return Kind::kNone;
		}
		const auto       now = Clock::now();
		std::scoped_lock lock(cacheLock);
		auto&            entry = cache[a_actor->GetFormID()];
		if (entry.time == Clock::time_point{} || now - entry.time > kCacheTime) {
			entry = { Compute(a_actor), now };
			if (cache.size() > 512) {
				std::erase_if(cache, [&](const auto& a_kv) { return now - a_kv.second.time > kCacheTime; });
			}
		}
		return entry.kind;
	}

	bool IsExcluded(RE::Actor* a_actor)
	{
		auto* const race = a_actor->GetRace();
		return HasAny(a_actor->GetActorBase(), data.excludeKeywords) || HasAny(race, data.excludeKeywords) ||
		       (race && data.excludeRaces.contains(race)) ||
		       AnyBase(a_actor, [](const RE::TESNPC* a_npc) { return data.excludeActors.contains(a_npc); });
	}

	const char* KindName(Kind a_kind)
	{
		switch (a_kind) {
		case Kind::kListed:
			return "listed";
		case Kind::kKeyword:
			return "keyword";
		case Kind::kRace:
			return "race";
		case Kind::kAbility:
			return "ghost ability";
		default:
			return "not a ghost";
		}
	}

	bool IsGhostEnchantment(const RE::EnchantmentItem* a_enchantment)
	{
		if (!a_enchantment) {
			return false;
		}
		if (data.enchantments.contains(a_enchantment)) {
			return true;
		}
		// enchantments made by the player are copies: their base enchantment is the one that was learned
		if (const auto* learned = a_enchantment->data.baseEnchantment; learned && data.enchantments.contains(learned)) {
			return true;
		}
		return Settings::Get().undeadBane && EnchantmentByEffects(a_enchantment);
	}

	bool IsGhostWeapon(const RE::TESObjectWEAP* a_weapon, const RE::EnchantmentItem* a_instanceEnchantment)
	{
		if (!a_weapon) {
			return false;
		}
		if (data.weapons.contains(a_weapon) || HasAny(a_weapon, data.weaponKeywords)) {
			return true;
		}
		const auto& s = Settings::Get();
		if (s.silverWeapons && HasAny(a_weapon, data.silverKeywords)) {
			return true;
		}
		if (s.dawnbreaker && (data.dawnbreakerWeapons.contains(a_weapon) || data.dawnbreakerEnchantments.contains(a_weapon->formEnchanting) ||
								 (a_instanceEnchantment && data.dawnbreakerEnchantments.contains(a_instanceEnchantment)))) {
			return true;
		}
		if (a_weapon->IsBound()) {
			// their enchantment carries the Mystic Binding perk's Turn Undead: decided by the setting alone
			return Settings::Get().boundWeapons;
		}
		return IsGhostEnchantment(a_instanceEnchantment) || IsGhostEnchantment(a_weapon->formEnchanting);
	}

	bool IsGhostAmmo(const RE::TESAmmo* a_ammo)
	{
		return a_ammo && (data.weapons.contains(a_ammo) || HasAny(a_ammo->AsKeywordForm(), data.weaponKeywords) ||
							 (Settings::Get().silverWeapons && HasAny(a_ammo->AsKeywordForm(), data.silverKeywords)));
	}

	bool HandHasGhostWeapon(RE::Actor* a_actor, bool a_leftHand)
	{
		if (!a_actor) {
			return false;
		}
		if (Poison::IsCoated(a_actor, a_leftHand)) {
			return true;  // Spectral Poison
		}
		auto* const object = a_actor->GetEquippedObject(a_leftHand);
		auto* const weapon = object ? object->As<RE::TESObjectWEAP>() : nullptr;
		if (!weapon) {
			return false;
		}
		auto* const entry = a_actor->GetEquippedEntryData(a_leftHand);
		return IsGhostWeapon(weapon, entry ? entry->GetEnchantment() : nullptr);
	}

	bool IsGhostWeaponOf(RE::Actor* a_owner, const RE::TESObjectWEAP* a_weapon)
	{
		if (a_owner && a_weapon) {
			for (const bool left : { false, true }) {
				if (a_owner->GetEquippedObject(left) == a_weapon) {
					return HandHasGhostWeapon(a_owner, left);
				}
			}
		}
		return IsGhostWeapon(a_weapon, nullptr);
	}

	bool IsAllowedMagic(const RE::EffectSetting* a_effect)
	{
		return HasAny(a_effect, data.magicKeywords);
	}

	bool HasRevealEffect(RE::Actor* a_actor) { return HasActiveEffect(a_actor, data.revealEffects); }

	bool HasHazardEffect(RE::Actor* a_actor) { return HasActiveEffect(a_actor, data.hazardEffects); }

	bool IsYrdenHazard(const RE::TESForm* a_base) { return a_base && data.hazards.contains(a_base); }

	bool HasYrdenHazards() { return !data.hazards.empty(); }

	bool IsGhostShader(const RE::TESEffectShader* a_shader) { return a_shader && data.ghostShaders.contains(a_shader); }

	bool IsGhostEffect(const RE::EffectSetting* a_effect) { return a_effect && data.ghostEffects.contains(a_effect); }
}
