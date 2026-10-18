#include "Yrden.h"

#include "Detect.h"
#include "Settings.h"

namespace Yrden
{
	namespace
	{
		constexpr float kInterval = 0.2f;        // seconds between looks for signs
		constexpr float kSearchRadius = 8192.0f;  // around the player
		constexpr float kUnitsPerRadius = 22.0f;  // hazard radius -> game units (engine constant)

		std::vector<Zone> zones;
		std::shared_mutex zoneLock;
		float             timer{ 0.0f };

		float RadiusOf(RE::Hazard* a_hazard)
		{
			float radius = a_hazard->GetHazardRuntimeData().radius;
			if (radius <= 0.0f) {
				const auto* base = a_hazard->GetBaseObject();
				const auto* hazard = base ? base->As<RE::BGSHazard>() : nullptr;
				radius = hazard ? hazard->data.radius : 0.0f;
			}
			return radius * kUnitsPerRadius * Settings::Get().yrdenRadius;
		}

		// the sign only matters with a ghost around: no cell scan otherwise
		bool GhostsAround()
		{
			auto* const lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return false;
			}
			for (const auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (actor && !actor->IsDead() && Detect::GhostKind(actor.get()) != Detect::Kind::kNone) {
					return true;
				}
			}
			return false;
		}

		std::vector<Zone> Find()
		{
			std::vector<Zone> found;
			auto* const       player = RE::PlayerCharacter::GetSingleton();
			auto* const       tes = RE::TES::GetSingleton();
			if (!player || !tes || !player->GetParentCell()) {
				return found;
			}
			tes->ForEachReferenceInRange(player, kSearchRadius, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref->GetFormType() == RE::FormType::PlacedHazard && !a_ref->IsDisabled() &&
					!a_ref->IsMarkedForDeletion() && !a_ref->IsDeleted() && Detect::IsYrdenHazard(a_ref->GetBaseObject())) {
					const float radius = RadiusOf(static_cast<RE::Hazard*>(a_ref));
					if (radius > 0.0f) {
						found.push_back({ a_ref->GetPosition(), radius, a_ref->GetFormID() });
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return found;
		}
	}

	void Update(float a_delta)
	{
		timer -= a_delta;
		if (timer > 0.0f) {
			return;
		}
		timer = kInterval;
		auto found = Detect::HasYrdenHazards() && GhostsAround() ? Find() : std::vector<Zone>{};

		std::unique_lock lock(zoneLock);
		if (Settings::Get().debugLog && found.size() != zones.size()) {
			logs::info("Yrden signs active: {} -> {}", zones.size(), found.size());
			for (const auto& z : found) {
				logs::info("  sign {:08X} at ({:.0f}, {:.0f}, {:.0f}), radius {:.0f}", z.ref, z.center.x, z.center.y, z.center.z, z.radius);
			}
		}
		zones = std::move(found);
	}

	void Reset()
	{
		std::unique_lock lock(zoneLock);
		zones.clear();
		timer = 0.0f;
	}

	bool Contains(const RE::NiPoint3& a_position)
	{
		const auto&       s = Settings::Get();
		std::shared_lock lock(zoneLock);
		for (const auto& z : zones) {
			const float dz = a_position.z - z.center.z;
			if (dz > s.yrdenAbove || dz < -s.yrdenBelow) {
				continue;
			}
			const float dx = a_position.x - z.center.x;
			const float dy = a_position.y - z.center.y;
			if (dx * dx + dy * dy <= z.radius * z.radius) {
				return true;
			}
		}
		return false;
	}

	bool Any()
	{
		std::shared_lock lock(zoneLock);
		return !zones.empty();
	}

	std::vector<Zone> Zones()
	{
		std::shared_lock lock(zoneLock);
		return zones;
	}
}
