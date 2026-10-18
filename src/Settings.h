#pragma once

namespace Settings
{
	// how much magic incorporeal ghosts shrug off
	enum class MagicImmunity : int
	{
		kNone,         // spells and shouts work as in vanilla (only weapons pass through)
		kDamage,       // effects that damage, drain or stagger (default); fear, paralysis, turn undead... still work
		kAllHostile,   // every hostile effect
	};

	struct Values
	{
		bool          enabled{ true };
		bool          onlyHostile{ true };       // ghosts that are friendly to the player stay solid (quest ghosts, Kodlak...)
		bool          affectFollowers{ false };  // ghost followers / player teammates
		bool          affectSummons{ false };    // ghosts summoned by the player or a follower (Karstaag...)
		MagicImmunity magic{ MagicImmunity::kDamage };
		bool          undeadBane{ true };        // weapons with Turn Undead / undead bane enchantments are ghost weapons
		bool          boundWeapons{ true };      // conjured weapons (Bound Sword, Bow, Battleaxe, Dagger...) are ghost weapons
		bool          sound{ true };             // the spectral whoosh when an attack passes through
		bool          materialize{ true };       // ghosts inside Yrden lose their ghost shader and transparency
		float         fadeTime{ 0.4f };          // seconds the change of form takes
		float         yrdenRadius{ 1.0f };       // multiplier on Yrden's radius
		float         yrdenAbove{ 400.0f };      // a ghost this far above the sign's center still counts (Wispmothers float)
		float         yrdenBelow{ 150.0f };
		bool          debugLog{ false };
		std::string   language{ "auto" };        // menu / message language: "auto" = the game's, or english, french...
	};

	// Form lists. Entries are "Plugin.esp|0x123456" (local form id) or an editor id (keywords and races always work,
	// other forms only when their editor id is kept in memory, e.g. with powerofthree's Tweaks).
	struct Lists
	{
		// what makes an actor a ghost (any one is enough)
		std::vector<std::string> ghostKeywords;   // on the actor or its race
		std::vector<std::string> ghostRaces;
		std::vector<std::string> ghostShaders;    // constant scripted effects (abilities) with these effect shaders
		std::vector<std::string> ghostSpells;     // abilities / spells in the actor's spell list
		std::vector<std::string> ghostActors;     // base actors (NPC records)
		// what keeps an actor solid even if it looks like a ghost
		std::vector<std::string> excludeKeywords;
		std::vector<std::string> excludeRaces;
		std::vector<std::string> excludeActors;
		// ghost weapons
		std::vector<std::string> weaponKeywords;  // on the weapon or the ammo
		std::vector<std::string> weapons;         // weapons and ammo
		std::vector<std::string> enchantments;
		std::vector<std::string> enchantmentEffects;
		std::vector<std::string> enchantmentEffectKeywords;
		// Yrden
		std::vector<std::string> hazards;         // placed hazards whose radius makes ghosts vulnerable
		std::vector<std::string> hazardEffects;   // the effect the hazard gives actors inside (fallback detection)
		std::vector<std::string> revealEffects;   // any active effect from this list makes a ghost vulnerable
		// magic that always affects ghosts
		std::vector<std::string> magicKeywords;
		// shown on a materialized ghost that had no ghost shader to take off (Wispmothers: their look is in the mesh)
		std::vector<std::string> revealShaders;
	};

	void Load();
	void Save();  // the [General] values back into the ini (comments and lists are kept)

	Values&      Get();
	const Lists& GetLists();
	std::mutex&  Lock();  // around changes from the menu
}
