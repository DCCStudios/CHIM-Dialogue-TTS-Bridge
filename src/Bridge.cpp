#include "Bridge.h"

// CHIM voices the player's dialogue choice through its own dialoguemenu.swf: the SWF sends the
// mod event "PlayMenuTopic" with the topic text and holds the game's "TopicClicked" call until
// CHIM invokes _root.DialogueMenu_mc.startTopicClickedTimer("off"). A UI mod that replaces the
// SWF drops both halves. This plugin does the same handshake from native code, so it works with
// whichever dialogue menu SWF is loaded.
namespace
{
	using CallbackFn = RE::FxDelegateHandler::CallbackFn;

	constexpr auto kResumeFunction = "startTopicClickedTimer";

	CallbackFn* g_originalTopicClicked = nullptr;

	// UI-thread state for the menu instance that is currently open.
	RE::FxDelegateHandler*     g_injectedMenu = nullptr;   // menu that carries our startTopicClickedTimer
	RE::FxDelegateHandler*     g_nativeSwfMenu = nullptr;  // menu whose SWF does the handshake itself
	RE::FxDelegateHandler*     g_pendingMenu = nullptr;
	double                     g_pendingIndex = 0.0;
	bool                       g_pending = false;
	std::atomic<std::uint32_t> g_sequence{ 0 };

	void Resume(const char* a_reason)
	{
		if (!g_pending) {
			return;
		}
		g_pending = false;
		++g_sequence;

		const auto ui = RE::UI::GetSingleton();
		const auto menu = ui ? ui->GetMenu(RE::DialogueMenu::MENU_NAME) : nullptr;
		if (!menu || menu.get() != g_pendingMenu || !menu->uiMovie || !g_originalTopicClicked) {
			logger::info("held topic dropped ({}), the dialogue menu is gone", a_reason);
			return;
		}

		logger::info("resuming topic {} ({})", g_pendingIndex, a_reason);
		const RE::GFxValue       index{ g_pendingIndex };
		const RE::FxDelegateArgs args{ RE::GFxValue{}, menu.get(), menu->uiMovie.get(), &index, 1 };
		g_originalTopicClicked(args);
	}

