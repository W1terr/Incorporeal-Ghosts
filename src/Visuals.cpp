#include "Visuals.h"

#include "Detect.h"
#include "Forms.h"
#include "Ghosts.h"
#include "Settings.h"

namespace Visuals
{
	namespace
	{
		constexpr float kInterval = 0.1f;        // seconds between looks at the ghosts around
		constexpr float kRange = 6000.0f;        // ghosts this close to the player change form
		constexpr float kDefaultAlpha = 0.25f;   // vanilla GhostAbility (magicSetActorAlphaScript AlphaValue)

		struct Ghost
		{
			RE::ActorHandle                                      handle;
			float                                                ghostAlpha{ 1.0f };  // its alpha as a ghost
			float                                                alpha{ 1.0f };       // shown now
			bool                                                 solid{ false };
			bool                                                 changed{ false };    // alpha not at its target yet
			std::vector<RE::TESEffectShader*>                    shaders;             // ghost shaders taken off
			std::vector<RE::NiPointer<RE::ShaderReferenceEffect>> ours;               // shaders we put on
		};

		std::unordered_map<RE::FormID, Ghost> ghosts;
		std::vector<std::pair<RE::FormID, float>> restore;  // from the co-save: alpha to give back once loaded
		std::vector<RE::TESEffectShader*>         revealShaders;
		float                                     timer{ 0.0f };

		// the AlphaValue of the ghost ability's magicSetActorAlphaScript (copies made by mods keep it)
		float ReadGhostAlpha(RE::Actor* a_actor)
		{
			auto* const target = a_actor->AsMagicTarget();
			auto* const effects = target ? target->GetActiveEffectList() : nullptr;
			auto* const vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!effects || !vm) {
				return 1.0f;
			}
			auto* const policy = vm->GetObjectHandlePolicy();
			bool        ghostEffect = false;
			for (auto* effect : *effects) {
				if (!effect || !Detect::IsGhostEffect(effect->GetBaseObject())) {
					continue;
				}
				ghostEffect = true;
				const auto                                 handle = policy ? policy->GetHandleForObject(RE::ActiveEffect::VMTYPEID, effect) : 0;
				RE::BSTSmartPointer<RE::BSScript::Object> object;
				if (handle && vm->FindBoundObject(handle, "magicSetActorAlphaScript", object) && object) {
					if (const auto* value = object->GetProperty("AlphaValue"); value) {
						return std::clamp(value->GetFloat(), 0.0f, 1.0f);
					}
				}
			}
			// a ghost ability whose alpha couldn't be read: vanilla's; no ghost ability (Wispmothers): not see-through
			return ghostEffect ? kDefaultAlpha : 1.0f;
		}

		bool IsOn(const RE::ShaderReferenceEffect* a_effect, RE::Actor* a_actor)
		{
			return a_effect && !a_effect->finished && a_effect->target.get().get() == a_actor;
		}

