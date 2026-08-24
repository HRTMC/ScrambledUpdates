#include "Patches.h"
#include "ScrambledBugs.h"

#include <RE/M/Misc.h>
#include <REL/Module.h>
#include <REL/Offset2ID.h>
#include <REL/Relocation.h>
#include <SKSE/API.h>
#include <SKSE/Interfaces.h>
#include <SKSE/Version.h>

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	std::optional<REL::Offset2ID> g_table;

	bool LoadAddressLibrary()
	{
		g_table.emplace();
		logger::info("address library: {} offsets", g_table->size());
		return g_table->size() != 0;
	}

	// Replaces Relocation::AddressLibrary::Header::Read
	void __fastcall HookHeaderRead(
		ScrambledBugs::Header*        self,
		void*                         /*stream*/,
		const ScrambledBugs::Version* productVersion)
	{
		self->format         = ScrambledBugs::FORMAT_ANNIVERSARY_EDITION;
		self->productVersion = *productVersion;
		self->fileNameLength = 0;
		self->pointerSize    = sizeof(void*);

		// Entries that exist, not the file's identifier range, so the mapping
		// ScrambledBugs sizes from this has no slack to fill.
		self->addressCount = static_cast<std::int32_t>(g_table->size());
	}

	// Replaces Relocation::AddressLibrary::Read
	void __fastcall HookRead(
		ScrambledBugs::Element*      destination,
		void*                        /*stream*/,
		const ScrambledBugs::Header* /*header*/)
	{
		std::size_t written{ 0 };
		for (const auto& entry : *g_table)
		{
			destination[written++] = { entry.id, entry.offset };
		}

		logger::info("loaded {} addresses", written);
	}

	bool g_attempted{ false };
	bool g_patched{ false };

	std::uint32_t PluginVersion(REX::W32::HMODULE module)
	{
		auto* data = REX::W32::GetProcAddress(module, "SKSEPlugin_Version");
		if (!data)
		{
			return 0;
		}
		// dataVersion at 0x0, pluginVersion at 0x4.
		return *reinterpret_cast<const std::uint32_t*>(
			reinterpret_cast<const std::uint8_t*>(data) + 4);
	}

	void AbsoluteJump(std::uint8_t* target, void* destination)
	{
		std::uint8_t jump[]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,  // jmp [rip+0]
			                 0, 0, 0, 0, 0, 0, 0, 0 };
		const auto address = reinterpret_cast<std::uintptr_t>(destination);
		std::memcpy(jump + 6, &address, sizeof(address));

		REL::safe_write(reinterpret_cast<std::uintptr_t>(target), jump, sizeof(jump));
	}

	void Apply()
	{
		if (g_attempted)
		{
			return;
		}

		auto module = REX::W32::GetModuleHandleW(ScrambledBugs::MODULE_NAME);
		if (!module)
		{
			return;
		}

		g_attempted = true;

		const auto version = PluginVersion(module);
		if (version != Patches::PLUGIN_VERSION)
		{
			logger::error("ScrambledBugs is plugin version {}, expected {}",
			              version, Patches::PLUGIN_VERSION);
			return;
		}

		auto* base = reinterpret_cast<std::uint8_t*>(module);

		for (const auto& site : Patches::DISPLACEMENTS)
		{
			REL::safe_write(
				reinterpret_cast<std::uintptr_t>(base + site.rva + site.displacementAt),
				site.corrected);
		}

		AbsoluteJump(base + Patches::HEADER_READ, &HookHeaderRead);
		AbsoluteJump(base + Patches::ADDRESS_LIBRARY_READ, &HookRead);

		g_patched = true;
		logger::info("patched ScrambledBugs {} at {}: {} displacements corrected",
		             version, static_cast<const void*>(base),
		             std::size(Patches::DISPLACEMENTS));
	}

	constexpr std::uint32_t DLL_NOTIFICATION_REASON_LOADED{ 1 };

	using DllNotificationCallback = void(__stdcall*)(std::uint32_t, const void*, void*);
	using RegisterNotification =
		std::int32_t(__stdcall*)(std::uint32_t, DllNotificationCallback, void*, void**);

	void __stdcall OnDllLoaded(std::uint32_t reason, const void* /*data*/, void* /*context*/)
	{
		if (reason == DLL_NOTIFICATION_REASON_LOADED)
		{
			Apply();
		}
	}

	void WatchForScrambledBugs()
	{
		auto add = reinterpret_cast<RegisterNotification>(REX::W32::GetProcAddress(
			REX::W32::GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));

		void* dummy{ nullptr };
		if (add(0, &OnDllLoaded, nullptr, &dummy) != 0)
		{
			logger::error("could not watch for ScrambledBugs");
		}
	}

	void InitializeLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			stl::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= "ScrambledUpdates.log"sv;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v"s);
	}

	bool NeedsPatching()
	{
		return REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_7_99;
	}

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		if (message->type != SKSE::MessagingInterface::kDataLoaded || g_patched)
		{
			return;
		}

		const auto* warning = REX::W32::GetModuleHandleW(ScrambledBugs::MODULE_NAME)
			? "ScrambledBugs is installed but could not be patched, so its fixes are "
			  "not working. See ScrambledUpdates.log"
			: "ScrambledBugs is not installed, so ScrambledUpdates is inactive";

		logger::error("{}", warning);
		RE::DebugMessageBox(warning);
	}
}

SKSEPluginInfo(
	.Version = REL::Version{ 1, 0, 0, 0 },
	.Name = "ScrambledUpdates"sv,
	.Author = "doodlum"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::PluginDeclaration::RuntimeCompatibility(
		SKSE::VersionIndependence::AddressLibrary))

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	if (NeedsPatching())
	{
		// false, or Init replaces the logger opened in preload.
		SKSE::Init(skse, false);
		SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	}
	return true;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Preload(const SKSE::LoadInterface* /*skse*/)
{
	if (!NeedsPatching())
	{
		return true;
	}

	InitializeLog();

	if (!LoadAddressLibrary())
	{
		return true;
	}

	// ScrambledBugs exports no SKSEPlugin_Preload so it's never loaded at this point
	WatchForScrambledBugs();

	return true;
}