	// Bound to _root.DialogueMenu_mc.startTopicClickedTimer, the function CHIM invokes once the
	// player's line has finished (or right away when its setting is off).
	class ResumeHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params&) override { Resume("CHIM reply"); }
	};

	// 0 = no DialogueMenu_mc to attach to, 1 = our function is installed, 2 = the SWF has its own.
	int EnsureResumeFunction(RE::FxDelegateHandler* a_menu, RE::GFxMovieView* a_movie)
	{
		if (g_injectedMenu == a_menu) {
			return 1;
		}
		if (g_nativeSwfMenu == a_menu) {
			return 2;
		}

		RE::GFxValue clip;
		if (!a_movie->GetVariable(&clip, "_root.DialogueMenu_mc") || !clip.IsObject()) {
			return 0;
		}

		RE::GFxValue existing;
		if (clip.GetMember(kResumeFunction, &existing) && !existing.IsUndefined() && !existing.IsNull()) {
			g_nativeSwfMenu = a_menu;
			logger::info("dialogue menu SWF already handles player TTS, staying out of the way");
			return 2;
		}

		static RE::GPtr<ResumeHandler> handler = RE::make_gptr<ResumeHandler>();
		RE::GFxValue                   function;
		a_movie->CreateFunction(&function, handler.get());
		if (!clip.SetMember(kResumeFunction, function)) {
			return 0;
		}
		g_injectedMenu = a_menu;
		logger::info("installed {} on the dialogue menu", kResumeFunction);
		return 1;
	}

	std::string TopicTextFromMenu(RE::GFxMovieView* a_movie, double a_index)
	{
		RE::GFxValue entries;
		if (a_movie->GetVariable(&entries, "_root.DialogueMenu_mc.TopicList.entryList") && entries.IsArray()) {
			const auto count = entries.GetArraySize();
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::GFxValue entry, index, text;
				if (entries.GetElement(i, &entry) && entry.IsObject() &&
					entry.GetMember("topicIndex", &index) && index.IsNumber() && index.GetNumber() == a_index &&
					entry.GetMember("text", &text) && text.IsString()) {
					return text.GetString();
				}
			}
		}
		return {};
	}

	std::string TopicTextFromGame(double a_index)
	{
		const auto manager = RE::MenuTopicManager::GetSingleton();
		if (!manager || !manager->dialogueList || a_index < 0.0) {
			return {};
		}
		std::uint32_t i = 0;
		for (const auto dialogue : *manager->dialogueList) {
			if (i++ == static_cast<std::uint32_t>(a_index)) {
				return dialogue ? std::string{ dialogue->topicText.c_str() } : std::string{};
			}
		}
		return {};
	}

	// Same clean-up CHIM's SWF applies before it sends the text.
	std::string Sanitize(std::string a_text)
	{
		if (const auto cut = a_text.find(" ("); cut != std::string::npos) {
			a_text.erase(cut);
		}
		for (auto& c : a_text) {
			if (std::strchr(" /\\:*?\"<>|", c)) {
				c = '_';
			}
		}
		return a_text;
	}

	// Releases the topic if CHIM never answers, so dialogue cannot stay stuck.
	void StartFallbackTimer(std::uint32_t a_sequence, std::size_t a_wordCount)
	{
		const auto wait = 20000ms + 400ms * a_wordCount;
		std::thread([a_sequence, wait] {
			std::this_thread::sleep_for(wait);
			if (g_sequence.load() != a_sequence) {
				return;
			}
			SKSE::GetTaskInterface()->AddUITask([a_sequence] {
				if (g_sequence.load() == a_sequence) {
					Resume("no reply from CHIM");
				}
			});
		}).detach();
	}

	void OnTopicClicked(const RE::FxDelegateArgs& a_params)
	{
		const auto menu = a_params.GetHandler();
		const auto movie = a_params.GetMovie();
		if (!menu || !movie || a_params.GetArgCount() < 1 || !a_params[0].IsNumber() || g_pending) {
			g_originalTopicClicked(a_params);
			return;
		}

		const auto mode = EnsureResumeFunction(menu, movie);
		if (mode == 2) {
			g_originalTopicClicked(a_params);
			return;
		}

		const auto index = a_params[0].GetNumber();
		auto       text = TopicTextFromMenu(movie, index);
		if (text.empty()) {
			text = TopicTextFromGame(index);
		}
		if (text.empty()) {
			logger::warn("no text found for topic {}, passing it through", index);
			g_originalTopicClicked(a_params);
			return;
		}

		const auto wordCount = static_cast<std::size_t>(std::count(text.begin(), text.end(), ' ')) + 1;
		const auto topic = Sanitize(std::move(text));

		// Hold the click only when CHIM has a function to call back.
		if (mode == 1) {
			g_pending = true;
			g_pendingMenu = menu;
			g_pendingIndex = index;
			StartFallbackTimer(++g_sequence, wordCount);
		}

		logger::info("topic {} selected, sending PlayMenuTopic '{}'{}", index, topic, mode == 1 ? "" : " (not held)");
		SKSE::ModCallbackEvent event{ "PlayMenuTopic", topic, 0.0f, nullptr };
		SKSE::GetModCallbackEventSource()->SendEvent(&event);

		if (mode != 1) {
			g_originalTopicClicked(a_params);
		}
	}

	// Swaps the game's "TopicClicked" callback for ours while DialogueMenu registers its callbacks.
	class Interposer : public RE::FxDelegateHandler::CallbackProcessor
	{
	public:
		explicit Interposer(RE::FxDelegateHandler::CallbackProcessor* a_inner) :
			inner(a_inner) {}

		void Process(const RE::GString& a_methodName, CallbackFn* a_method) override
		{
			if (a_method && std::strcmp(a_methodName.c_str(), "TopicClicked") == 0) {
				g_originalTopicClicked = a_method;
				inner->Process(a_methodName, &OnTopicClicked);
			} else {
				inner->Process(a_methodName, a_method);
			}
		}

		RE::FxDelegateHandler::CallbackProcessor* inner;
	};

	struct AcceptHook
	{
		static void thunk(RE::DialogueMenu* a_this, RE::FxDelegateHandler::CallbackProcessor* a_processor)
		{
			Interposer interposer{ a_processor };
			func(a_this, &interposer);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && !a_event->opening && a_event->menuName == RE::DialogueMenu::MENU_NAME) {
				// The menu object is destroyed after this; a new one may reuse the address.
				g_injectedMenu = nullptr;
				g_nativeSwfMenu = nullptr;
				g_pendingMenu = nullptr;
				g_pending = false;
				++g_sequence;
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

bool Bridge::Install()
{
	REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_DialogueMenu[0] };
	AcceptHook::func = vtable.write_vfunc(1, AcceptHook::thunk);

	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_message) {
		if (a_message->type == SKSE::MessagingInterface::kDataLoaded) {
			static MenuSink sink;
			if (const auto ui = RE::UI::GetSingleton()) {
				ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
			}
		}
	});
	return true;
}
