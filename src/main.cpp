#include "Detect.h"
#include "Forms.h"
#include "Hooks.h"
#include "Lang.h"
#include "Menu.h"
#include "Poison.h"
#include "Settings.h"
#include "Visuals.h"
#include "Yrden.h"

namespace
{
	constexpr std::uint32_t kSaveID = 'IGHO';

	bool warned = false;

	void OnSave(SKSE::SerializationInterface* a_intfc) { Visuals::Save(a_intfc); }

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		std::uint32_t type, version, length;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type == 'SOLD') {
				Visuals::Load(a_intfc, version, length);
			}
		}
	}

	void OnRevert(SKSE::SerializationInterface*) { Visuals::Revert(); }

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Forms::Load();
			Detect::Init();
			Visuals::Init();
			Lang::Set(Settings::Get().language);
			Poison::Init();
			Menu::Register();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Yrden::Reset();
			Poison::Reset();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Yrden::Reset();
			Poison::Reset();
			if (!warned && !Forms::WitcherSignsLoaded()) {
				warned = true;
				RE::SendHUDMessage::ShowHUDMessage(Lang::T("Incorporeal Ghosts: The Witcher Signs is missing, only ghost weapons can hurt ghosts."));
			}
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 1 << 10 });
	logs::info("Incorporeal Ghosts loading");
	Settings::Load();
	Hooks::Install();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	if (auto* serialization = SKSE::GetSerializationInterface(); serialization) {
		serialization->SetUniqueID(kSaveID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}
	return true;
}
