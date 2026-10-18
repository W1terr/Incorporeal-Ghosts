#include "Poison.h"

#include "Forms.h"
#include "Lang.h"
#include "Settings.h"

namespace Poison
{
	namespace
	{
		using Lang::F;
		using Lang::T;

		constexpr float kScanTime = 2.0f;  // seconds the weapons are watched after the poison left the inventory

		struct Coat
		{
			RE::TESObjectWEAP* weapon;
			RE::ExtraDataList* xList;      // the coated item among copies of the same weapon
			float              remaining;  // seconds
		};

		std::mutex        lock;  // coats are read from the hit hooks
		std::vector<Coat> coats;

		std::atomic<bool> coated{ false };  // any coat at all: the hit hooks skip the lock without one

		float                                       pendingScan{ 0.0f };
		std::array<bool, 2>                         glowing{};  // right, left
		std::vector<RE::TESBoundObject*>            ingredients;
		RE::TESEffectShader*                        weaponShader{ nullptr };
		std::map<RE::TESBoundObject*, std::int32_t> snapshot;  // ingredient counts before the next brew

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		bool Contains(const RE::InventoryEntryData* a_entry, const RE::ExtraDataList* a_xList)
		{
			if (!a_entry || !a_entry->extraLists) {
				return false;
			}
			for (const auto* xList : *a_entry->extraLists) {
				if (xList == a_xList) {
					return true;
				}
			}
			return false;
		}

		// the coat on whatever the player holds in that hand
		const Coat* CoatIn(bool a_leftHand)
		{
			auto* const player = Player();
			auto* const object = player ? player->GetEquippedObject(a_leftHand) : nullptr;
			if (!object) {
				return nullptr;
			}
			const auto* entry = player->GetEquippedEntryData(a_leftHand);
			for (const auto& coat : coats) {
				if (coat.weapon == object && (!entry || !entry->extraLists || Contains(entry, coat.xList))) {
					return &coat;
				}
			}
			return nullptr;
		}

		// ---------------------------------------------------------------------------------------- applying

		// the player poisoned a weapon with it: take the vanilla poison off and coat the weapon
		void TakeOffPoison()
		{
			auto* const player = Player();
			auto* const poison = Forms::Get().poison;
			auto* const changes = player && poison ? player->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return;
			}
			for (auto* entry : *changes->entryList) {
				auto* const weapon = entry && entry->object ? entry->object->As<RE::TESObjectWEAP>() : nullptr;
				if (!weapon || !entry->extraLists) {
					continue;
				}
				for (auto* xList : *entry->extraLists) {
					const auto* extra = xList ? xList->GetByType<RE::ExtraPoison>() : nullptr;
					if (!extra || extra->poison != poison) {
						continue;
					}
					xList->RemoveByType(RE::ExtraDataType::kPoison);
					const float duration = Settings::Get().poisonDuration;
					{
						std::scoped_lock guard(lock);
						std::erase_if(coats, [&](const Coat& a_coat) { return a_coat.xList == xList; });
						coats.push_back({ weapon, xList, duration });
						coated = true;
					}
					RE::SendHUDMessage::ShowHUDMessage(F("{} can strike ghosts for {:.0f} seconds.", Forms::Name(weapon), duration).c_str());
					logs::info("Spectral Poison on {} for {:.0f} s", Forms::Name(weapon), duration);
				}
			}
		}

		// ---------------------------------------------------------------------------------------- the glow

