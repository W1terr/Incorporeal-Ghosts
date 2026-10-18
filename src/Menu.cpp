// SKSE Menu Framework 3 API (https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API, LGPL-2.1).
// It pulls in windows.h, so only this file includes it.
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)
#undef GetObject
#undef PlaySound

#include "Menu.h"

#include "Detect.h"
#include "Forms.h"
#include "Ghosts.h"
#include "Lang.h"
#include "Poison.h"
#include "Settings.h"
#include "Yrden.h"

namespace Menu
{
	namespace
	{
		using namespace ImGuiMCP;
		using Lang::F;
		using Lang::T;

		constexpr auto  kSection = "Incorporeal Ghosts";
		constexpr float kNearby = 4096.0f;  // the status page lists ghosts this close

		const ImVec4 kGood{ 0.55f, 0.85f, 0.55f, 1.0f };
		const ImVec4 kGhostly{ 0.6f, 0.75f, 1.0f, 1.0f };
		const ImVec4 kWarn{ 1.0f, 0.65f, 0.35f, 1.0f };

		// translated label with a fixed ImGui id (it must not change with the language)
		std::string Id(const char* a_english)
		{
			return std::format("{}###{}", T(a_english), a_english);
		}

		void Hint(const char* a_english)
		{
			SameLine();
			TextDisabled("(?)");
			if (IsItemHovered()) {
				SetTooltip("%s", T(a_english));
			}
		}

		void Heading(const char* a_english)
		{
			SeparatorText(T(a_english));
		}

