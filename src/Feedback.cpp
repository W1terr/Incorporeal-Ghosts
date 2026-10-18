#include "Feedback.h"

#include "Forms.h"
#include "Settings.h"

namespace Feedback
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr auto kSpacing = 350ms;  // one sound per ghost per swing / volley

		std::mutex                                       lock;
		std::vector<RE::ActorHandle>                     queue;
		std::unordered_map<RE::FormID, Clock::time_point> last;
	}

	void PassThrough(RE::Actor* a_ghost)
	{
		if (!a_ghost || !Settings::Get().sound || !Forms::Get().passThrough) {
			return;
		}
		const auto       now = Clock::now();
		std::scoped_lock guard(lock);
		auto&            time = last[a_ghost->GetFormID()];
		if (now - time < kSpacing) {
			return;
		}
		time = now;
		if (queue.size() < 16) {
			queue.push_back(a_ghost->GetHandle());
		}
	}

	void Update()
	{
		std::vector<RE::ActorHandle> playing;
		{
			std::scoped_lock guard(lock);
			if (queue.empty()) {
				return;
			}
			playing.swap(queue);
			if (last.size() > 64) {
				const auto now = Clock::now();
				std::erase_if(last, [&](const auto& a_kv) { return now - a_kv.second > 5s; });
			}
		}
		auto* const audio = RE::BSAudioManager::GetSingleton();
		auto* const sound = Forms::Get().passThrough;
		if (!audio || !sound) {
			return;
		}
		for (const auto& handle : playing) {
			const auto ghost = handle.get();
			if (!ghost) {
				continue;
			}
			RE::BSSoundHandle sh;
			if (!audio->GetSoundHandle(sh, sound)) {
				continue;
			}
			auto position = ghost->GetPosition();
			position.z += ghost->GetHeight() * 0.6f;  // about the chest, where the blade went through
			sh.SetPosition(position);
			sh.Play();
		}
	}
}