		// takes the ghost shaders off; returns which ones were on
		std::vector<RE::TESEffectShader*> StopGhostShaders(RE::Actor* a_actor)
		{
			std::vector<RE::TESEffectShader*> stopped;
			if (auto* const lists = RE::ProcessLists::GetSingleton(); lists) {
				lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
					if (IsOn(a_effect, a_actor) && Detect::IsGhostShader(a_effect->effectData)) {
						a_effect->finished = true;
						if (std::ranges::find(stopped, a_effect->effectData) == stopped.end()) {
							stopped.push_back(a_effect->effectData);
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return stopped;
		}

		// the shader's effect on the actor (still in the game's list, so alive), or null
		RE::ShaderReferenceEffect* FindShader(RE::Actor* a_actor, const RE::TESEffectShader* a_shader)
		{
			RE::ShaderReferenceEffect* found = nullptr;
			if (auto* const lists = RE::ProcessLists::GetSingleton(); lists) {
				lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
					if (IsOn(a_effect, a_actor) && a_effect->effectData == a_shader) {
						found = a_effect;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return found;
		}

		void StopOurs(Ghost& a_ghost)
		{
			for (auto& effect : a_ghost.ours) {
				if (effect) {
					effect->finished = true;
				}
			}
			a_ghost.ours.clear();
		}

		void Apply(RE::Actor* a_actor, Ghost& a_ghost, RE::TESEffectShader* a_shader)
		{
			if (!a_shader || FindShader(a_actor, a_shader)) {
				return;
			}
			// the engine function doesn't return the effect it made (CommonLib says it does): find it in the list
			a_actor->ApplyEffectShader(a_shader, -1.0f);
			if (auto* const effect = FindShader(a_actor, a_shader); effect) {
				a_ghost.ours.emplace_back(effect);
			}
		}

		void Materialize(RE::Actor* a_actor, Ghost& a_ghost)
		{
			if (a_ghost.shaders.empty() && a_ghost.ours.empty() && !a_ghost.changed) {
				a_ghost.ghostAlpha = ReadGhostAlpha(a_actor);
				a_ghost.alpha = a_ghost.ghostAlpha;  // fades from there
			}
			StopOurs(a_ghost);
			for (auto* shader : StopGhostShaders(a_actor)) {
				if (std::ranges::find(a_ghost.shaders, shader) == a_ghost.shaders.end()) {
					a_ghost.shaders.push_back(shader);
				}
			}
			// nothing to take off (Wispmothers): show that the sign holds it
			if (a_ghost.shaders.empty()) {
				for (auto* shader : revealShaders) {
					Apply(a_actor, a_ghost, shader);
				}
			}
			a_ghost.solid = true;
			a_ghost.changed = true;
			if (Settings::Get().debugLog) {
				logs::info("{} [{:08X}] takes physical form ({} ghost shaders off, ghost alpha {:.2f})", Forms::Name(a_actor),
					a_actor->GetFormID(), a_ghost.shaders.size(), a_ghost.ghostAlpha);
			}
		}

		void Dematerialize(RE::Actor* a_actor, Ghost& a_ghost)
		{
			StopOurs(a_ghost);
			for (auto* shader : a_ghost.shaders) {
				Apply(a_actor, a_ghost, shader);
			}
			a_ghost.solid = false;
			a_ghost.changed = true;
			if (Settings::Get().debugLog) {
				logs::info("{} [{:08X}] is a ghost again", Forms::Name(a_actor), a_actor->GetFormID());
			}
		}

		void Scan()
		{
			auto* const player = RE::PlayerCharacter::GetSingleton();
			auto* const lists = RE::ProcessLists::GetSingleton();
			if (!player || !lists) {
				return;
			}
			const bool on = Settings::Get().materialize;
			const auto center = player->GetPosition();

			std::unordered_set<RE::FormID> seen;
			for (const auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || !actor->Is3DLoaded() || actor->GetPosition().GetDistance(center) > kRange) {
					continue;
				}
				const auto id = actor->GetFormID();
				auto       it = ghosts.find(id);
				const bool solid = on && Ghosts::IsMaterialized(actor.get());
				if (it == ghosts.end()) {
					if (!solid) {
						continue;
					}
					it = ghosts.emplace(id, Ghost{ .handle = actor->GetHandle() }).first;
				}
				seen.insert(id);
				auto& ghost = it->second;
				if (actor->IsDead()) {
					continue;  // below
				}
				if (solid && !ghost.solid) {
					Materialize(actor.get(), ghost);
				} else if (!solid && ghost.solid) {
					Dematerialize(actor.get(), ghost);
				} else if (solid) {
					StopGhostShaders(actor.get());  // came back with its 3D
				}
			}

			for (auto it = ghosts.begin(); it != ghosts.end();) {
				auto&      ghost = it->second;
				const auto actor = ghost.handle.get();
				// dead or gone: it stays as it died, only our shaders go (the ability's go with the ability)
				if (!actor || actor->IsDead()) {
					StopOurs(ghost);
					it = ghosts.erase(it);
					continue;
				}
				if (!seen.contains(it->first)) {
					if (!actor->Is3DLoaded()) {
						// unloaded: the ability puts its shader back with the 3D, only the alpha is ours to restore
						if (ghost.solid || ghost.changed) {
							actor->SetAlpha(ghost.ghostAlpha);
						}
						StopOurs(ghost);
						it = ghosts.erase(it);
						continue;
					}
					if (ghost.solid) {
						Dematerialize(actor.get(), ghost);  // walked out of range while solid
					}
				}
				// a ghost again and faded in: only kept while it wears shaders we put back
				if (!ghost.solid && !ghost.changed && ghost.ours.empty()) {
					it = ghosts.erase(it);
				} else {
					++it;
				}
			}
		}

		void Fade(float a_delta)
		{
			const float time = Settings::Get().fadeTime;
			const float step = time > 0.0f ? a_delta / time : 1.0f;
			for (auto& [id, ghost] : ghosts) {
				if (!ghost.changed) {
					continue;
				}
				const auto actor = ghost.handle.get();
				if (!actor) {
					ghost.changed = false;
					continue;
				}
				const float target = ghost.solid ? 1.0f : ghost.ghostAlpha;
				ghost.alpha = ghost.alpha < target ? std::min(target, ghost.alpha + step) : std::max(target, ghost.alpha - step);
				actor->SetAlpha(ghost.alpha);
				ghost.changed = ghost.alpha != target;
			}
		}

		void RestoreSaved()
		{
			for (auto it = restore.begin(); it != restore.end();) {
				auto* const actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
				if (!actor) {
					it = restore.erase(it);
					continue;
				}
				if (!actor->Is3DLoaded()) {
					++it;
					continue;
				}
				if (!actor->IsDead() && !ghosts.contains(it->first)) {
					actor->SetAlpha(it->second);
				}
				it = restore.erase(it);
			}
		}
	}

	void Init()
	{
		revealShaders = Forms::ResolveAll<RE::TESEffectShader>(Settings::GetLists().revealShaders, "Reveal shader");
	}

	void Update(float a_delta)
	{
		if (!restore.empty()) {
			RestoreSaved();
		}
		timer -= a_delta;
		if (timer <= 0.0f) {
			timer = kInterval;
			Scan();
		}
		Fade(a_delta);
	}

	void Save(SKSE::SerializationInterface* a_intfc)
	{
		std::vector<std::pair<RE::FormID, float>> solid;
		for (const auto& [id, ghost] : ghosts) {
			if (ghost.solid || ghost.changed) {
				solid.emplace_back(id, ghost.ghostAlpha);
			}
		}
		for (const auto& entry : restore) {
			solid.push_back(entry);
		}
		if (!a_intfc->OpenRecord('SOLD', 1)) {
			return;
		}
		const auto count = static_cast<std::uint32_t>(solid.size());
		a_intfc->WriteRecordData(count);
		for (const auto& [id, alpha] : solid) {
			a_intfc->WriteRecordData(id);
			a_intfc->WriteRecordData(alpha);
		}
	}

	void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t, std::uint32_t)
	{
		std::uint32_t count = 0;
		if (!a_intfc->ReadRecordData(count)) {
			return;
		}
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::FormID id = 0;
			float      alpha = 1.0f;
			if (!a_intfc->ReadRecordData(id) || !a_intfc->ReadRecordData(alpha)) {
				break;
			}
			if (RE::FormID resolved = 0; a_intfc->ResolveFormID(id, resolved)) {
				restore.emplace_back(resolved, alpha);
			}
		}
		logs::info("{} ghosts were saved in physical form, they get their transparency back", restore.size());
	}

	void Revert()
	{
		ghosts.clear();
		restore.clear();
		timer = 0.0f;
	}
}