		void StopGlow()
		{
			auto* const lists = RE::ProcessLists::GetSingleton();
			auto* const player = Player();
			if (!lists || !player || !weaponShader) {
				return;
			}
			lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
				if (a_effect && !a_effect->finished && a_effect->effectData == weaponShader && a_effect->target.get().get() == player) {
					a_effect->finished = true;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		// the hand's weapon node; a drawn bow is held in the left hand (SHIELD), its arrow in the right (WEAPON)
		void Glow(bool a_leftHand, const RE::TESObjectWEAP* a_weapon)
		{
			auto* const player = Player();
			const bool  ranged = a_weapon && (a_weapon->IsBow() || a_weapon->IsCrossbow());
			for (const auto name : { "WEAPON"sv, "SHIELD"sv }) {
				if (!ranged && (name == "SHIELD"sv) != a_leftHand) {
					continue;
				}
				for (const bool firstPerson : { false, true }) {
					auto* const root = player->Get3D(firstPerson);
					if (auto* const node = root ? root->GetObjectByName(name) : nullptr; node) {
						player->ApplyEffectShader(weaponShader, -1.0f, nullptr, false, false, node);
					}
				}
			}
		}

		// the weapon glows while it is coated and drawn (sheathed it sits on another node)
		void UpdateGlow()
		{
			auto* const player = Player();
			if (!weaponShader || !player || !player->Is3DLoaded()) {
				return;
			}
			const auto* state = player->AsActorState();
			const bool  drawn = state && state->IsWeaponDrawn();
			std::array<bool, 2>                      want{};
			std::array<const RE::TESObjectWEAP*, 2> weapons{};
			{
				std::scoped_lock guard(lock);
				for (const bool left : { false, true }) {
					const auto* coat = drawn ? CoatIn(left) : nullptr;
					want[left] = coat != nullptr;
					weapons[left] = coat ? coat->weapon : nullptr;
				}
			}
			if (want == glowing) {
				return;
			}
			StopGlow();
			for (const bool left : { false, true }) {
				if (want[left]) {
					Glow(left, weapons[left]);
				}
			}
			glowing = want;
		}

		// ---------------------------------------------------------------------------------------- crafting

		std::map<RE::TESBoundObject*, std::int32_t> IngredientCounts()
		{
			auto* const player = Player();
			if (!player) {
				return {};
			}
			return player->GetInventoryCounts([](RE::TESBoundObject& a_object) { return a_object.Is(RE::FormType::Ingredient); });
		}

		// after a brew: if it used exactly our ingredients (the same number of each), its poison becomes Spectral Poison
		void CheckBrew(RE::AlchemyItem* a_brewed)
		{
			auto counts = IngredientCounts();
			std::map<RE::TESBoundObject*, std::int32_t> used;
			for (const auto& [object, before] : snapshot) {
				const auto it = counts.find(object);
				const auto after = it != counts.end() ? it->second : 0;
				if (before > after) {
					used[object] = before - after;
				}
			}
			snapshot = std::move(counts);

			auto* const recipe = Forms::Get().recipe;
			auto* const poison = Forms::Get().poison;
			if (used.empty() || !a_brewed || a_brewed == poison || !a_brewed->IsPoison() || !poison || !recipe || !recipe->IsRead()) {
				return;
			}
			if (used.size() != ingredients.size()) {
				return;
			}
			const auto n = used.begin()->second;
			for (auto* ingredient : ingredients) {
				const auto it = used.find(ingredient);
				if (it == used.end() || it->second != n) {
					return;
				}
			}
			auto* const player = Player();
			const auto  have = player->GetInventoryCounts([&](RE::TESBoundObject& a_object) { return &a_object == a_brewed; });
			const auto  brewed = have.empty() ? 0 : std::min(have.begin()->second, n);
			if (brewed <= 0) {
				return;
			}
			player->RemoveItem(a_brewed, brewed, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			player->AddObjectToContainer(poison, nullptr, brewed, nullptr);
			RE::SendHUDMessage::ShowHUDMessage(F("{} brewed.", Forms::Name(poison)).c_str());
			logs::info("Brewed {} Spectral Poison", brewed);
		}

		bool CraftingOpen()
		{
			auto* const ui = RE::UI::GetSingleton();
			return ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME);
		}

		void QueueBrewCheck(RE::FormID a_brewed)
		{
			SKSE::GetTaskInterface()->AddTask([a_brewed]() {
				auto* const form = RE::TESForm::LookupByID(a_brewed);
				CheckBrew(form ? form->As<RE::AlchemyItem>() : nullptr);
			});
		}

		class Events :
			public RE::BSTEventSink<RE::TESContainerChangedEvent>,
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
			public RE::BSTEventSink<RE::ItemCrafted::Event>
		{
		public:
			static Events* Get()
			{
				static Events sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				auto* const poison = Forms::Get().poison;
				if (!a_event || !poison) {
					return RE::BSEventNotifyControl::kContinue;
				}
				constexpr RE::FormID kPlayer = 0x14;
				if (a_event->baseObj == poison->GetFormID()) {
					// used from the inventory: it goes on a weapon in a moment
					if (a_event->oldContainer == kPlayer && a_event->newContainer == 0) {
						pendingScan = kScanTime;
					}
				} else if (a_event->oldContainer == 0 && a_event->newContainer == kPlayer && CraftingOpen()) {
					QueueBrewCheck(a_event->baseObj);  // something brewed at the alchemy lab
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->opening && a_event->menuName == RE::CraftingMenu::MENU_NAME) {
					SKSE::GetTaskInterface()->AddTask([]() { snapshot = IngredientCounts(); });
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::ItemCrafted::Event* a_event, RE::BSTEventSource<RE::ItemCrafted::Event>*) override
			{
				if (a_event && a_event->item && a_event->item->Is(RE::FormType::AlchemyItem)) {
					QueueBrewCheck(a_event->item->GetFormID());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// ---------------------------------------------------------------------------------------- vendors

		// every vendor that trades in poisons (the apothecaries) sells the poison and the recipe
		void StockVendors()
		{
			const auto& s = Settings::Get();
			auto* const poison = Forms::Get().poison;
			auto* const recipe = Forms::Get().recipe;
			auto* const keyword = RE::TESForm::LookupByID(0x08CDED);  // VendorItemPoison
			if (!poison || !recipe || !keyword || (s.vendorPoisons <= 0 && !s.vendorRecipe)) {
				return;
			}
			std::unordered_set<RE::TESObjectCONT*> stocked;
			for (auto* faction : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFaction>()) {
				const auto& vendor = faction ? faction->vendorData : RE::FACTION_VENDOR_DATA{};
				if (!vendor.vendorSellBuyList || !vendor.merchantContainer || !vendor.vendorSellBuyList->HasForm(keyword)) {
					continue;
				}
				auto* const base = vendor.merchantContainer->GetBaseObject();
				auto* const chest = base ? base->As<RE::TESObjectCONT>() : nullptr;
				if (!chest || !stocked.insert(chest).second) {
					continue;
				}
				if (s.vendorPoisons > 0) {
					chest->AddObjectToContainer(poison, s.vendorPoisons, nullptr);
				}
				if (s.vendorRecipe) {
					chest->AddObjectToContainer(recipe, 1, nullptr);
				}
			}
			logs::info("Spectral Poison and its recipe sold by {} vendor chests", stocked.size());
		}
	}

	void Init()
	{
		const auto& l = Settings::GetLists();
		ingredients = Forms::ResolveAll<RE::TESBoundObject>(l.poisonIngredients, "Spectral Poison ingredient");
		const auto shaders = Forms::ResolveAll<RE::TESEffectShader>(l.poisonShader, "Spectral Poison weapon shader");
		weaponShader = shaders.empty() ? nullptr : shaders.front();
		if (!Forms::Get().poison || !Forms::Get().recipe) {
			logs::warn("Spectral Poison isn't in the plugin: no poison");
			return;
		}
		if (ingredients.empty()) {
			logs::warn("No Spectral Poison ingredients: it can only be bought");
		}
		StockVendors();
		ApplyNames();
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton(); events) {
			events->AddEventSink<RE::TESContainerChangedEvent>(Events::Get());
		}
		if (auto* ui = RE::UI::GetSingleton(); ui) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(Events::Get());
		}
		if (auto* crafted = RE::ItemCrafted::GetEventSource(); crafted) {
			crafted->AddEventSink(Events::Get());
		}
	}

	void ApplyNames()
	{
		const auto& own = Forms::Get();
		if (own.poison) {
			own.poison->SetFullName(T("Spectral Poison"));
		}
		if (own.recipe) {
			own.recipe->SetFullName(T("Recipe: Spectral Poison"));
		}
		if (own.coatingEffect) {
			own.coatingEffect->SetFullName(T("Spectral Poison"));
			own.coatingEffect->magicItemDescription = F("For {:.0f} seconds the poisoned weapon strikes ghosts and Wispmothers as if they had a body.",
				Settings::Get().poisonDuration);
		}
	}

	void Update(float a_delta)
	{
		if (pendingScan > 0.0f) {
			pendingScan -= a_delta;
			TakeOffPoison();
		}
		if (!coated && !glowing[0] && !glowing[1]) {
			return;  // nothing coated, nothing glowing
		}
		bool faded = false;
		{
			std::scoped_lock guard(lock);
			for (auto& coat : coats) {
				coat.remaining -= a_delta;
			}
			faded = std::erase_if(coats, [](const Coat& a_coat) { return a_coat.remaining <= 0.0f; }) > 0;
			coated = !coats.empty();
		}
		if (faded) {
			RE::SendHUDMessage::ShowHUDMessage(T("The spectral coating has faded."));
		}
		UpdateGlow();
	}

	void Reset()
	{
		{
			std::scoped_lock guard(lock);
			coats.clear();
			coated = false;
		}
		StopGlow();
		glowing = {};
		pendingScan = kScanTime;  // a game saved in the moment the poison went on: take it off again
	}

	bool IsCoated(RE::Actor* a_actor, bool a_leftHand)
	{
		if (!coated || !a_actor || !a_actor->IsPlayerRef()) {
			return false;
		}
		std::scoped_lock guard(lock);
		return CoatIn(a_leftHand) != nullptr;
	}

	bool IsCoatingEffect(const RE::EffectSetting* a_effect)
	{
		return a_effect && a_effect == Forms::Get().coatingEffect;
	}

	float Remaining(bool a_leftHand)
	{
		if (!coated) {
			return 0.0f;
		}
		std::scoped_lock guard(lock);
		const auto*      coat = CoatIn(a_leftHand);
		return coat ? coat->remaining : 0.0f;
	}
}
