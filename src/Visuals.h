#pragma once

// A ghost inside Yrden takes physical form: its ghost effect shader is taken off and its transparency fades back to
// solid. When it steps out, both come back. Ghosts without a ghost shader (Wispmothers: their look is in the mesh) get
// the reveal shader instead while they are solid.
namespace Visuals
{
	void Init();                 // data loaded
	void Update(float a_delta);  // main thread, every frame

	// co-save: a ghost saved while solid gets its transparency back when the game is loaded
	void Save(SKSE::SerializationInterface* a_intfc);
	void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
	void Revert();
}
