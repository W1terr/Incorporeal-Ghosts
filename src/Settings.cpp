#include "Settings.h"

namespace Settings
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/IncorporealGhosts.ini";
		constexpr auto kPatchDir = "Data/SKSE/Plugins/IncorporealGhosts";  // compatibility files: their lists are added

		Values     values;
		Lists      lists;
		std::mutex lock;

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		struct Entry
		{
			std::string key;  // "section.key", lower case
			std::string value;
		};

		std::vector<Entry> Read(const std::filesystem::path& a_path)
		{
			std::vector<Entry> entries;
			std::ifstream      file(a_path);
			std::string        line;
			std::string        section;
			while (std::getline(file, line)) {
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#') {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = Lower(Trim(std::string_view(line).substr(1, line.size() - 2)));
					continue;
				}
				const auto equals = line.find('=');
				if (equals == std::string::npos) {
					continue;
				}
				std::string value = Trim(std::string_view(line).substr(equals + 1));
				if (const auto comment = value.find(';'); comment != std::string::npos) {
					value = Trim(std::string_view(value).substr(0, comment));
				}
				entries.push_back({ section + "." + Lower(Trim(std::string_view(line).substr(0, equals))), std::move(value) });
			}
			return entries;
		}

		void Bool(const Entry& a_entry, const char* a_key, bool& a_value)
		{
			if (a_entry.key == a_key) {
				const auto v = Lower(a_entry.value);
				a_value = v == "1" || v == "true" || v == "yes" || v == "on";
			}
		}

		void Number(const Entry& a_entry, const char* a_key, float& a_value, float a_min, float a_max)
		{
			if (a_entry.key == a_key) {
				try {
					a_value = std::clamp(std::stof(a_entry.value), a_min, a_max);
				} catch (...) {
					logs::warn("{}: '{}' is not a number", a_key, a_entry.value);
				}
			}
		}

		void Magic(const Entry& a_entry, const char* a_key, MagicImmunity& a_value)
		{
			float number = static_cast<float>(a_value);
			Number(a_entry, a_key, number, 0.0f, 2.0f);
			a_value = static_cast<MagicImmunity>(static_cast<int>(number));
		}

		// comma separated, the same key again adds to the list
		void List(const Entry& a_entry, const char* a_key, std::vector<std::string>& a_list)
		{
			if (a_entry.key != a_key) {
				return;
			}
			std::string_view rest = a_entry.value;
			while (!rest.empty()) {
				const auto comma = rest.find(',');
				auto       item = Trim(rest.substr(0, comma));
				if (!item.empty() && std::ranges::find(a_list, item) == a_list.end()) {
					a_list.push_back(std::move(item));
				}
				if (comma == std::string_view::npos) {
					break;
				}
				rest.remove_prefix(comma + 1);
			}
		}

		void Apply(const std::vector<Entry>& a_entries, bool a_general)
		{
			for (const auto& e : a_entries) {
				if (a_general) {
					Bool(e, "general.benabled", values.enabled);
					Bool(e, "general.bonlyhostile", values.onlyHostile);
					Bool(e, "general.baffectfollowers", values.affectFollowers);
					Bool(e, "general.baffectsummons", values.affectSummons);
					Magic(e, "general.imagicimmunity", values.magic);
					Bool(e, "general.bsilverweapons", values.silverWeapons);
					Bool(e, "general.bdawnbreaker", values.dawnbreaker);
					Bool(e, "general.bundeadbane", values.undeadBane);
					Bool(e, "general.bboundweapons", values.boundWeapons);
					Bool(e, "general.bpassthroughsound", values.sound);
					Bool(e, "general.bmaterialize", values.materialize);
					Number(e, "general.ffadetime", values.fadeTime, 0.0f, 3.0f);
					Number(e, "general.fyrdenradius", values.yrdenRadius, 0.25f, 4.0f);
					Number(e, "general.fyrdenabove", values.yrdenAbove, 0.0f, 2000.0f);
					Number(e, "general.fyrdenbelow", values.yrdenBelow, 0.0f, 2000.0f);
					Bool(e, "general.bdebuglog", values.debugLog);
					Number(e, "general.fpoisonduration", values.poisonDuration, 1.0f, 600.0f);
					float vendorPoisons = static_cast<float>(values.vendorPoisons);
					Number(e, "general.ivendorpoisons", vendorPoisons, 0.0f, 20.0f);
					values.vendorPoisons = static_cast<int>(vendorPoisons);
					Bool(e, "general.bvendorrecipe", values.vendorRecipe);
					if (e.key == "general.slanguage" && !e.value.empty()) {
						values.language = Lower(e.value);
					}
				}
				List(e, "ghosts.keywords", lists.ghostKeywords);
				List(e, "ghosts.races", lists.ghostRaces);
				List(e, "ghosts.abilityshaders", lists.ghostShaders);
				List(e, "ghosts.spells", lists.ghostSpells);
				List(e, "ghosts.actors", lists.ghostActors);
				List(e, "exclude.keywords", lists.excludeKeywords);
				List(e, "exclude.races", lists.excludeRaces);
				List(e, "exclude.actors", lists.excludeActors);
				List(e, "ghostweapons.keywords", lists.weaponKeywords);
				List(e, "ghostweapons.silverkeywords", lists.silverKeywords);
				List(e, "ghostweapons.dawnbreaker", lists.dawnbreaker);
				List(e, "ghostweapons.weapons", lists.weapons);
				List(e, "ghostweapons.enchantments", lists.enchantments);
				List(e, "ghostweapons.enchantmenteffects", lists.enchantmentEffects);
				List(e, "ghostweapons.enchantmenteffectkeywords", lists.enchantmentEffectKeywords);
				List(e, "yrden.hazards", lists.hazards);
				List(e, "yrden.hazardeffects", lists.hazardEffects);
				List(e, "yrden.revealeffects", lists.revealEffects);
				List(e, "magic.allowedkeywords", lists.magicKeywords);
				List(e, "visuals.revealshader", lists.revealShaders);
				List(e, "poison.ingredients", lists.poisonIngredients);
				List(e, "poison.weaponshader", lists.poisonShader);
			}
		}

		std::string Format(float a_value)
		{
			auto text = std::format("{:.2f}", a_value);
			while (text.back() == '0') {
				text.pop_back();
			}
			if (text.back() == '.') {
				text.pop_back();
			}
			return text;
		}

		std::vector<std::pair<std::string, std::string>> GeneralValues()
		{
			return {
				{ "bEnabled", values.enabled ? "1" : "0" },
				{ "bOnlyHostile", values.onlyHostile ? "1" : "0" },
				{ "bAffectFollowers", values.affectFollowers ? "1" : "0" },
				{ "bAffectSummons", values.affectSummons ? "1" : "0" },
				{ "iMagicImmunity", std::to_string(static_cast<int>(values.magic)) },
				{ "bSilverWeapons", values.silverWeapons ? "1" : "0" },
				{ "bDawnbreaker", values.dawnbreaker ? "1" : "0" },
				{ "bUndeadBane", values.undeadBane ? "1" : "0" },
				{ "bBoundWeapons", values.boundWeapons ? "1" : "0" },
				{ "bPassThroughSound", values.sound ? "1" : "0" },
				{ "bMaterialize", values.materialize ? "1" : "0" },
				{ "fFadeTime", Format(values.fadeTime) },
				{ "fYrdenRadius", Format(values.yrdenRadius) },
				{ "fYrdenAbove", Format(values.yrdenAbove) },
				{ "fYrdenBelow", Format(values.yrdenBelow) },
				{ "bDebugLog", values.debugLog ? "1" : "0" },
				{ "sLanguage", values.language },
				{ "fPoisonDuration", Format(values.poisonDuration) },
				{ "iVendorPoisons", std::to_string(values.vendorPoisons) },
				{ "bVendorRecipe", values.vendorRecipe ? "1" : "0" },
			};
		}
	}

	void Load()
	{
		values = {};
		lists = {};
		if (!std::filesystem::exists(kPath)) {
			logs::warn("{} is missing: no ghosts, ghost weapons or Yrden signs are defined", kPath);
		}
		Apply(Read(kPath), true);

		std::error_code ec;
		std::vector<std::filesystem::path> patches;
		for (const auto& file : std::filesystem::directory_iterator(kPatchDir, ec)) {
			if (file.is_regular_file() && Lower(file.path().extension().string()) == ".ini") {
				patches.push_back(file.path());
			}
		}
		std::ranges::sort(patches);
		for (const auto& patch : patches) {
			Apply(Read(patch), false);
			logs::info("Compatibility file {} added", patch.filename().string());
		}

		logs::info("Settings: enabled {}, only hostile {}, followers {}, summons {}, magic immunity {}, undead bane {}, bound weapons {}, sound {}, Yrden radius x{}",
			values.enabled, values.onlyHostile, values.affectFollowers, values.affectSummons, static_cast<int>(values.magic),
			values.undeadBane, values.boundWeapons, values.sound, values.yrdenRadius);
	}

	void Save()
	{
		std::vector<std::string> lines;
		{
			std::ifstream file(kPath);
			std::string   line;
			while (std::getline(file, line)) {
				lines.push_back(line);
			}
		}

		auto       pending = GeneralValues();
		std::string section;
		std::size_t generalEnd = std::string::npos;  // where missing keys go
		for (std::size_t i = 0; i < lines.size(); ++i) {
			const auto trimmed = Trim(lines[i]);
			if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
				section = Lower(Trim(std::string_view(trimmed).substr(1, trimmed.size() - 2)));
				if (section == "general") {
					generalEnd = i + 1;
				}
				continue;
			}
			if (section != "general" || trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
				continue;
			}
			generalEnd = i + 1;
			const auto equals = trimmed.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			const auto key = Lower(Trim(std::string_view(trimmed).substr(0, equals)));
			const auto it = std::ranges::find_if(pending, [&](const auto& a_kv) { return Lower(a_kv.first) == key; });
			if (it == pending.end()) {
				continue;
			}
			std::string comment;
			if (const auto semicolon = trimmed.find(';', equals); semicolon != std::string::npos) {
				comment = "  " + trimmed.substr(semicolon);
			}
			lines[i] = std::format("{} = {}{}", it->first, it->second, comment);
			pending.erase(it);
		}

		if (generalEnd == std::string::npos) {
			lines.insert(lines.begin(), { "[General]", "" });
			generalEnd = 1;
		}
		for (const auto& [key, value] : pending) {
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(generalEnd++), std::format("{} = {}", key, value));
		}

		std::ofstream file(kPath, std::ios::trunc);
		for (const auto& line : lines) {
			file << line << '\n';
		}
		if (!file) {
			logs::error("Couldn't write {}", kPath);
		}
	}

	Values& Get() { return values; }

	const Lists& GetLists() { return lists; }

	std::mutex& Lock() { return lock; }
}
