#include "Hooks.h"

#include "Feedback.h"
#include "Ghosts.h"
#include "Visuals.h"
#include "Yrden.h"

// Where the engine already handles ghosts: actors with the "ghost" flag can't be hit at all, and three functions check
// it before anything happens. We wrap the same three functions and add our own rule (a ghost outside Yrden, hit by
// something that isn't a ghost weapon), so an attack passes through exactly like it does through a flagged ghost:
//
//   MeleeHit(attacker, victim, projectile, leftHand)    SE 37673 / AE 38627 - a swing reaching an actor
//   ShouldCollide(ref, collidable, projectile, base)    SE 43011 / AE 44202 - may a projectile collide with this ref
//   ProcessHit(victim, HitData&)                        SE 37633 / AE 38586 - every weapon hit, the last stop
//
// Each one is hooked at its entry: its first instructions move into a stub and the entry jumps to us, so every caller
// passes through, also combat mods that call them through a pointer or already redirected the vanilla calls (Precision,
// Valhalla Combat...). The entry bytes are checked first (the same in SE and AE); if they aren't what we expect
// (another mod hooked the entry), every "call rel32" that lands on the function is hooked instead, found in the running
// game, so no offsets are hardcoded either way.
// Magic: Character's MagicTarget::AddTarget (vtable), which adds every spell / enchantment / shout effect to an actor.
namespace Hooks
{
	namespace
	{
		// every "call rel32" in the game's code, by the function it calls (one pass for all hooked functions)
		std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> FindCalls(const std::vector<std::uintptr_t>& a_targets)
		{
			std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> calls;
			const auto        text = REL::Module::get().segment(REL::Segment::textx);
			const auto*       begin = reinterpret_cast<const std::uint8_t*>(text.address());
			const std::size_t size = text.size();
			for (std::size_t i = 0; i + 5 <= size; ++i) {
				if (begin[i] != 0xE8) {
					continue;
				}
				std::int32_t rel;
				std::memcpy(&rel, begin + i + 1, sizeof(rel));
				const auto site = reinterpret_cast<std::uintptr_t>(begin + i);
				const auto target = site + 5 + static_cast<std::intptr_t>(rel);
				if (std::ranges::find(a_targets, target) != a_targets.end()) {
					calls[target].push_back(site);
				}
			}
			return calls;
		}

		std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> calls;

		// hooks every call to a function; returns how many
		template <class Hook>
		std::size_t HookCalls(REL::RelocationID a_id, std::string_view a_name, std::size_t a_expected)
		{
			const auto  target = a_id.address();
			const auto& sites = calls[target];
			if (sites.empty() || sites.size() > a_expected * 2 + 4) {
				logs::error("{}: found {} calls (expected {}), not hooked", a_name, sites.size(), a_expected);
				return 0;
			}
			auto& trampoline = SKSE::GetTrampoline();
			for (const auto site : sites) {
				Hook::func = trampoline.write_call<5>(site, Hook::thunk);
			}
			const auto base = REL::Module::get().base();
			std::string where;
			for (const auto site : sites) {
				where += std::format(" {:X}", site - base);
			}
			logs::info("{}: {} calls hooked (expected {}):{}", a_name, sites.size(), a_expected, where);
			return sites.size();
		}

		void WriteJump(std::uint8_t*& a_at, std::uintptr_t a_to)
		{
			constexpr std::uint8_t jmp[]{ 0xFF, 0x25, 0, 0, 0, 0 };  // jmp [rip+0]
			std::memcpy(a_at, jmp, sizeof(jmp));
			std::memcpy(a_at + sizeof(jmp), &a_to, sizeof(a_to));
			a_at += sizeof(jmp) + sizeof(a_to);
		}

		// Entry hook for a function starting with position independent instructions: they are copied to the stub as
		// they are. Returns the stub (calls the original function) or 0.
		std::uintptr_t DetourPlain(std::uintptr_t a_entry, std::span<const std::uint8_t> a_prologue, const void* a_thunk)
		{
			if (std::memcmp(reinterpret_cast<const void*>(a_entry), a_prologue.data(), a_prologue.size()) != 0) {
				return 0;
			}
			auto& trampoline = SKSE::GetTrampoline();
			auto* const stub = static_cast<std::uint8_t*>(trampoline.allocate(a_prologue.size() + 14));
			auto*       at = stub;
			std::memcpy(at, a_prologue.data(), a_prologue.size());
			at += a_prologue.size();
			WriteJump(at, a_entry + a_prologue.size());
			trampoline.write_branch<5>(a_entry, reinterpret_cast<std::uintptr_t>(a_thunk));
			return reinterpret_cast<std::uintptr_t>(stub);
		}

