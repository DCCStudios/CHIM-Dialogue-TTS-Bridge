#include "Bridge.h"

namespace Plugin
{
	inline constexpr auto NAME = "CHIMDialogueTTSBridge"sv;
}

// Declared here rather than through add_commonlibsse_plugin() because the CMake-generated
// declaration loses the Address Library v5 bit that SKSE 2.3+ requires on Skyrim 1.7.99+.
SKSEPluginInfo(
	.Version = REL::Version{ 1, 0, 0, 0 },
	.Name = Plugin::NAME,
	.Author = ""sv,
	.SupportEmail = ""sv,
	.StructCompatibility = static_cast<SKSE::StructCompatibility>(
		SKSE::PluginVersionData::kVersionIndependentEx_NoStructUse |
		SKSE::PluginVersionData::kVersionIndependentEx_AddressLibraryV5),
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
	.MinimumSKSEVersion = REL::Version{ 0, 0, 0, 0 })

namespace
{
	void InitializeLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			stl::report_and_fail("Failed to find standard logging directory"sv);
		}
		*path /= (std::string{ Plugin::NAME } + ".log");
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%t] [%l] %v"s);
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("{} 1.0.0 loading on runtime {}", Plugin::NAME, a_skse->RuntimeVersion().string());

	// a_log=false: CommonLib 7's own log::init() would replace the logger set up above.
	SKSE::Init(a_skse, false);

	// The vtable hook goes in at load so it is there before the first Dialogue Menu is built.
	if (Bridge::Install()) {
		logger::info("{} initialized", Plugin::NAME);
	}
	return true;
}
