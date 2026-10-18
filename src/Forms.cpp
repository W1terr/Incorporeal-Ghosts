#include "Forms.h"

namespace Forms
{
	namespace
	{
		Own  own;
		bool witcherSigns{ false };
	}

	void Load()
	{
		auto* const data = RE::TESDataHandler::GetSingleton();
		auto        find = [&]<class T>(RE::FormID a_id, T*& a_out) {
            auto* const form = data->LookupForm(a_id, kPlugin);
            a_out = form ? form->As<T>() : nullptr;
		};
		find(kGhostWeapon, own.ghostWeapon);
		find(kIncorporeal, own.incorporeal);
		find(kNotIncorporeal, own.notIncorporeal);
		find(kPassThroughSound, own.passThrough);
		if (!data->LookupModByName(kPlugin)) {
			logs::warn("{} is not loaded: no pass-through sound, the IG_ keywords don't exist", kPlugin);
		} else {
			logs::info("{}: keywords {}/{}/{}, sound {}", kPlugin, own.ghostWeapon != nullptr, own.incorporeal != nullptr,
				own.notIncorporeal != nullptr, own.passThrough != nullptr);
		}
		witcherSigns = data->LookupModByName(kWitcherSigns) != nullptr;
		if (!witcherSigns) {
			logs::warn("The Witcher Signs ({}) is not loaded: only ghost weapons can hurt ghosts", kWitcherSigns);
		}
	}

	const Own& Get() { return own; }

	bool WitcherSignsLoaded() { return witcherSigns; }

	RE::TESForm* Resolve(std::string_view a_spec)
	{
		const auto bar = a_spec.find('|');
		if (bar == std::string_view::npos) {
			return RE::TESForm::LookupByEditorID(a_spec);
		}
		const std::string plugin(a_spec.substr(0, bar));
		const std::string id(a_spec.substr(bar + 1));
		RE::FormID        local = 0;
		try {
			local = static_cast<RE::FormID>(std::stoul(id, nullptr, 16));
		} catch (...) {
			logs::warn("'{}': '{}' is not a hex form id", a_spec, id);
			return nullptr;
		}
		auto* const data = RE::TESDataHandler::GetSingleton();
		return data->LookupModByName(plugin) ? data->LookupForm(local & 0xFFFFFF, plugin) : nullptr;
	}

	std::string Name(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "none";
		}
		if (const char* name = a_form->GetName(); name && *name) {
			return name;
		}
		if (const char* edid = a_form->GetFormEditorID(); edid && *edid) {
			return edid;
		}
		return std::format("{:08X}", a_form->GetFormID());
	}
}