		// MeleeHit starts with "test rdx, rdx / je <end>": the je is rebuilt in the stub with an absolute jump
		std::uintptr_t DetourMelee(std::uintptr_t a_entry, const void* a_thunk)
		{
			constexpr std::uint8_t head[]{ 0x48, 0x85, 0xD2, 0x0F, 0x84 };  // test rdx, rdx / je rel32
			const auto*            code = reinterpret_cast<const std::uint8_t*>(a_entry);
			if (std::memcmp(code, head, sizeof(head)) != 0) {
				return 0;
			}
			std::int32_t rel;
			std::memcpy(&rel, code + 5, sizeof(rel));
			const auto end = a_entry + 9 + static_cast<std::intptr_t>(rel);

			auto& trampoline = SKSE::GetTrampoline();
			auto* const stub = static_cast<std::uint8_t*>(trampoline.allocate(3 + 2 + 14 + 14));
			auto*       at = stub;
			constexpr std::uint8_t test[]{ 0x48, 0x85, 0xD2, 0x75, 14 };  // test rdx, rdx / jne over the next jump
			std::memcpy(at, test, sizeof(test));
			at += sizeof(test);
			WriteJump(at, end);
			WriteJump(at, a_entry + 9);
			trampoline.write_branch<5>(a_entry, reinterpret_cast<std::uintptr_t>(a_thunk));
			return reinterpret_cast<std::uintptr_t>(stub);
		}

		struct MeleeHit
		{
			static void thunk(RE::Actor* a_attacker, RE::Actor* a_victim, RE::Projectile* a_projectile, bool a_leftHand)
			{
				if (!a_victim || Ghosts::MeleeHits(a_attacker, a_victim, a_projectile, a_leftHand)) {
					func(a_attacker, a_victim, a_projectile, a_leftHand);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ShouldCollide
		{
			static bool thunk(RE::TESObjectREFR* a_ref, void* a_collidable, RE::Projectile* a_projectile, void* a_base)
			{
				if (a_ref && a_projectile && !Ghosts::ProjectileHits(a_projectile, a_ref)) {
					return false;
				}
				return func(a_ref, a_collidable, a_projectile, a_base);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ProcessHit
		{
			static void thunk(RE::Actor* a_victim, RE::HitData& a_hit)
			{
				if (!a_victim || Ghosts::HitLands(a_victim, a_hit)) {
					func(a_victim, a_hit);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct AddTarget
		{
			static bool thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
			{
				auto* const actor = a_this ? a_this->GetTargetAsActor() : nullptr;
				if (actor && !Ghosts::EffectApplies(actor, a_data)) {
					return false;
				}
				return func(a_this, a_data);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class Hook>
		void InstallHook(REL::RelocationID a_id, std::string_view a_name, std::size_t a_expected, std::uintptr_t a_stub)
		{
			if (a_stub) {
				Hook::func = REL::Relocation<decltype(Hook::thunk)>{ a_stub };
				logs::info("{}: entry hooked", a_name);
				return;
			}
			logs::warn("{}: its entry isn't what was expected (another mod hooked it?), hooking its calls instead", a_name);
			if (calls.empty()) {
				const REL::RelocationID ids[]{ RELOCATION_ID(37673, 38627), RELOCATION_ID(43011, 44202), RELOCATION_ID(37633, 38586) };
				calls = FindCalls({ ids[0].address(), ids[1].address(), ids[2].address() });
			}
			HookCalls<Hook>(a_id, a_name, a_expected);
		}

		// once a frame, on the main thread, while the game runs
		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				Yrden::Update(a_delta);
				Feedback::Update();
				Visuals::Update(a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void Install()
	{
		const REL::RelocationID melee = RELOCATION_ID(37673, 38627);
		const REL::RelocationID collide = RELOCATION_ID(43011, 44202);
		const REL::RelocationID hit = RELOCATION_ID(37633, 38586);
		constexpr std::uint8_t  collideStart[]{ 0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30 };  // push rsi, rdi, r14 / sub rsp, 30h
		constexpr std::uint8_t  hitStart[]{ 0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x54 };          // mov rax, rsp / push rbp, rsi, rdi, r12

		InstallHook<MeleeHit>(melee, "MeleeHit", 3, DetourMelee(melee.address(), reinterpret_cast<const void*>(&MeleeHit::thunk)));
		InstallHook<ShouldCollide>(collide, "Projectile ShouldCollide", 6, DetourPlain(collide.address(), collideStart, reinterpret_cast<const void*>(&ShouldCollide::thunk)));
		InstallHook<ProcessHit>(hit, "ProcessHit", 8, DetourPlain(hit.address(), hitStart, reinterpret_cast<const void*>(&ProcessHit::thunk)));
		calls.clear();

		REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[4] };  // the MagicTarget part
		AddTarget::func = character.write_vfunc(0x1, AddTarget::thunk);

		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = player.write_vfunc(0xAD, PlayerUpdate::thunk);

		logs::info("Hooks installed");
	}
}
