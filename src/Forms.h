#pragma once

namespace Forms
{
	inline constexpr auto kPlugin = "IncorporealGhosts.esp";
	inline constexpr auto kWitcherSigns = "W3S.esl";

	// IncorporealGhosts.esp, keep in sync with tools/build_esp.py
	inline constexpr RE::FormID kGhostWeapon = 0x800;
	inline constexpr RE::FormID kIncorporeal = 0x801;
	inline constexpr RE::FormID kNotIncorporeal = 0x802;
	inline constexpr RE::FormID kPassThroughSound = 0x803;

	struct Own
	{
		RE::BGSKeyword*            ghostWeapon{ nullptr };
		RE::BGSKeyword*            incorporeal{ nullptr };
		RE::BGSKeyword*            notIncorporeal{ nullptr };
		RE::BGSSoundDescriptorForm* passThrough{ nullptr };
	};

	void       Load();
	const Own& Get();
	bool       WitcherSignsLoaded();

	// "Plugin.esp|0x123456" (local form id) or an editor id
	RE::TESForm* Resolve(std::string_view a_spec);

	template <class T>
	std::vector<T*> ResolveAll(const std::vector<std::string>& a_specs, std::string_view a_what)
	{
		std::vector<T*> out;
		for (const auto& spec : a_specs) {
			auto* const form = Resolve(spec);
			auto* const typed = form ? form->As<T>() : nullptr;
			if (typed) {
				if (std::ranges::find(out, typed) == out.end()) {
					out.push_back(typed);
				}
			} else if (form) {
				logs::warn("{} '{}' is a {}, not the right kind of form", a_what, spec, RE::FormTypeToString(form->GetFormType()));
			} else {
				logs::info("{} '{}' not found (its plugin isn't loaded?)", a_what, spec);
			}
		}
		return out;
	}

	// the name the player sees, else the editor id, else the form id
	std::string Name(const RE::TESForm* a_form);
}