		void __stdcall OnFrameworkEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::EventType::kCloseMenu) {
				std::scoped_lock lock(Settings::Lock());
				Settings::Save();
			}
		}

		void __stdcall RenderSettings()
		{
			std::scoped_lock lock(Settings::Lock());
			auto&            s = Settings::Get();

			Heading("Ghosts");
			Checkbox(Id("Ghosts and Wispmothers are incorporeal").c_str(), &s.enabled);
			Hint("Weapons pass through them and damaging magic does nothing, unless the weapon is a ghost weapon or the ghost stands in Yrden.");
			Checkbox(Id("Only ghosts hostile to you").c_str(), &s.onlyHostile);
			Hint("Friendly and quest ghosts (Kodlak, the Greybeards' phantom...) stay solid until they turn on you.");
			Checkbox(Id("Ghost followers too").c_str(), &s.affectFollowers);
			Checkbox(Id("Your summoned ghosts too").c_str(), &s.affectSummons);
			Hint("Ghosts you or your followers summon (Karstaag...). Ghosts your enemies summon are always incorporeal.");

			Heading("Magic");
			const char* magicNames[] = { T("Works as in vanilla"), T("Damage, drain and stagger do nothing"), T("No hostile magic works") };
			int         magic = static_cast<int>(s.magic);
			if (Combo(Id("Magic against incorporeal ghosts").c_str(), &magic, magicNames, 3)) {
				s.magic = static_cast<Settings::MagicImmunity>(magic);
			}
			Hint("Spells and shouts that would have no effect fly straight through. Fear, paralysis, Turn Undead and similar still work with the default setting. Inside Yrden everything works.");

			Heading("Ghost weapons");
			TextWrapped("%s", T("Ghostblade always hurts ghosts."));
			Checkbox(Id("Silver weapons count").c_str(), &s.silverWeapons);
			Hint("Every weapon and ammo made of silver (WeapMaterialSilver): Silver Sword, Silver Greatsword, Bloodskal Blade, modded silver weapons.");
			Checkbox(Id("Dawnbreaker counts").c_str(), &s.dawnbreaker);
			Hint("Also when a mod changes its enchantment or adds its own versions of it with the same enchantment (Zim's Immersive Artifacts...).");
			Checkbox(Id("Undead bane enchantments count").c_str(), &s.undeadBane);
			Hint("Weapons enchanted with Turn Undead (Blessed / Sanctified / Holy...), Red Eagle's Bane, Auriel's Bow.");
			Checkbox(Id("Bound weapons count").c_str(), &s.boundWeapons);
			Hint("Bound Sword, Bow, Battleaxe, Dagger and every other conjured weapon, yours and your followers'.");

			Heading("Yrden");
			SliderFloat(Id("Radius").c_str(), &s.yrdenRadius, 0.5f, 2.0f, "x %.2f");
			Hint("Multiplier on the sign's circle (The Witcher Signs: 319 units).");
			SliderFloat(Id("Counts above the sign").c_str(), &s.yrdenAbove, 0.0f, 1000.0f, "%.0f");
			Hint("Wispmothers float: how high above the sign's center (in game units) a ghost still counts as inside.");
			Checkbox(Id("Ghosts take physical form inside Yrden").c_str(), &s.materialize);
			Hint("Their ghost shader comes off and they fade back to solid; when they leave the sign they turn ghostly again.");
			SliderFloat(Id("Change of form").c_str(), &s.fadeTime, 0.0f, 2.0f, "%.1f s");

			Heading("Feedback");
			Checkbox(Id("Spectral sound when an attack passes through").c_str(), &s.sound);
			Checkbox(Id("Log details (SKSE log)").c_str(), &s.debugLog);
		}

		void Verdict(bool a_ghost, const char* a_yes)
		{
			SameLine();
			if (a_ghost) {
				TextColored(kGood, "%s", T(a_yes));
			} else {
				TextDisabled("%s", T("ordinary"));
			}
		}

		void WeaponLine(const char* a_hand, RE::Actor* a_player, bool a_left)
		{
			auto* const object = a_player->GetEquippedObject(a_left);
			if (!object || !object->Is(RE::FormType::Weapon)) {
				TextDisabled("%s: -", T(a_hand));
				return;
			}
			Text("%s: %s", T(a_hand), Forms::Name(object).c_str());
			Verdict(Detect::HandHasGhostWeapon(a_player, a_left), "ghost weapon");
			if (const float left = Poison::Remaining(a_left); left > 0.0f) {
				SameLine();
				TextColored(kGhostly, "%s", F("Spectral Poison: {:.0f} s", left).c_str());
			}
		}

		void __stdcall RenderStatus()
		{
			auto* const player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Is3DLoaded()) {
				TextDisabled("%s", T("Load a game to see the status."));
				return;
			}

			Heading("The Witcher Signs");
			if (Forms::WitcherSignsLoaded()) {
				TextColored(kGood, "%s", T("W3S.esl is loaded"));
			} else {
				TextColored(kWarn, "%s", T("W3S.esl is not loaded: only ghost weapons hurt ghosts"));
			}
			const auto zones = Yrden::Zones();
			if (zones.empty()) {
				TextDisabled("%s", T("No Yrden sign is active near you"));
			}
			for (const auto& z : zones) {
				const float distance = player->GetPosition().GetDistance(z.center);
				auto        line = F("Yrden {:.0f} units away, radius {:.0f}", distance, z.radius);
				if (distance <= z.radius) {
					line = std::format("{} ({})", line, T("you are inside"));
				}
				BulletText("%s", line.c_str());
			}

			Heading("Your weapons");
			WeaponLine("Right hand", player, false);
			WeaponLine("Left hand", player, true);
			if (auto* const ammo = player->GetCurrentAmmo(); ammo) {
				Text("%s: %s", T("Ammo"), Forms::Name(ammo).c_str());
				Verdict(Detect::IsGhostAmmo(ammo), "ghost ammo");
			}

			Heading("Ghosts near you");
			int         shown = 0;
			auto* const lists = RE::ProcessLists::GetSingleton();
			if (lists) {
				for (const auto& handle : lists->highActorHandles) {
					const auto actor = handle.get();
					if (!actor || actor->IsDead() || actor->GetPosition().GetDistance(player->GetPosition()) > kNearby) {
						continue;
					}
					const auto kind = Detect::GhostKind(actor.get());
					if (kind == Detect::Kind::kNone) {
						continue;
					}
					const auto state = Ghosts::GetState(actor.get());
					Text("%s", Forms::Name(actor.get()).c_str());
					SameLine();
					TextDisabled("[%08X] (%s)", actor->GetFormID(), T(Detect::KindName(kind)));
					SameLine();
					TextColored(state == Ghosts::State::kIncorporeal                                        ? kGhostly :
								state == Ghosts::State::kInYrden || state == Ghosts::State::kRevealed ? kGood :
																										 kWarn,
						"%s", T(Ghosts::StateName(state)));
					++shown;
				}
			}
			if (shown == 0) {
				TextDisabled("%s", T("None"));
			}
		}

		void __stdcall RenderLanguages();

		// the sidebar pages: English name (the translation key), page, name it has in the framework now
		struct Page
		{
			const char*                              english;
			SKSEMenuFramework::Model::RenderFunction render;
			std::string                              shown;
		};
		std::array pages{
			Page{ "Settings", RenderSettings, {} },
			Page{ "Status", RenderStatus, {} },
			Page{ "Language", RenderLanguages, {} },
		};

		// exported by SKSE Menu Framework 3.8+, not in its header: renames a page at runtime
		using RenameSectionFunction = bool (*)(const char* a_path, const char* a_newName);

		RenameSectionFunction RenameSection()
		{
			static const auto func = SKSEMenuFramework::Model::Internal::GetFunction<RenameSectionFunction>("RenameSection");
			return func;
		}

		void RenamePages()
		{
			if (!RenameSection()) {
				return;
			}
			for (auto& page : pages) {
				const std::string wanted = T(page.english);
				if (wanted != page.shown && RenameSection()(std::format("{}/{}", kSection, page.shown).c_str(), wanted.c_str())) {
					page.shown = wanted;
				}
			}
		}

		void __stdcall RenderLanguages()
		{
			std::scoped_lock lock(Settings::Lock());
			auto&            s = Settings::Get();

			Heading("Language");
			bool changed = false;
			if (RadioButton(Id("Same as the game").c_str(), s.language == "auto")) {
				s.language = "auto";
				changed = true;
			}
			for (const auto& language : Lang::All()) {
				if (RadioButton(language.name, s.language == language.id)) {
					s.language = language.id;
					changed = true;
				}
			}
			if (changed) {
				Lang::Set(s.language);
				Poison::ApplyNames();
				RenamePages();
			}
			Spacing();
			if (!RenameSection()) {
				TextWrapped("%s", T("The names of the pages on the left change after you restart the game."));
			}
			// without the right glyphs the framework draws "?" for every letter, so this is also shown in English
			if (const auto glyphs = Lang::Current().glyphs) {
				constexpr auto kGlyphHint = "If you see ? instead of letters: in SKSE Menu Framework open Options > Open Settings and set Character Glyphs to {}.";
				Spacing();
				TextWrapped("%s", F(kGlyphHint, glyphs).c_str());
				TextDisabled("%s", std::vformat(kGlyphHint, std::make_format_args(glyphs)).c_str());
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			logs::info("SKSE Menu Framework is not installed: settings only through the ini");
			return;
		}
		SKSEMenuFramework::SetSection(kSection);
		for (auto& page : pages) {
			page.shown = T(page.english);
			SKSEMenuFramework::AddSectionItem(page.shown, page.render);
		}
		SKSEMenuFramework::AddEvent(OnFrameworkEvent, 0.0f);
		logs::info("SKSE Menu Framework {} found, pages added", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
