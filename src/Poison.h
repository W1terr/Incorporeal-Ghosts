#pragma once

// Spectral Poison: coats the player's weapon for a fixed time (12 s) so it hurts incorporeal ghosts.
//
// The vanilla poison system only carries it: when the player applies it, the poison is taken off the weapon again
// right away (no charges, nothing for poison perks to change, its effect never reaches anyone) and the weapon gets our
// own spectral coating with its own timer instead. The coating belongs to that weapon (its inventory entry) in either
// hand, also bows and their arrows; it keeps counting down while the weapon is put away and ends on loading a game.
//
// Crafting: with the recipe read, Ectoplasm + Salt Pile + Deathbell (the same number of each, nothing else) at an
// alchemy lab make Spectral Poison instead of the vanilla poison those ingredients give. Apothecaries (every vendor whose
// buy / sell list has VendorItemPoison) sell the poison and the recipe.
namespace Poison
{
	void Init();                 // data loaded: vendors, names, event sinks
	void Update(float a_delta);  // main thread, every frame
	void Reset();                // a game is loaded / started
	void ApplyNames();           // item names in the menu language

	bool  IsCoated(RE::Actor* a_actor, bool a_leftHand);  // any thread
	bool  IsCoatingEffect(const RE::EffectSetting* a_effect);
	float Remaining(bool a_leftHand);                     // seconds, 0 = not coated (for the menu)
}
