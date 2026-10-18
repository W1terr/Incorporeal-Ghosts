#pragma once

// The Witcher Signs' Yrden is a placed hazard (_W3S_YrdenHazard) at the caster's feet. Its circle is the hazard's
// radius: the record's value (14.5) times 22 game units, the same as the engine uses to pick the hazard's targets.
namespace Yrden
{
	struct Zone
	{
		RE::NiPoint3 center;
		float        radius;
		RE::FormID   ref;
	};

	void Update(float a_delta);  // main thread: finds the active signs a few times a second
	void Reset();                // a game is being loaded

	bool              Contains(const RE::NiPoint3& a_position);  // any thread
	bool              Any();
	std::vector<Zone> Zones();
}
