#include "plugins.h"
#include "SoundEngine.h"
#include "SoundpadConfig.h"
#include "SoundpadFrame.h"
#include "VGUI2ExtensionImport.h"
#include "VoiceExtensionImport.h"
#include "GameState.h"
#include "DebugLog.h"

#include <interface.h>
#include <vgui/isurface.h>
#include <vgui_controls/Controls.h>
#include <IKeyValuesSystem.h>
#include <KeyValues.h>
#include <strsafe.h>
#include <regex>
#include <random>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <const.h>
#include <pmtrace.h>
#include <event_api.h>
#include <com_model.h>
#include <r_studioint.h>

cl_enginefunc_t gEngfuncs = { 0 };
cl_exportfuncs_t gExportfuncs = { 0 };
static engine_studio_api_t g_engineStudio = { 0 };
static int (*g_pfnHUD_GetStudioModelInterface)(int version, struct r_studio_interface_s** ppinterface, struct engine_studio_api_s* pstudio) = nullptr;

static int Hook_HUD_GetStudioModelInterface(int version, struct r_studio_interface_s** ppinterface, struct engine_studio_api_s* pstudio)
{
	if (pstudio)
	{
		memcpy(&g_engineStudio, pstudio, sizeof(g_engineStudio));
	}
	if (g_pfnHUD_GetStudioModelInterface)
	{
		return g_pfnHUD_GetStudioModelInterface(version, ppinterface, pstudio);
	}
	return 0;
}

void SafeConPrintf(const char* fmt, ...)
{
	if (!gEngfuncs.Con_Printf)
		return;
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	buffer[sizeof(buffer) - 1] = '\0';
	gEngfuncs.Con_Printf("%s", buffer);
}

extern int g_iEngineType;

// DLL notifications run inside the Ldr critical region. They only record a
// pending reload here; the actual interface work happens on the game thread.
static std::atomic<bool> g_pendingVoiceReload{ false };
static std::atomic<bool> g_pendingVguiReload{ false };
static void ProcessPendingModuleNotifications();

uint64_t GetPlayerSteamID(int clientIndex)
{
	if (clientIndex < 1 || clientIndex > 32)
		return 0;

	// In the main menu or disconnected state, maxclients is 0 or unassigned.
	// Querying player info or studio interfaces outside an active game session triggers an access violation.
	if (!gEngfuncs.GetMaxClients || gEngfuncs.GetMaxClients() <= 0 || clientIndex > gEngfuncs.GetMaxClients())
		return 0;

	cl_entity_t* localEnt = gEngfuncs.GetLocalPlayer ? gEngfuncs.GetLocalPlayer() : nullptr;
	if (!localEnt || localEnt->player == 0 || localEnt->index <= 0)
		return 0;

	// 1. Try Sven Co-op player_info_sc_t from engine studio interface (SvEngine only)
	if (g_iEngineType == ENGINE_SVENGINE && g_engineStudio.PlayerInfo)
	{
		auto* sc_info = reinterpret_cast<player_info_sc_t*>(g_engineStudio.PlayerInfo(clientIndex - 1));
		if (sc_info && sc_info->m_nSteamID != 0)
			return sc_info->m_nSteamID;
	}

	// 2. Try hud_player_info_t from engine
	if (gEngfuncs.pfnGetPlayerInfo)
	{
		hud_player_info_t info = {};
		gEngfuncs.pfnGetPlayerInfo(clientIndex, &info);
		if (info.m_nSteamID != 0)
			return info.m_nSteamID;
	}

	// 3. Fallback for local player via steam_api.dll
	if (localEnt->index == clientIndex)
	{
		HMODULE hSteamApi = GetModuleHandleA("steam_api.dll");
		if (hSteamApi)
		{
			using FnSteamAPI_IsSteamRunning = bool(*)();
			using FnGetHSteamUser = int32_t(*)();
			using FnFindOrCreateUserInterface = void*(*)(int32_t, const char*);
			using FnSteamAPI_ISteamUser_GetSteamID = uint64_t(*)(void*);

			auto pfnIsSteamRunning = reinterpret_cast<FnSteamAPI_IsSteamRunning>(GetProcAddress(hSteamApi, "SteamAPI_IsSteamRunning"));
			if (pfnIsSteamRunning && !pfnIsSteamRunning())
			{
				return 0;
			}

			auto pfnGetHSteamUser = reinterpret_cast<FnGetHSteamUser>(GetProcAddress(hSteamApi, "SteamAPI_GetHSteamUser"));
			auto pfnFindOrCreateUserInterface = reinterpret_cast<FnFindOrCreateUserInterface>(GetProcAddress(hSteamApi, "SteamInternal_FindOrCreateUserInterface"));
			auto pfnGetSteamID = reinterpret_cast<FnSteamAPI_ISteamUser_GetSteamID>(GetProcAddress(hSteamApi, "SteamAPI_ISteamUser_GetSteamID"));

			if (pfnGetHSteamUser && pfnFindOrCreateUserInterface && pfnGetSteamID)
			{
				int32_t hUser = pfnGetHSteamUser();
				if (hUser)
				{
					void* pSteamUser = pfnFindOrCreateUserInterface(hUser, "SteamUser020");
					if (pSteamUser)
					{
						uint64_t id = pfnGetSteamID(pSteamUser);
						if (id != 0) return id;
					}
				}
			}
		}
	}

	return 0;
}
metahook_api_t* g_pMetaHookAPI = nullptr;
mh_interface_t* g_pInterface = nullptr;
mh_enginesave_t* g_pMetaSave = nullptr;
IFileSystem* g_pFileSystem = nullptr;
extern IFileSystem* g_pFullFileSystem;
extern IKeyValuesSystem* g_pKeyValuesSystem;
int g_iEngineType = 0;
DWORD g_dwEngineBuildnum = 0;

cvar_t* sp_allow_server = nullptr;
cvar_t* sp_pause_on_mic = nullptr;
cvar_t* sp_mic_duck = nullptr;
cvar_t* sp_duck_level = nullptr;
cvar_t* sp_reactions_enabled = nullptr;
cvar_t* sp_autohide = nullptr;
cvar_t* sp_tts_engine = nullptr;
cvar_t* sp_sapi_voice = nullptr;
cvar_t* sp_tts_volume = nullptr;
cvar_t* sp_tts_pitch = nullptr;
cvar_t* sp_tts_jitter = nullptr;
cvar_t* sp_tts_whisper = nullptr;
cvar_t* sp_tts_autosay = nullptr;
cvar_t* sp_tts_random_style = nullptr;
cvar_t* sp_voice_hangtime = nullptr;
cvar_t* sp_medic_auto = nullptr;
cvar_t* sp_debug = nullptr;

static void (*g_originalHudFrame)(double) = nullptr;

static uint32_t g_lastDamageBits = 0;
static int g_lastDamageTaken = 0;
static double g_lastDamageTime = 0.0;

static IBaseInterface* SafeFileSystemFactory(const char* pName, int* pReturnCode)
{
	if (g_pFileSystem)
	{
		if (pReturnCode) *pReturnCode = 0;
		return (IBaseInterface*)g_pFileSystem;
	}
	return nullptr;
}

static bool InitVGuiInterfaces(const char* moduleName, CreateInterfaceFn* factories, int count)
{
	if (!g_pFileSystem && g_pInterface)
	{
		g_pFileSystem = g_pInterface->FileSystem;
		if (!g_pFileSystem)
			g_pFileSystem = (IFileSystem*)g_pInterface->FileSystem_HL25;
	}
	if (!g_pFullFileSystem)
	{
		g_pFullFileSystem = g_pFileSystem;
	}

	if (!g_pKeyValuesSystem)
	{
		HINTERFACEMODULE hVGUI2 = (HINTERFACEMODULE)GetModuleHandleA("vgui2.dll");
		if (hVGUI2)
		{
			CreateInterfaceFn fnVGUI2CreateInterface = Sys_GetFactory(hVGUI2);
			if (fnVGUI2CreateInterface)
			{
				g_pKeyValuesSystem = (IKeyValuesSystem*)fnVGUI2CreateInterface(KEYVALUESSYSTEM_INTERFACE_VERSION, NULL);
			}
		}
	}

	if (!g_pKeyValuesSystem && factories && count > 1 && factories[1])
	{
		g_pKeyValuesSystem = (IKeyValuesSystem*)factories[1](KEYVALUESSYSTEM_INTERFACE_VERSION, NULL);
	}

	if (!g_pKeyValuesSystem)
	{
		g_pKeyValuesSystem = KeyValuesSystem();
	}

	if (g_pKeyValuesSystem)
	{
		g_pKeyValuesSystem->RegisterSizeofKeyValues(sizeof(KeyValues));
	}

	// Prepare safe factory array with 3 guaranteed elements so Controls.cpp never dereferences null or reads out-of-bounds
	CreateInterfaceFn safeFactories[3] = { nullptr, nullptr, SafeFileSystemFactory };
	if (factories && count > 0 && factories[0])
		safeFactories[0] = factories[0];
	if (factories && count > 1 && factories[1])
		safeFactories[1] = factories[1];
	if (factories && count > 2 && factories[2])
	{
		void* pTestFs = factories[2](FILESYSTEM_INTERFACE_VERSION, NULL);
		if (pTestFs)
			safeFactories[2] = factories[2];
	}

	// Fallback for factory 0 (engine) if missing
	if (!safeFactories[0] && g_pMetaHookAPI)
	{
		safeFactories[0] = g_pMetaHookAPI->GetEngineFactory();
	}
	if (!safeFactories[0])
	{
		HMODULE hEngine = GetModuleHandleA("hw.dll");
		if (!hEngine) hEngine = GetModuleHandleA("sw.dll");
		if (hEngine)
			safeFactories[0] = Sys_GetFactory((HINTERFACEMODULE)hEngine);
	}

	// Fallback for factory 1 (vgui2) if missing
	if (!safeFactories[1])
	{
		HMODULE hVGUI2 = GetModuleHandleA("vgui2.dll");
		if (hVGUI2)
			safeFactories[1] = Sys_GetFactory((HINTERFACEMODULE)hVGUI2);
	}

	// VGui_InitInterfacesList calls factoryList[0] and factoryList[1] unconditionally;
	// calling a null function pointer crashes immediately without error dialogue!
	if (!safeFactories[0] || !safeFactories[1])
	{
		if (gEngfuncs.Con_DPrintf)
			gEngfuncs.Con_DPrintf("[Soundpad] Aborting VGui_InitInterfacesList: missing required engine or vgui2 factory.\n");
		return false;
	}

	bool bInit = vgui::VGui_InitInterfacesList(moduleName, safeFactories, 3);
	if (!g_pFullFileSystem && g_pFileSystem)
	{
		g_pFullFileSystem = g_pFileSystem;
	}

	return bInit;
}

CVGUI2Extension_ClientVGUICallbacks g_ClientVGUICallbacks;

void CVGUI2Extension_ClientVGUICallbacks::Initialize(CreateInterfaceFn* factories, int count)
{
	if (!InitVGuiInterfaces("Soundpad", factories, count))
	{
		if (gEngfuncs.Con_DPrintf)
			gEngfuncs.Con_DPrintf("[Soundpad] Failed to VGui_InitInterfacesList for ClientVGUI; UI disabled.\n");
		return;
	}

	CSoundpadConfig::Get().Load();
	if (gEngfuncs.Cvar_Set)
	{
		const std::string& eng = CSoundpadConfig::Get().GetTtsEngine();
		if (!eng.empty() && sp_tts_engine)
			gEngfuncs.Cvar_Set("sp_tts_engine", eng.c_str());
		const std::string& voice = CSoundpadConfig::Get().GetSapiVoice();
		if (!voice.empty() && sp_sapi_voice)
			gEngfuncs.Cvar_Set("sp_sapi_voice", voice.c_str());
	}
}

bool g_bOpenedFromCommand = false;
bool g_bReopenOnGameUI = false;
CVGUI2Extension_GameUICallbacks g_GameUICallbacks;

void CVGUI2Extension_GameUICallbacks::Initialize(CreateInterfaceFn* factories, int count)
{
	if (!InitVGuiInterfaces("Soundpad", factories, count))
	{
		if (gEngfuncs.Con_DPrintf)
			gEngfuncs.Con_DPrintf("[Soundpad] Failed to VGui_InitInterfacesList for GameUI; UI disabled.\n");
		return;
	}

	CSoundpadConfig::Get().Load();
	if (gEngfuncs.Cvar_Set)
	{
		const std::string& eng = CSoundpadConfig::Get().GetTtsEngine();
		if (!eng.empty() && sp_tts_engine)
			gEngfuncs.Cvar_Set("sp_tts_engine", eng.c_str());
		const std::string& voice = CSoundpadConfig::Get().GetSapiVoice();
		if (!voice.empty() && sp_sapi_voice)
			gEngfuncs.Cvar_Set("sp_sapi_voice", voice.c_str());
	}
}

void CVGUI2Extension_GameUICallbacks::Shutdown()
{
	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->CloseAllSubDialogs();
		delete g_hSoundpadFrame.Get();
		g_hSoundpadFrame = nullptr;
	}
}

void CVGUI2Extension_GameUICallbacks::ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext)
{
	if (g_bReopenOnGameUI)
	{
		Soundpad_OpenGUI(nullptr, false);
	}
}

void CVGUI2Extension_GameUICallbacks::HideGameUI(VGUI2Extension_CallbackContext* CallbackContext)
{
	if (g_hSoundpadFrame && g_hSoundpadFrame->IsVisible())
	{
		g_hSoundpadFrame->SetVisible(false);
	}
}

void CVGUI2Extension_GameUICallbacks::DisconnectFromServer(VGUI2Extension_CallbackContext* CallbackContext)
{
	DebuLog("DisconnectFromServer: server transition / disconnect, stopping all audio playback");
	SoundEngine_Stop();
	CGameState::Get().Reset();
	g_lastDamageBits = 0;
	g_lastDamageTaken = 0;
	g_lastDamageTime = 0.0;

	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->CloseAllSubDialogs();
		g_hSoundpadFrame->RefreshPlayersList();
	}
}

void CVGUI2Extension_GameUICallbacks::LoadingStarted(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext)
{
	DebuLog("LoadingStarted: level loading started, stopping audio playback");
	SoundEngine_Stop();
	CGameState::Get().Reset();
	g_lastDamageBits = 0;
	g_lastDamageTaken = 0;
	g_lastDamageTime = 0.0;

	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->CloseAllSubDialogs();
	}
}

void CVGUI2Extension_GameUICallbacks::LoadingFinished(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext)
{
	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->RefreshPlayersList();
	}
}

void CVGUI2Extension_ClientVGUICallbacks::Shutdown()
{
	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->CloseAllSubDialogs();
		delete g_hSoundpadFrame.Get();
		g_hSoundpadFrame = nullptr;
	}
}

void CVGUI2Extension_ClientVGUICallbacks::HideAllVGUIMenu(VGUI2Extension_CallbackContext* CallbackContext)
{
}

void CVGUI2Extension_ClientVGUICallbacks::HideClientUI(VGUI2Extension_CallbackContext* CallbackContext)
{
}

class CVGUI2Extension_BaseUICallbacks : public IVGUI2Extension_BaseUICallbacks
{
public:
	int GetAltitude() const override { return 0; }

	void Initialize(CreateInterfaceFn* factories, int count) override
	{
		if (!g_pKeyValuesSystem)
		{
			HINTERFACEMODULE hVGUI2 = (HINTERFACEMODULE)GetModuleHandleA("vgui2.dll");
			if (hVGUI2)
			{
				CreateInterfaceFn fnVGUI2CreateInterface = Sys_GetFactory(hVGUI2);
				if (fnVGUI2CreateInterface)
				{
					g_pKeyValuesSystem = (IKeyValuesSystem*)fnVGUI2CreateInterface(KEYVALUESSYSTEM_INTERFACE_VERSION, NULL);
					if (g_pKeyValuesSystem)
					{
						g_pKeyValuesSystem->RegisterSizeofKeyValues(sizeof(KeyValues));
					}
				}
			}
		}
		if (!g_pKeyValuesSystem && factories && count > 1 && factories[1])
		{
			g_pKeyValuesSystem = (IKeyValuesSystem*)factories[1](KEYVALUESSYSTEM_INTERFACE_VERSION, NULL);
			if (g_pKeyValuesSystem)
			{
				g_pKeyValuesSystem->RegisterSizeofKeyValues(sizeof(KeyValues));
			}
		}
	}

	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion) override {}
	void Shutdown(void) override {}
	void Key_Event(int& down, int& keynum, const char*& pszCurrentBinding, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void CallEngineSurfaceAppProc(void*& pevent, void*& userData, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void CallEngineSurfaceWndProc(void*& hwnd, unsigned int& msg, unsigned int& wparam, long& lparam, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Paint(int& x, int& y, int& right, int& bottom, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideConsole(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ShowConsole(VGUI2Extension_CallbackContext* CallbackContext) override {}
};

static CVGUI2Extension_BaseUICallbacks s_BaseUICallbacks;

class CVGUI2Extension_KeyValuesCallbacks : public IVGUI2Extension_KeyValuesCallbacks
{
public:
	int GetAltitude() const override { return 1; }

	void KeyValues_LoadFromFile(void*& pthis, IFileSystem*& pFileSystem, const char*& resourceName, const char*& pathId, const char* sourceModule, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		if (!CallbackContext || !pthis || !resourceName)
			return;

		if (CallbackContext->IsPost && !strcmp(resourceName, "resource/GameMenu.res"))
		{
			bool* pRealReturnValue = (bool*)CallbackContext->pRealReturnValue;
			if (pRealReturnValue && *pRealReturnValue == true)
			{
				if (!g_pKeyValuesSystem)
				{
					HINTERFACEMODULE hVGUI2 = (HINTERFACEMODULE)GetModuleHandleA("vgui2.dll");
					if (hVGUI2)
					{
						CreateInterfaceFn fnVGUI2CreateInterface = Sys_GetFactory(hVGUI2);
						if (fnVGUI2CreateInterface)
						{
							g_pKeyValuesSystem = (IKeyValuesSystem*)fnVGUI2CreateInterface(KEYVALUESSYSTEM_INTERFACE_VERSION, NULL);
							if (g_pKeyValuesSystem)
								g_pKeyValuesSystem->RegisterSizeofKeyValues(sizeof(KeyValues));
						}
					}
				}

				if (!g_pKeyValuesSystem)
					return;

				KeyValues* pKeyValues = (KeyValues*)pthis;
				for (auto p = pKeyValues->GetFirstSubKey(); p; p = p->GetNextKey())
				{
					const char* cmd = p->GetString("command", "");
					if (cmd && !stricmp(cmd, "OpenSoundpadDialog"))
						return;
				}

				KeyValues* SectionTarget = nullptr;
				int maxId = 0;
				for (auto p = pKeyValues->GetFirstSubKey(); p; p = p->GetNextKey())
				{
					const char* name = p->GetName();
					if (name && *name)
					{
						int id = atoi(name);
						if (id > maxId) maxId = id;
					}

					const char* command = p->GetString("command", "");
					if (command && (!stricmp(command, "OpenOptionsDialog") || !stricmp(command, "ResumeGame")))
						SectionTarget = p;
				}
				if (!SectionTarget)
					SectionTarget = pKeyValues->GetFirstSubKey();

				if (SectionTarget)
				{
					char szNewNameButton[32];
					snprintf(szNewNameButton, sizeof(szNewNameButton), "%d", maxId + 1);
					auto SectionSoundpad = new KeyValues(szNewNameButton);
					if (SectionSoundpad)
					{
						SectionSoundpad->SetString("label", "Soundpad");
						SectionSoundpad->SetString("command", "OpenSoundpadDialog");
						pKeyValues->AddSubKeyAfter(SectionSoundpad, SectionTarget);
					}
				}
			}
		}
	}
};

static CVGUI2Extension_KeyValuesCallbacks s_KeyValuesCallbacks;

class CVGUI2Extension_TaskBarCallbacks : public IVGUI2Extension_GameUITaskBarCallbacks
{
public:
	int GetAltitude() const override { return 0; }

	void CTaskBar_ctor(IGameUITaskBarCtorCallbackContext* CallbackContext) override {}

	void CTaskBar_OnCommand(void*& pPanel, const char*& command, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		if (command && !strcmp(command, "OpenSoundpadDialog"))
		{
			Soundpad_OpenGUI(nullptr, false);
		}
	}
};

static CVGUI2Extension_TaskBarCallbacks s_TaskBarCallbacks;

class SafeBufferReader
{
public:
	SafeBufferReader(const void* buf, int size)
		: m_ptr(static_cast<const uint8_t*>(buf)), m_size(size), m_pos(0)
	{
		if (!m_ptr || m_size <= 0)
		{
			m_ptr = nullptr;
			m_size = 0;
			m_pos = 0;
		}
	}

	bool HasMore() const { return m_ptr && m_pos < m_size; }

	int ReadByte()
	{
		if (!m_ptr || m_pos + 1 > m_size) return -1;
		return m_ptr[m_pos++];
	}

	int ReadShort()
	{
		if (!m_ptr || m_pos + 2 > m_size) return 0;
		int16_t val = 0;
		memcpy(&val, m_ptr + m_pos, sizeof(val));
		m_pos += 2;
		return val;
	}

	int ReadLong()
	{
		if (!m_ptr || m_pos + 4 > m_size) return 0;
		int val = 0;
		memcpy(&val, m_ptr + m_pos, sizeof(val));
		m_pos += 4;
		return val;
	}

	std::string ReadString()
	{
		std::string str;
		if (!m_ptr) return str;
		while (m_pos < m_size && m_ptr[m_pos] != '\0')
		{
			unsigned char c = m_ptr[m_pos++];
			if (c >= 0x20 || c <= 0x04 || c == 0x07 || c == '\r' || c == '\n' || c == '\t')
				str += static_cast<char>(c);
		}
		if (m_pos < m_size && m_ptr[m_pos] == '\0')
			m_pos++;
		return str;
	}

private:
	const uint8_t* m_ptr;
	int m_size;
	int m_pos;
};

static bool MatchesDamageType(uint32_t bits, const std::string& condition)
{
	if (condition.empty() || _stricmp(condition.c_str(), "GENERIC") == 0 || _stricmp(condition.c_str(), "ANY") == 0)
		return true;

	// GoldSrc const.h DMG_* bits (verified against GoldSrc SDK)
	if (_stricmp(condition.c_str(), "CRUSH")  == 0 && (bits & (1 << 0)))  return true; // DMG_CRUSH
	if (_stricmp(condition.c_str(), "BULLET") == 0 && (bits & (1 << 1)))  return true; // DMG_BULLET
	if (_stricmp(condition.c_str(), "SLASH")  == 0 && (bits & (1 << 2)))  return true; // DMG_SLASH
	if ((_stricmp(condition.c_str(), "BURN") == 0 || _stricmp(condition.c_str(), "FIRE") == 0) && (bits & (1 << 3)))  return true; // DMG_BURN
	if (_stricmp(condition.c_str(), "FREEZE") == 0 && (bits & (1 << 4)))  return true; // DMG_FREEZE
	if (_stricmp(condition.c_str(), "FALL")   == 0 && (bits & (1 << 5)))  return true; // DMG_FALL
	if ((_stricmp(condition.c_str(), "BLAST") == 0 || _stricmp(condition.c_str(), "EXPLOSIVE") == 0) && (bits & (1 << 6))) return true; // DMG_BLAST
	if (_stricmp(condition.c_str(), "CLUB")   == 0 && (bits & (1 << 7)))  return true; // DMG_CLUB
	if ((_stricmp(condition.c_str(), "SHOCK") == 0 || _stricmp(condition.c_str(), "ELECTRIC") == 0) && (bits & (1 << 8))) return true; // DMG_SHOCK
	if (_stricmp(condition.c_str(), "SONIC")  == 0 && (bits & (1 << 9)))  return true; // DMG_SONIC
	if (_stricmp(condition.c_str(), "ENERGY") == 0 && (bits & (1 << 10))) return true; // DMG_ENERGYBEAM
	if (_stricmp(condition.c_str(), "DROWN")  == 0 && (bits & (1 << 14))) return true; // DMG_DROWN
	if (_stricmp(condition.c_str(), "ACID")   == 0 && (bits & (1 << 15))) return true; // DMG_ACID
	if (_stricmp(condition.c_str(), "POISON") == 0 && (bits & (1 << 16))) return true; // DMG_POISON
	if (_stricmp(condition.c_str(), "RADIATION") == 0 && (bits & (1 << 17))) return true; // DMG_RADIATION

	return false;
}

static bool MatchWordOrExact(const std::string& text, const std::string& target)
{
	if (text.empty() || target.empty())
		return false;

	if (_stricmp(text.c_str(), target.c_str()) == 0)
		return true;

	std::string tLower = text;
	std::transform(tLower.begin(), tLower.end(), tLower.begin(), ::tolower);
	std::string targetLower = target;
	std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), ::tolower);

	size_t pos = 0;
	while ((pos = tLower.find(targetLower, pos)) != std::string::npos)
	{
		bool validBefore = (pos == 0) || !isalnum(static_cast<unsigned char>(tLower[pos - 1]));
		size_t endPos = pos + targetLower.size();
		bool validAfter = (endPos >= tLower.size()) || !isalnum(static_cast<unsigned char>(tLower[endPos]));

		if (validBefore && validAfter)
			return true;

		pos++;
	}

	return false;
}

static double g_lastGlobalReactionTime = 0.0;
static double g_lastDeathReactionTime = 0.0;

static bool CheckAndFireReaction(ReactionEventType evType, const std::string& param, bool forceTest = false)
{
	if (!forceTest && sp_reactions_enabled && sp_reactions_enabled->value <= 0.0f)
		return false;

	double nowTime = 0.0;
	LARGE_INTEGER counter = {}, freq = {};
	if (QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&freq) && freq.QuadPart > 0)
		nowTime = static_cast<double>(counter.QuadPart) / static_cast<double>(freq.QuadPart);

	// Prevent duplicate events in the exact same frame/tick from cutting and dropping each other
	if (!forceTest && (nowTime - g_lastGlobalReactionTime < 0.30))
	{
		return false;
	}

	// Death-specific anti-spam throttle: prevent rapid multi-death spam within 1.0s
	if (!forceTest && evType == ReactionEventType::Death && (nowTime - g_lastDeathReactionTime < 1.0))
	{
		return false;
	}

	auto& reactions = CSoundpadConfig::Get().GetReactions();
	std::vector<ReactionItem*> candidates;

	for (auto& r : reactions)
	{
		if (r.eventType != evType)
			continue;

		double effectiveCooldown = (std::max)(0.5, static_cast<double>(r.cooldown));
		if (!forceTest && (nowTime - r.lastTriggeredTime < effectiveCooldown))
		{
			continue;
		}

		bool conditionMatched = false;
		if (evType == ReactionEventType::Death)
		{
			bool isSuicide = CGameState::Get().IsLastDeathSuicide();
			bool isKillerPlayer = CGameState::Get().WasLastKillerPlayer();

			if (r.condition.empty() || _stricmp(r.condition.c_str(), "GENERIC") == 0 || _stricmp(r.condition.c_str(), "ANY") == 0)
			{
				conditionMatched = true;
			}
			else if (_stricmp(r.condition.c_str(), "SUICIDE") == 0 || _stricmp(r.condition.c_str(), "SELF") == 0)
			{
				conditionMatched = isSuicide;
			}
			else if (_stricmp(r.condition.c_str(), "WORLD") == 0)
			{
				conditionMatched = !isSuicide && (param.empty() || _stricmp(param.c_str(), "world") == 0 || _stricmp(param.c_str(), "worldspawn") == 0 || (g_lastDamageBits & (1 << 5)) /* FALL */ || (g_lastDamageBits & (1 << 14)) /* DROWN */);
			}
			else if (_stricmp(r.condition.c_str(), "MONSTER") == 0)
			{
				conditionMatched = !isSuicide && !isKillerPlayer && (!param.empty() && (param.rfind("monster_", 0) == 0 || (_stricmp(param.c_str(), "world") != 0 && _stricmp(param.c_str(), "worldspawn") != 0)));
			}
			else if (_stricmp(r.condition.c_str(), "PLAYER") == 0)
			{
				conditionMatched = !isSuicide && isKillerPlayer;
			}
			else if (g_lastDamageBits != 0
			         && (nowTime - g_lastDamageTime) < 2.0  // discard stale damage bits after 2s
			         && MatchesDamageType(g_lastDamageBits, r.condition))
			{
				conditionMatched = true;
			}
			else if (!param.empty())
			{
				conditionMatched = MatchWordOrExact(param, r.condition) || MatchWordOrExact(r.condition, param);
			}
		}
		else if (evType == ReactionEventType::Damage)
		{
			if (r.condition.empty() || _stricmp(r.condition.c_str(), "GENERIC") == 0 || _stricmp(r.condition.c_str(), "ANY") == 0)
			{
				conditionMatched = true;
			}
			else if (r.condition[0] == '<')
			{
				int thresh = atoi(r.condition.c_str() + (r.condition[1] == '=' ? 2 : 1));
				int curHp = CGameState::Get().GetHealth();
				if ((curHp > 0 && curHp <= thresh) || forceTest)
					conditionMatched = true;
			}
			else if (r.condition[0] == '>')
			{
				int thresh = atoi(r.condition.c_str() + (r.condition[1] == '=' ? 2 : 1));
				if (g_lastDamageTaken >= thresh || forceTest)
					conditionMatched = true;
			}
			else if (g_lastDamageBits != 0 && MatchesDamageType(g_lastDamageBits, r.condition))
			{
				conditionMatched = true;
			}
			else if (!param.empty())
			{
				conditionMatched = MatchWordOrExact(param, r.condition) || MatchWordOrExact(r.condition, param);
			}
		}
		else if (evType == ReactionEventType::Kill)
		{
			bool isVictimPlayer = CGameState::Get().WasLastKillPlayer();
			if (r.condition.empty() || _stricmp(r.condition.c_str(), "ANY") == 0)
			{
				conditionMatched = true;
			}
			else if (_stricmp(r.condition.c_str(), "MONSTER") == 0)
			{
				conditionMatched = !isVictimPlayer;
			}
			else if (_stricmp(r.condition.c_str(), "PLAYER") == 0)
			{
				conditionMatched = isVictimPlayer;
			}
			else
			{
				conditionMatched = MatchWordOrExact(param, r.condition) || MatchWordOrExact(r.condition, param);
			}
		}
		else if (evType == ReactionEventType::OtherDeath)
		{
			if (r.condition.empty() || _stricmp(r.condition.c_str(), "ANY") == 0 || _stricmp(r.condition.c_str(), "GENERIC") == 0)
			{
				conditionMatched = true;
			}
			else
			{
				conditionMatched = MatchWordOrExact(param, r.condition) || MatchWordOrExact(r.condition, param);
			}
		}
		else if (evType == ReactionEventType::Chat)
		{
			if (r.condition.empty() || _stricmp(r.condition.c_str(), "ANY") == 0)
			{
				conditionMatched = true;
			}
			else
			{
				try
				{
					std::regex re(r.condition, std::regex_constants::icase);
					conditionMatched = std::regex_search(param, re);
				}
				catch (...)
				{
					conditionMatched = MatchWordOrExact(param, r.condition) || MatchWordOrExact(r.condition, param);
				}
			}
		}
		else if (evType == ReactionEventType::Command)
		{
			if (r.condition.empty() || _stricmp(r.condition.c_str(), "GENERIC") == 0 || _stricmp(r.condition.c_str(), "ANY") == 0)
			{
				conditionMatched = true;
			}
			else
			{
				std::string cond = r.condition;
				size_t ltPos = cond.find('<');
				std::string cmdPart = (ltPos != std::string::npos) ? cond.substr(0, ltPos) : cond;
				std::string hpPart = (ltPos != std::string::npos) ? cond.substr(ltPos) : "";

				while (!cmdPart.empty() && isspace(static_cast<unsigned char>(cmdPart.back())))
					cmdPart.pop_back();
				size_t start = 0;
				while (start < cmdPart.size() && isspace(static_cast<unsigned char>(cmdPart[start])))
					start++;
				if (start > 0)
					cmdPart = cmdPart.substr(start);

				bool cmdMatched = false;
				if (cmdPart.empty())
				{
					cmdMatched = true;
				}
				else if (!param.empty())
				{
					std::string pLower = param;
					std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
					std::string cLower = cmdPart;
					std::transform(cLower.begin(), cLower.end(), cLower.begin(), ::tolower);
					if (pLower == cLower || pLower.find(cLower) == 0)
						cmdMatched = true;
				}

				bool hpMatched = true;
				if (!hpPart.empty())
				{
					const char* p = hpPart.c_str() + 1;
					if (*p == '=') ++p;
					while (*p == ' ') ++p;
					int thresh = atoi(p);
					int curHp = CGameState::Get().GetHealth();
					if (curHp > 0 && curHp > thresh && !forceTest)
					{
						hpMatched = false;
					}
				}

				if (cmdMatched && hpMatched)
				{
					conditionMatched = true;
				}
			}
		}

		if (conditionMatched)
		{
			candidates.push_back(&r);
		}
	}

	if (candidates.empty())
	{
		if (forceTest && gEngfuncs.Con_Printf)
		{
			SafeConPrintf("[Soundpad] No reactions matched event=%d param='%s'.\n",
				static_cast<int>(evType), param.c_str());
		}
		return false;
	}

	// Calculate total probability weight among competing triggers
	float totalWeight = 0.0f;
	for (auto* cand : candidates)
		totalWeight += cand->chance;

	if (!forceTest)
	{
		float overallChance = (totalWeight > 100.0f) ? 100.0f : totalWeight;
		// 0.01 % resolution so sub-1 % chances stay reachable
		float roll = static_cast<float>((rand() % 10000) + 1) / 100.0f;
		if (roll > overallChance)
		{
			SP_DEBUG_LOG("[Soundpad] Competing reactions rolled %.1f%% > chance %.1f%%; silenced.\n",
				roll, overallChance);
			return false;
		}
	}

	// Weighted pick among candidates
	ReactionItem* selected = nullptr;
	if (candidates.size() == 1 || totalWeight <= 0.0f)
	{
		selected = candidates[0];
	}
	else
	{
		float pick = static_cast<float>((rand() % 10000) + 1) / 10000.0f * totalWeight;
		float acc = 0.0f;
		for (auto* cand : candidates)
		{
			acc += cand->chance;
			if (pick <= acc)
			{
				selected = cand;
				break;
			}
		}
		if (!selected)
			selected = candidates.back();
	}

	g_lastGlobalReactionTime = nowTime;
	selected->lastTriggeredTime = nowTime;

	cvar_t* pLoopback = gEngfuncs.pfnGetCvarPointer ? gEngfuncs.pfnGetCvarPointer("voice_loopback") : nullptr;
	bool hasLoopback = pLoopback && pLoopback->value > 0.0f;

	// Execute Action: Sound / Macro / TTS
	if (selected->soundPath.rfind("tts:", 0) == 0)
	{
		std::string phrase = selected->soundPath.substr(4);
		std::string formatted = CGameState::Get().FormatTokens(phrase);
		float vol = (selected->volume > 0.0f) ? selected->volume : (sp_tts_volume ? sp_tts_volume->value : 1.0f);
		float pitch = (selected->pitch > 0.0f) ? selected->pitch : (sp_tts_pitch ? sp_tts_pitch->value : 1.0f);
		bool jitter = (!sp_tts_jitter || sp_tts_jitter->value > 0.0f);
		bool whisper = (sp_tts_whisper && sp_tts_whisper->value > 0.0f);
		bool randStyle = (sp_tts_random_style && sp_tts_random_style->value > 0.0f) || CSoundpadConfig::Get().GetTtsRandomStyle();

		// Reaction TTS fires immediately as an event reaction
		SoundEngine_SpeakTTS(formatted.c_str(), vol, pitch, jitter, whisper, false, true, randStyle, false);
		if (forceTest || SP_IsDebug())
			SafeConPrintf("[Soundpad] Reaction TTS fired: '%s' (spoken: '%s')%s\n",
				phrase.c_str(), formatted.c_str(), forceTest ? " [FORCE TEST]" : "");
		return true;
	}

	if (selected->soundPath.rfind("seq:", 0) == 0 || selected->soundPath.rfind("macro:", 0) == 0)
	{
		std::string seqName = selected->soundPath;
		if (seqName.rfind("seq:", 0) == 0) seqName = seqName.substr(4);
		else if (seqName.rfind("macro:", 0) == 0) seqName = seqName.substr(6);
		const auto* seq = CSoundpadConfig::Get().FindSequence(seqName);
		if (seq)
		{
			SoundEngine_PlaySequence(*seq);
			if (forceTest || SP_IsDebug())
				SafeConPrintf("[Soundpad] Reaction macro fired: '%s' (filter='%s', chance=%.1f%%)%s\n",
					seqName.c_str(), selected->condition.c_str(), selected->chance, forceTest ? " [FORCE TEST]" : "");
			return true;
		}
	}

	SoundPlayParams params;
	params.volume = selected->volume;
	params.pitch = selected->pitch;
	params.fadeInSec = selected->fadeInSec;
	params.fadeOutSec = selected->fadeOutSec;
	params.fadeCurve = static_cast<FadeCurveType>(selected->fadeCurve);
	params.fxReverb = selected->fxReverb;
	params.fxZigzag = selected->fxZigzag;

	// Broadcast over voice stream (monitor audio will be synchronized automatically if voice_loopback is off)
	SoundEngine_PlayEx(selected->soundPath.c_str(), params);

	if (forceTest || SP_IsDebug())
		SafeConPrintf("[Soundpad] Reaction fired: '%s' (filter='%s', chance=%.1f%%)%s\n",
			selected->soundPath.c_str(), selected->condition.c_str(), selected->chance, forceTest ? " [FORCE TEST]" : "");
	return true;
}

void ReactionManager_TriggerManual(const char* eventName, const char* condition, bool forceTest)
{
	ReactionEventType ev = ReactionEventType::Death;
	if (_stricmp(eventName, "damage") == 0) ev = ReactionEventType::Damage;
	else if (_stricmp(eventName, "kill") == 0) ev = ReactionEventType::Kill;
	else if (_stricmp(eventName, "chat") == 0) ev = ReactionEventType::Chat;
	else if (_stricmp(eventName, "command") == 0 || _stricmp(eventName, "cmd") == 0) ev = ReactionEventType::Command;
	else if (_stricmp(eventName, "teamdeath") == 0 || _stricmp(eventName, "otherdeath") == 0) ev = ReactionEventType::OtherDeath;

	CheckAndFireReaction(ev, condition ? condition : "", forceTest);
}

void SoundEngine_PlaySequence(const SequenceItem& seq)
{
	if (seq.steps.empty())
		return;

	bool first = true;
	for (const auto& step : seq.steps)
	{
		float curPitch = step.pitch;
		for (int rep = 0; rep < step.repeats; ++rep)
		{
			SoundPlayParams p;
			p.volume = step.volume;
			p.pitch = curPitch;
			p.enqueue = !first;
			p.startSec = step.startSec;
			p.endSec = step.endSec;
			p.fadeInSec = step.fadeInSec;
			p.fadeOutSec = step.fadeOutSec;
			p.fadeCurve = static_cast<FadeCurveType>(step.fadeCurve);
			p.fxReverb = step.fxReverb;
			p.fxZigzag = step.fxZigzag;

			SoundEngine_PlayEx(step.soundPath.c_str(), p);
			first = false;
			curPitch += step.pitchStep;
		}
	}
}

static pfnUserMsgHook g_pfnUserMsg_Damage = nullptr;
static pfnUserMsgHook g_pfnUserMsg_DeathMsg = nullptr;
static pfnUserMsgHook g_pfnUserMsg_SayText = nullptr;
static pfnUserMsgHook g_pfnUserMsg_TextMsg = nullptr;
static pfnUserMsgHook g_pfnUserMsg_Health = nullptr;

static void SP_Medic_f();

static int __MsgFunc_Health(const char* pszName, int iSize, void* pbuf)
{
	if (pbuf && iSize > 0)
	{
		SafeBufferReader reader(pbuf, iSize);
		int hp = (iSize >= 2) ? reader.ReadShort() : reader.ReadByte();

		int oldHp = CGameState::Get().GetHealth();
		CGameState::Get().SetHealth(hp);

		// If auto-medic is enabled, trigger callout if player took damage and health dropped
		if (sp_medic_auto && sp_medic_auto->value > 0.0f && hp > 0 && oldHp > hp)
		{
			SP_Medic_f();
		}
	}
	return g_pfnUserMsg_Health ? g_pfnUserMsg_Health(pszName, iSize, pbuf) : 1;
}

static int __MsgFunc_Damage(const char* pszName, int iSize, void* pbuf)
{
	if (pbuf && iSize >= 6)
	{
		SafeBufferReader reader(pbuf, iSize);
		int armor = reader.ReadByte();
		int damage = reader.ReadByte();
		uint32_t bits = static_cast<uint32_t>(reader.ReadLong());
		g_lastDamageBits = bits;
		g_lastDamageTaken = damage;
		// Record QPC time so stale damageBits can be discarded in CheckAndFireReaction
		{
			LARGE_INTEGER _c = {}, _f = {};
			if (QueryPerformanceCounter(&_c) && QueryPerformanceFrequency(&_f) && _f.QuadPart > 0)
				g_lastDamageTime = static_cast<double>(_c.QuadPart) / static_cast<double>(_f.QuadPart);
		}

		cl_entity_t* local = gEngfuncs.GetLocalPlayer ? gEngfuncs.GetLocalPlayer() : nullptr;
		int hp = (local && local->player != 0) ? static_cast<int>(local->curstate.health) : 100;
		CGameState::Get().SetHealth(hp);
		CGameState::Get().SetArmor(armor);
		CGameState::Get().RecordDamage(damage, bits, "world");

		if (damage > 0)
		{
			CheckAndFireReaction(ReactionEventType::Damage, "");
		}
	}
	return g_pfnUserMsg_Damage ? g_pfnUserMsg_Damage(pszName, iSize, pbuf) : 1;
}

static std::string StripColorCodes(const std::string& input)
{
	std::string res;
	res.reserve(input.size());
	for (unsigned char c : input)
	{
		// 0x01: normal/yellow, 0x02: player/team color, 0x03: team color, 0x04: green, 0x07: custom RGB
		if (c == 0x01 || c == 0x02 || c == 0x03 || c == 0x04 || c == 0x07)
			continue;
		if (c < 0x20 && c != '\t' && c != '\r' && c != '\n')
			continue;
		res.push_back(static_cast<char>(c));
	}
	return res;
}

struct RecentChatEntry
{
	std::string key;
	std::chrono::steady_clock::time_point when;
};

static std::deque<RecentChatEntry> s_recentChats;

// Local listen servers can deliver the same chat twice (SayText plus a relayed
// TextMsg). Treat identical sender+text within one second as one message.
static bool IsDuplicateChatMessage(const std::string& sender, const std::string& body)
{
	if (body.empty())
		return false;

	std::string key = StripColorCodes(sender);
	key += '\x1F';
	key += body;

	const auto now = std::chrono::steady_clock::now();
	while (!s_recentChats.empty() &&
		now - s_recentChats.front().when > std::chrono::milliseconds(1000))
	{
		s_recentChats.pop_front();
	}

	for (const auto& entry : s_recentChats)
	{
		if (entry.key == key)
			return true;
	}

	s_recentChats.push_back({ std::move(key), now });
	if (s_recentChats.size() > 16)
		s_recentChats.pop_front();
	return false;
}

static std::string FormatEngineString(const std::string& fmt, const std::vector<std::string>& args){
	if (args.empty())
		return fmt;

	std::string result = fmt;
	// Replace %s1, %s2, %s3, %s4
	for (size_t i = 0; i < args.size(); ++i)
	{
		char placeholder[8];
		snprintf(placeholder, sizeof(placeholder), "%%s%zu", i + 1);
		size_t pos = 0;
		while ((pos = result.find(placeholder, pos)) != std::string::npos)
		{
			result.replace(pos, strlen(placeholder), args[i]);
			pos += args[i].length();
		}
	}
	// Replace positional %s
	for (size_t i = 0; i < args.size(); ++i)
	{
		size_t pos = result.find("%s");
		if (pos == std::string::npos)
			break;
		result.replace(pos, 2, args[i]);
	}

	// If fmt started with '#' and had no placeholders replaced, append the arguments
	if (!args.empty() && (fmt[0] == '#' || result == fmt))
	{
		for (const auto& a : args)
		{
			if (!result.empty()) result += " ";
			result += a;
		}
	}

	return result;
}

static bool TryParsePitchToken(const std::string& token, float& outPitch)
{
	if (token.empty())
		return false;

	std::string s = token;
	// Optional %...% or :... wrapper if someone happened to write it
	if (s.size() >= 3 && s.front() == '%' && s.back() == '%')
		s = s.substr(1, s.size() - 2);
	else if (s.front() == ':')
		s = s.substr(1);

	if (s.empty())
		return false;

	char* endp = nullptr;
	double val = std::strtod(s.c_str(), &endp);
	if (!endp || *endp != '\0')
		return false;

	if (val <= 0.0)
		return false;

	bool hasDot = (s.find('.') != std::string::npos);
	if (hasDot)
	{
		// Fractional multiplier: 0.5, 0.8, 1.0, 1.25, 1.5, 2.0
		outPitch = static_cast<float>(val);
	}
	else
	{
		// Integer percentage: 50 -> 0.5, 80 -> 0.8, 100 -> 1.0, 120 -> 1.2, 150 -> 1.5
		if (val <= 3.0)
			outPitch = static_cast<float>(val); // 1 -> 1.0, 2 -> 2.0
		else
			outPitch = static_cast<float>(val / 100.0);
	}

	outPitch = std::clamp(outPitch, 0.25f, 2.5f);
	return true;
}

static void CheckKillOrDeathMessage(const std::string& rawMsg)
{
	std::string msg = StripColorCodes(rawMsg);
	size_t first = msg.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return;
	size_t last = msg.find_last_not_of(" \t\r\n");
	msg = msg.substr(first, (last - first + 1));

	if (msg.empty())
		return;

	// Check if this server text announces a kill or death involving the local player (e.g. in Sven Co-op)
	cl_entity_t* local = gEngfuncs.GetLocalPlayer ? gEngfuncs.GetLocalPlayer() : nullptr;
	if (local && local->player != 0 && local->index > 0 && gEngfuncs.pfnGetPlayerInfo)
	{
		hud_player_info_t info = {};
		gEngfuncs.pfnGetPlayerInfo(local->index, &info);
		if (info.name && info.name[0])
		{
			std::string localName = info.name;
			CGameState::Get().SetPlayerName(localName);

			std::string msgLower = msg;
			std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);

			// Passive voice check: "<Victim> was killed by <Killer>" or "<Victim> was slain by <Killer>"
			size_t passivePos = msgLower.find(" was killed by ");
			size_t passiveLen = 15;
			if (passivePos == std::string::npos)
			{
				passivePos = msgLower.find(" was slain by ");
				passiveLen = 14;
			}

			if (passivePos != std::string::npos)
			{
				std::string victimPart = msg.substr(0, passivePos);
				std::string killerPart = msg.substr(passivePos + passiveLen);

				size_t s = victimPart.find_first_not_of(" \t:.-");
				if (s != std::string::npos) victimPart = victimPart.substr(s);
				size_t e = victimPart.find_last_not_of(" \t\r\n.!");
				if (e != std::string::npos) victimPart = victimPart.substr(0, e + 1);

				s = killerPart.find_first_not_of(" \t:.-");
				if (s != std::string::npos) killerPart = killerPart.substr(s);
				e = killerPart.find_last_not_of(" \t\r\n.!");
				if (e != std::string::npos) killerPart = killerPart.substr(0, e + 1);

				if (MatchWordOrExact(killerPart, localName))
				{
					// Local player is the killer
					CGameState::Get().RecordKill(victimPart, false);
					CheckAndFireReaction(ReactionEventType::Kill, victimPart);
				}
				else if (MatchWordOrExact(victimPart, localName))
				{
					// Local player was killed
					bool isSuicide = MatchWordOrExact(killerPart, localName);
					CGameState::Get().RecordDeath(killerPart, isSuicide, false);
					CheckAndFireReaction(ReactionEventType::Death, killerPart);
				}
				else
				{
					// Another player died
					CheckAndFireReaction(ReactionEventType::OtherDeath, victimPart);
				}
			}
			else
			{
				// Active voice check: "<Killer> killed <Victim>" or "<Killer> slew <Victim>"
				size_t activePos = msgLower.find(" killed ");
				size_t activeLen = 8;
				if (activePos == std::string::npos)
				{
					activePos = msgLower.find(" slew ");
					activeLen = 6;
				}

				if (activePos != std::string::npos)
				{
					std::string killerPart = msg.substr(0, activePos);
					std::string victimPart = msg.substr(activePos + activeLen);

					size_t s = killerPart.find_first_not_of(" \t:.-");
					if (s != std::string::npos) killerPart = killerPart.substr(s);
					size_t e = killerPart.find_last_not_of(" \t\r\n.!");
					if (e != std::string::npos) killerPart = killerPart.substr(0, e + 1);

					s = victimPart.find_first_not_of(" \t:.-");
					if (s != std::string::npos) victimPart = victimPart.substr(s);
					std::string vLower = victimPart;
					std::transform(vLower.begin(), vLower.end(), vLower.begin(), ::tolower);
					if (vLower.rfind("an ", 0) == 0) victimPart = victimPart.substr(3);
					else if (vLower.rfind("a ", 0) == 0) victimPart = victimPart.substr(2);
					else if (vLower.rfind("the ", 0) == 0) victimPart = victimPart.substr(4);

					e = victimPart.find_last_not_of(" \t\r\n.!");
					if (e != std::string::npos) victimPart = victimPart.substr(0, e + 1);

					if (MatchWordOrExact(killerPart, localName))
					{
						// Local player is killer
						CGameState::Get().RecordKill(victimPart, false);
						CheckAndFireReaction(ReactionEventType::Kill, victimPart);
					}
					else if (MatchWordOrExact(victimPart, localName))
					{
						// Local player was killed
						bool isSuicide = MatchWordOrExact(killerPart, localName);
						CGameState::Get().RecordDeath(killerPart, isSuicide, false);
						CheckAndFireReaction(ReactionEventType::Death, killerPart);
					}
					else
					{
						// Another player died
						CheckAndFireReaction(ReactionEventType::OtherDeath, victimPart);
					}
				}
				else
				{
					// "<Player> died" check
					size_t diedPos = msgLower.find(" died");
					if (diedPos != std::string::npos)
					{
						std::string deadPart = msg.substr(0, diedPos);
						size_t s = deadPart.find_first_not_of(" \t:.-");
						if (s != std::string::npos) deadPart = deadPart.substr(s);
						size_t e = deadPart.find_last_not_of(" \t\r\n.!");
						if (e != std::string::npos) deadPart = deadPart.substr(0, e + 1);

						if (MatchWordOrExact(deadPart, localName))
						{
							CGameState::Get().RecordDeath("world", false, false);
							CheckAndFireReaction(ReactionEventType::Death, "world");
						}
						else if (!deadPart.empty())
						{
							CheckAndFireReaction(ReactionEventType::OtherDeath, deadPart);
						}
					}
				}
			}
		}
	}
}

static void DispatchChatMessage(const std::string& rawMsg, int clientIndex = -1)
{
	std::string msg = StripColorCodes(rawMsg);
	size_t first = msg.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return;
	size_t last = msg.find_last_not_of(" \t\r\n");
	msg = msg.substr(first, (last - first + 1));

	if (msg.empty())
		return;

	// Chat message parsing
	size_t colon = msg.find(": ");
	std::string chatBody;
	bool hasColon = (colon != std::string::npos);

	// Server message filtering:
	// 1. If sp_allow_server is disabled (0), server prints must never trigger soundpad.
	// 2. Server prints without a player colon (": ") are system banners, plugin help, etc. - never player speech.
	if (clientIndex <= 0)
	{
		if (sp_allow_server && sp_allow_server->value <= 0.0f)
			return;

		if (!hasColon)
			return;
	}

	if (hasColon)
	{
		chatBody = msg.substr(colon + 2);
	}
	else
	{
		chatBody = msg;
	}

	while (!chatBody.empty() && (chatBody[0] == ' ' || chatBody[0] == '\t'))
		chatBody.erase(0, 1);

	// Sender identity lookup from engine
	hud_player_info_t senderInfo = {};
	uint64_t senderSteamID = 0;
	std::string senderName = "Server/Console";
	bool isLocalSender = false;

	if (clientIndex >= 1 && clientIndex <= 32 && gEngfuncs.pfnGetPlayerInfo)
	{
		gEngfuncs.pfnGetPlayerInfo(clientIndex, &senderInfo);
		if (senderInfo.name && senderInfo.name[0])
			senderName = senderInfo.name;
		senderSteamID = GetPlayerSteamID(clientIndex);
		isLocalSender = (senderInfo.thisplayer != 0);
	}
	else if (hasColon && gEngfuncs.pfnGetPlayerInfo)
	{
		// Try resolving sender nickname for server-relayed TextMsg with "Name: text"
		std::string candidateName = msg.substr(0, colon);
		size_t s = candidateName.find_first_not_of(" \t");
		if (s != std::string::npos) candidateName = candidateName.substr(s);
		size_t e = candidateName.find_last_not_of(" \t");
		if (e != std::string::npos) candidateName = candidateName.substr(0, e + 1);

		if (!candidateName.empty())
		{
			for (int slot = 1; slot <= 32; ++slot)
			{
				hud_player_info_t pi = {};
				gEngfuncs.pfnGetPlayerInfo(slot, &pi);
				if (pi.name && _stricmp(pi.name, candidateName.c_str()) == 0)
				{
					senderName = pi.name;
					senderSteamID = GetPlayerSteamID(slot);
					isLocalSender = (pi.thisplayer != 0);
					break;
				}
			}
		}
	}
	// NOTE: do not retry pfnGetPlayerInfo(clientIndex) here. For server-relayed
	// TextMsg the index is 0/-1; that call returns no sender and used to overwrite
	// the name-matched SteamID resolved above with 0, breaking ban checks.

	// Clean and trim chat body for command evaluation
	std::string trimmedBody = chatBody;
	size_t tStart = trimmedBody.find_first_not_of(" \t\r\n");
	if (tStart != std::string::npos)
		trimmedBody = trimmedBody.substr(tStart);
	else
		trimmedBody.clear();
	size_t tEnd = trimmedBody.find_last_not_of(" \t\r\n");
	if (tEnd != std::string::npos)
		trimmedBody = trimmedBody.substr(0, tEnd + 1);

	if (IsDuplicateChatMessage(senderName, trimmedBody))
	{
		DebuLog("DispatchChatMessage: duplicate chat suppressed from '%s': '%s'",
			senderName.c_str(), trimmedBody.c_str());
		return;
	}

	// 1. Check for Stop Playback command: empty dot (".") or .stop / .s / !stop / !s
	if (trimmedBody == "." || trimmedBody == ".stop" || trimmedBody == ".s" ||
	    trimmedBody == "!stop" || trimmedBody == "!s")
	{
		if (CSoundpadConfig::Get().IsDotStopEnabled())
		{
			if (CSoundpadConfig::Get().IsPlayerBanned(senderSteamID, senderName))
			{
				if (gEngfuncs.Con_Printf)
				{
					SafeConPrintf("[Soundpad] Dot-stop ignored from banned player %s (#%d, SteamID: %llu)\n",
						senderName.c_str(), clientIndex, static_cast<unsigned long long>(senderSteamID));
				}
				return;
			}

			if (gEngfuncs.Con_Printf)
			{
				SafeConPrintf("[Soundpad] Playback stopped by chat command '%s' from %s (#%d, SteamID: %llu)\n",
					trimmedBody.c_str(), senderName.c_str(), clientIndex, static_cast<unsigned long long>(senderSteamID));
			}
			SoundEngine_Stop();
			return;
		}
	}

	// 2. Check if player is banned - banned players cannot trigger aliases, !tts, or reactions
	if (CSoundpadConfig::Get().IsPlayerBanned(senderSteamID, senderName))
	{
		return;
	}

	// 3. Check for Direct Chat Trigger (from main sound list) or legacy Chat Alias
	SoundItem* triggerSound = nullptr;
	ChatAliasItem* alias = nullptr;
	float customPitch = -1.0f;

	// Exact match first (prevents breaking triggers with numbers like "pi 3.14")
	triggerSound = CSoundpadConfig::Get().FindSoundByChatTrigger(trimmedBody);
	if (!triggerSound)
		alias = CSoundpadConfig::Get().FindChatAlias(trimmedBody);

	// If not an exact match, check for trailing pitch (e.g. "double kill 0.8", "sound %0.8%", "sound 80")
	if (!triggerSound && !alias)
	{
		size_t lastSpace = trimmedBody.find_last_of(" \t");
		if (lastSpace != std::string::npos)
		{
			std::string lastToken = trimmedBody.substr(lastSpace + 1);
			std::string prefix = trimmedBody.substr(0, lastSpace);
			size_t pEnd = prefix.find_last_not_of(" \t");
			if (pEnd != std::string::npos) prefix = prefix.substr(0, pEnd + 1);

			float p = 0.0f;
			if (TryParsePitchToken(lastToken, p))
			{
				triggerSound = CSoundpadConfig::Get().FindSoundByChatTrigger(prefix);
				if (!triggerSound)
					alias = CSoundpadConfig::Get().FindChatAlias(prefix);

				if (triggerSound || alias)
				{
					customPitch = p;
				}
			}
		}

		// Also check colon syntax (e.g. "double kill:0.8")
		if (!triggerSound && !alias)
		{
			size_t colon = trimmedBody.rfind(':');
			if (colon != std::string::npos && colon > 0)
			{
				std::string prefix = trimmedBody.substr(0, colon);
				std::string suffix = trimmedBody.substr(colon + 1);
				size_t pEnd = prefix.find_last_not_of(" \t");
				if (pEnd != std::string::npos) prefix = prefix.substr(0, pEnd + 1);

				float p = 0.0f;
				if (TryParsePitchToken(suffix, p))
				{
					triggerSound = CSoundpadConfig::Get().FindSoundByChatTrigger(prefix);
					if (!triggerSound)
						alias = CSoundpadConfig::Get().FindChatAlias(prefix);

					if (triggerSound || alias)
					{
						customPitch = p;
					}
				}
			}
		}
	}

	if (triggerSound)
	{
		double nowTime = 0.0;
		LARGE_INTEGER counter = {}, freq = {};
		if (QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&freq) && freq.QuadPart > 0)
			nowTime = static_cast<double>(counter.QuadPart) / static_cast<double>(freq.QuadPart);

		constexpr double kTriggerCooldown = 2.0; // 2-second anti-spam cooldown
		if (nowTime - triggerSound->lastTriggeredTime >= kTriggerCooldown)
		{
			triggerSound->lastTriggeredTime = nowTime;
			float effPitch = (customPitch > 0.0f) ? customPitch : triggerSound->pitch;
			SP_DEBUG_LOG("[Soundpad] Chat trigger '%s' triggered '%s' by %s (pitch: %.2f)\n",
				triggerSound->chatTrigger.c_str(), triggerSound->path.c_str(), senderName.c_str(), effPitch);

			SoundPlayParams p;
			p.volume = triggerSound->volume;
			p.pitch = effPitch;
			// Chat triggers queue behind whatever is already playing. Only an explicit
			// UI/console "Play" (enqueue=false) may cut the Soundpad client's queue.
			p.enqueue = true;
			SoundEngine_PlayEx(triggerSound->path.c_str(), p);
			return;
		}
		return;
	}

	if (alias)
	{
		double nowTime = 0.0;
		LARGE_INTEGER counter = {}, freq = {};
		if (QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&freq) && freq.QuadPart > 0)
			nowTime = static_cast<double>(counter.QuadPart) / static_cast<double>(freq.QuadPart);

		if (nowTime - alias->lastTriggeredTime >= static_cast<double>(alias->cooldown))
		{
			alias->lastTriggeredTime = nowTime;
			float effPitch = (customPitch > 0.0f) ? customPitch : alias->pitch;
			SP_DEBUG_LOG("[Soundpad] Chat alias '%s' triggered by %s (playing '%s', pitch: %.2f)\n",
				alias->command.c_str(), senderName.c_str(), alias->soundPath.c_str(), effPitch);

			SoundPlayParams p;
			p.volume = alias->volume;
			p.pitch = effPitch; // Play to Voice stream
			// Same rule as chat triggers: aliases append instead of stopping the queue.
			p.enqueue = true;
			SoundEngine_PlayEx(alias->soundPath.c_str(), p);
			return;
		}
		else
		{
			// In cooldown: ignore silently
			return;
		}
	}

	bool randStyle = (sp_tts_random_style && sp_tts_random_style->value > 0.0f) || CSoundpadConfig::Get().GetTtsRandomStyle();

	if (chatBody.rfind("!tts", 0) == 0 || chatBody.rfind("tts", 0) == 0)
	{
		std::string ttsText;
		float customTtsPitch = -1.0f;

		// Check for !tts:<pitch> or tts:<pitch>
		size_t prefixLen = (chatBody[0] == '!') ? 4 : 3;
		if (chatBody.size() > prefixLen && chatBody[prefixLen] == ':')
		{
			size_t spacePos = chatBody.find_first_of(" \t", prefixLen + 1);
			std::string pitchStr = (spacePos != std::string::npos)
				? chatBody.substr(prefixLen + 1, spacePos - (prefixLen + 1))
				: chatBody.substr(prefixLen + 1);
			float p = 0.0f;
			if (TryParsePitchToken(pitchStr, p))
			{
				customTtsPitch = p;
				if (spacePos != std::string::npos)
					ttsText = chatBody.substr(spacePos + 1);
			}
		}

		if (ttsText.empty())
		{
			if (chatBody.rfind("!tts ", 0) == 0) ttsText = chatBody.substr(5);
			else if (chatBody.rfind("tts ", 0) == 0) ttsText = chatBody.substr(4);
		}

		if (!ttsText.empty())
		{
			// Check if first word is pitch (e.g. "!tts 80 hello" or "!tts 0.8 hello")
			size_t firstSp = ttsText.find_first_of(" \t");
			if (firstSp != std::string::npos && customTtsPitch < 0.0f)
			{
				std::string firstWord = ttsText.substr(0, firstSp);
				float p = 0.0f;
				if (TryParsePitchToken(firstWord, p))
				{
					customTtsPitch = p;
					ttsText = ttsText.substr(firstSp + 1);
				}
			}

			// Trim spaces
			size_t firstN = ttsText.find_first_not_of(" \t");
			if (firstN != std::string::npos) ttsText = ttsText.substr(firstN);
			size_t lastN = ttsText.find_last_not_of(" \t");
			if (lastN != std::string::npos) ttsText = ttsText.substr(0, lastN + 1);

			float vol = sp_tts_volume ? sp_tts_volume->value : 1.0f;
			float pitch = (customTtsPitch > 0.0f) ? customTtsPitch : (sp_tts_pitch ? sp_tts_pitch->value : 1.0f);
			bool jitter = (!sp_tts_jitter || sp_tts_jitter->value > 0.0f);
			bool whisper = (sp_tts_whisper && sp_tts_whisper->value > 0.0f);
			std::string formatted = CGameState::Get().FormatTokens(ttsText);
			// Player chat TTS queues cleanly behind other speech
			SoundEngine_SpeakTTS(formatted.c_str(), vol, pitch, jitter, whisper, false, true, randStyle, true);
			return;
		}
	}
	else if (sp_tts_autosay && sp_tts_autosay->value > 0.0f && hasColon)
	{
		float vol = sp_tts_volume ? sp_tts_volume->value : 1.0f;
		float pitch = sp_tts_pitch ? sp_tts_pitch->value : 1.0f;
		bool jitter = (!sp_tts_jitter || sp_tts_jitter->value > 0.0f);
		bool whisper = (sp_tts_whisper && sp_tts_whisper->value > 0.0f);
		std::string formatted = CGameState::Get().FormatTokens(chatBody);
		// Player chat autosay queues cleanly behind other speech
		SoundEngine_SpeakTTS(formatted.c_str(), vol, pitch, jitter, whisper, false, true, randStyle, true);
	}
}

static int __MsgFunc_DeathMsg(const char* pszName, int iSize, void* pbuf)
{
	if (pbuf && iSize >= 2)
	{
		SafeBufferReader reader(pbuf, iSize);
		int killer = reader.ReadByte();
		int victim = reader.ReadByte();
		std::string weapon = reader.ReadString();

		cl_entity_t* local = gEngfuncs.GetLocalPlayer ? gEngfuncs.GetLocalPlayer() : nullptr;
		int localIndex = (local && local->player != 0) ? local->index : 0;

		if (victim == localIndex && localIndex > 0)
		{
			bool isSuicide = (killer == localIndex);
			bool isKillerPlayer = (killer >= 1 && killer <= 32 && killer != localIndex);
			std::string killerIdent = weapon;

			if (isKillerPlayer && gEngfuncs.pfnGetPlayerInfo)
			{
				hud_player_info_t kInfo = {};
				gEngfuncs.pfnGetPlayerInfo(killer, &kInfo);
				if (kInfo.name && kInfo.name[0])
					killerIdent = kInfo.name;
			}

			CGameState::Get().RecordDeath(killerIdent, isSuicide, isKillerPlayer);
			CheckAndFireReaction(ReactionEventType::Death, killerIdent);
		}
		else if (killer == localIndex && killer != victim && localIndex > 0)
		{
			bool isVictimPlayer = (victim >= 1 && victim <= 32);
			std::string victimName = weapon;
			if (isVictimPlayer && gEngfuncs.pfnGetPlayerInfo)
			{
				hud_player_info_t info = {};
				gEngfuncs.pfnGetPlayerInfo(victim, &info);
				if (info.name && info.name[0])
					victimName = info.name;
			}
			CGameState::Get().RecordKill(victimName, isVictimPlayer);
			CheckAndFireReaction(ReactionEventType::Kill, victimName);
		}
		else if (victim >= 1 && victim <= 32 && victim != localIndex && localIndex > 0)
		{
			// Another player died (teammate / other player)
			std::string victimName = weapon;
			if (gEngfuncs.pfnGetPlayerInfo)
			{
				hud_player_info_t vInfo = {};
				gEngfuncs.pfnGetPlayerInfo(victim, &vInfo);
				if (vInfo.name && vInfo.name[0])
					victimName = vInfo.name;
			}
			CheckAndFireReaction(ReactionEventType::OtherDeath, victimName);
		}
	}
	return g_pfnUserMsg_DeathMsg ? g_pfnUserMsg_DeathMsg(pszName, iSize, pbuf) : 1;
}

static int __MsgFunc_SayText(const char* pszName, int iSize, void* pbuf)
{
	if (pbuf && iSize > 1)
	{
		SafeBufferReader reader(pbuf, iSize);
		int client = reader.ReadByte();
		std::string str0 = reader.ReadString();
		std::vector<std::string> args;
		while (reader.HasMore())
		{
			std::string arg = reader.ReadString();
			if (!arg.empty())
				args.push_back(std::move(arg));
		}
		std::string fullMsg = args.empty() ? str0 : FormatEngineString(str0, args);
		// NOTE: CheckKillOrDeathMessage was REMOVED from SayText!
		// SayText is player chat; allowing chat to trigger death events was a critical spoofing/spam vulnerability.
		DispatchChatMessage(fullMsg, client);
	}
	return g_pfnUserMsg_SayText ? g_pfnUserMsg_SayText(pszName, iSize, pbuf) : 1;
}

static int __MsgFunc_TextMsg(const char* pszName, int iSize, void* pbuf)
{
	if (pbuf && iSize > 1)
	{
		SafeBufferReader reader(pbuf, iSize);
		int dest = reader.ReadByte();
		// In GoldSrc: dest 1=notify, 2=console, 3=talk (chat), 4=center.
		std::string str0 = reader.ReadString();
		std::vector<std::string> args;
		while (reader.HasMore())
		{
			std::string arg = reader.ReadString();
			if (!arg.empty())
				args.push_back(std::move(arg));
		}
		std::string fullMsg = args.empty() ? str0 : FormatEngineString(str0, args);

		// All text messages (including console / notify) can be checked for kill/death announcements
		CheckKillOrDeathMessage(fullMsg);

		// ONLY HUD_PRINTTALK (dest == 3) is actual chat displayed in the chat area!
		// HUD_PRINTCONSOLE (dest == 2, plugin help/cvar outputs), HUD_PRINTNOTIFY (1),
		// and HUD_PRINTCENTER (4) are NOT chat and must NEVER trigger chat sounds!
		if (dest == 3)
		{
			DispatchChatMessage(fullMsg, 0);
		}
	}
	return g_pfnUserMsg_TextMsg ? g_pfnUserMsg_TextMsg(pszName, iSize, pbuf) : 1;
}

static void InstallUserMsgHooks()
{
	if (!g_pMetaHookAPI)
		return;

	g_pfnUserMsg_Damage = g_pMetaHookAPI->HookUserMsg("Damage", __MsgFunc_Damage);
	g_pfnUserMsg_DeathMsg = g_pMetaHookAPI->HookUserMsg("DeathMsg", __MsgFunc_DeathMsg);
	g_pfnUserMsg_SayText = g_pMetaHookAPI->HookUserMsg("SayText", __MsgFunc_SayText);
	g_pfnUserMsg_TextMsg = g_pMetaHookAPI->HookUserMsg("TextMsg", __MsgFunc_TextMsg);
	g_pfnUserMsg_Health = g_pMetaHookAPI->HookUserMsg("Health", __MsgFunc_Health);
}

static void RegisterVGUI2ExtensionCallbacks()
{
	IVGUI2Extension* pExtension = VGUI2Extension();
	if (!pExtension)
		return;

	pExtension->RegisterBaseUICallbacks(&s_BaseUICallbacks);
	pExtension->RegisterGameUICallbacks(&g_GameUICallbacks);
	pExtension->RegisterClientVGUICallbacks(&g_ClientVGUICallbacks);
	pExtension->RegisterKeyValuesCallbacks(&s_KeyValuesCallbacks);
	pExtension->RegisterGameUITaskBarCallbacks(&s_TaskBarCallbacks);
}

// Runs on the game thread. The Ldr notification only sets a flag, so VE and
// VGUI2Extension are never entered from inside the loader lock.
static void ProcessPendingModuleNotifications()
{
	if (g_pendingVoiceReload.exchange(false))
	{
		if (VoiceExtension_IsLoaded() && !SoundEngine_IsVoiceBound())
		{
			SoundEngine_Shutdown();
			SoundEngine_Init();
		}
	}

	if (g_pendingVguiReload.exchange(false))
	{
		if (VGUI2Extension_IsLoaded() && !VGUI2Extension())
		{
			VGUI2Extension_Init();
			RegisterVGUI2ExtensionCallbacks();
		}
	}
}

static void (*g_pfnHUD_Init_Original)(void) = nullptr;
void Hook_HUD_Init(void)
{
	ProcessPendingModuleNotifications();

	if (g_pfnHUD_Init_Original)
		g_pfnHUD_Init_Original();

	static bool bUserMsgHooksInstalled = false;
	if (!bUserMsgHooksInstalled && g_pMetaHookAPI)
	{
		bUserMsgHooksInstalled = true;
		InstallUserMsgHooks();
	}

	SoundEngine_Stop();
	CGameState::Get().Reset();
	g_lastDamageBits = 0;
	g_lastDamageTaken = 0;
	g_lastDamageTime = 0.0;

	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->CloseAllSubDialogs();
		g_hSoundpadFrame->RefreshPlayersList();
	}
}

void HookHUD_Frame(double time)
{
	ProcessPendingModuleNotifications();

	if (g_originalHudFrame)
		g_originalHudFrame(time);

	SoundEngine_OnHudFrame(time);
}

void Soundpad_OpenGUI(vgui::Panel* parent, bool fromCommand)
{
	g_bOpenedFromCommand = fromCommand;
	g_bReopenOnGameUI = true;

	if (!vgui::surface() || !vgui::scheme() || !vgui::ipanel())
	{
		if (gEngfuncs.Con_DPrintf)
			gEngfuncs.Con_DPrintf("[Soundpad] ERROR: VGUI2 subsystem is not yet initialized.\n");
		return;
	}

	vgui::VPANEL embedded = vgui::surface()->GetEmbeddedPanel();

	if (!g_hSoundpadFrame)
	{
		g_hSoundpadFrame = new CSoundpadFrame(nullptr);
		if (embedded)
		{
			g_hSoundpadFrame->SetParent(embedded);
		}
	}

	// Always ensure any stale modal lock from subdialogs is cleared on opening
	if (vgui::input() && vgui::ipanel() && g_hSoundpadFrame)
	{
		vgui::VPANEL modal = vgui::input()->GetAppModalSurface();
		if (modal != 0 && (modal == g_hSoundpadFrame->GetVPanel() || vgui::ipanel()->HasParent(modal, g_hSoundpadFrame->GetVPanel())))
		{
			vgui::input()->ReleaseAppModalSurface();
		}
	}

	int sw = 0, sh = 0;
	if (vgui::surface())
		vgui::surface()->GetScreenSize(sw, sh);
	if (sw > 0 && sh > 0)
	{
		int fw = 0, fh = 0;
		g_hSoundpadFrame->GetSize(fw, fh);
		if (fw <= 0 || fh <= 0)
		{
			fw = 780;
			fh = 560;
			g_hSoundpadFrame->SetSize(fw, fh);
		}
		int fx = (sw - fw) / 2;
		int fy = (sh - fh) / 2;
		g_hSoundpadFrame->SetPos(fx, fy);
	}

	g_hSoundpadFrame->SetVisible(true);
	g_hSoundpadFrame->MoveToFront();
	g_hSoundpadFrame->SetKeyBoardInputEnabled(true);
	g_hSoundpadFrame->SetMouseInputEnabled(true);
	g_hSoundpadFrame->RequestFocus();
	g_hSoundpadFrame->Activate();
	g_hSoundpadFrame->RefreshPlayersList();
}

void Soundpad_ToggleGUI()
{
	if (g_hSoundpadFrame && g_hSoundpadFrame->IsVisible())
	{
		g_hSoundpadFrame->SetVisible(false);
		g_bOpenedFromCommand = false;
		g_bReopenOnGameUI = false;
	}
	else
	{
		Soundpad_OpenGUI(nullptr, true);
	}
}

static bool IsIntegerString(const char* s)
{
	if (!s || !*s) return false;
	for (const char* p = s; *p; ++p)
	{
		if (*p < '0' || *p > '9')
			return false;
	}
	return true;
}

static void SP_PlayInternal(bool enqueue)
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		// Play currently selected sound from GUI if available
		if (g_hSoundpadFrame.Get())
		{
			SoundItem* s = g_hSoundpadFrame->GetSelectedSound();
			if (s)
			{
				float vol = s->volume;
				float pitch = s->pitch;
				if (s->isSequence || s->path.rfind("seq:", 0) == 0 || s->path.rfind("macro:", 0) == 0)
				{
					std::string seqName = s->path;
					if (seqName.rfind("seq:", 0) == 0) seqName = seqName.substr(4);
					else if (seqName.rfind("macro:", 0) == 0) seqName = seqName.substr(6);
					const auto* seq = CSoundpadConfig::Get().FindSequence(seqName);
					if (seq) SoundEngine_PlaySequence(*seq);
					else SoundEngine_Play(s->path.c_str(), vol, pitch, enqueue);
				}
				else
				{
					SoundEngine_Play(s->path.c_str(), vol, pitch, enqueue);
				}
				return;
			}
		}

		SafeConPrintf("==================================================================\n");
		SafeConPrintf("Usage: sp_play/sp_queue/spplay/spqueue [path.wav | <#num> | <category> <#num>] [vol] [pitch] [q]\n");
		SafeConPrintf("  <#num>   : Row number (1..N) from GUI table (# column)\n");
		SafeConPrintf("  0 <#num> : Row number from global sound library (all categories)\n");
		SafeConPrintf("  vol      : 0.01..4.0 (default: sound preset or 1.0)\n");
		SafeConPrintf("  pitch    : 0.5..2.0  (default: sound preset or 1.0)\n");
		SafeConPrintf("  q        : force queue (sp_play/sp_queue queue; spplay cuts)\n");
		SafeConPrintf("Tip: Bind keys like 'bind KP_1 \"spplay 1\"' for cut-in; use 'sp_play 1' for queued chat/server triggers.\n");
		SafeConPrintf("==================================================================\n");
		return;
	}

	const char* soundArg = gEngfuncs.Cmd_Argv(1);
	if (!soundArg || !*soundArg)
		return;

	if (_stricmp(soundArg, "stop") == 0)
	{
		SoundEngine_Stop();
		return;
	}

	std::string soundPath;
	float volume = 1.0f;
	float pitch = 1.0f;
	int optArgStart = 2;
	bool matchedNumber = false;

	// Check if soundArg is category + next arg is row number (e.g. "sp_play barney 2" or "sp_play 0 2")
	const char* nextArg = (argc >= 3) ? gEngfuncs.Cmd_Argv(2) : nullptr;
	if (nextArg && IsIntegerString(nextArg))
	{
		int num = atoi(nextArg);
		if (strcmp(soundArg, "0") == 0)
		{
			auto sounds = CSoundpadConfig::Get().GetSoundsForCategory("");
			if (num >= 1 && num <= static_cast<int>(sounds.size()))
			{
				const auto& item = sounds[num - 1];
				soundPath = item.path;
				volume = item.volume;
				pitch = item.pitch;
				optArgStart = 3;
				matchedNumber = true;
			}
		}
		else
		{
			auto sounds = CSoundpadConfig::Get().GetSoundsForCategory(soundArg);
			if (!sounds.empty() && num >= 1 && num <= static_cast<int>(sounds.size()))
			{
				const auto& item = sounds[num - 1];
				soundPath = item.path;
				volume = item.volume;
				pitch = item.pitch;
				optArgStart = 3;
				matchedNumber = true;
			}
		}
	}

	// Check if soundArg itself is a row number (e.g. "sp_play 3")
	if (!matchedNumber && IsIntegerString(soundArg))
	{
		int num = atoi(soundArg);
		SoundItem* s = nullptr;
		if (g_hSoundpadFrame.Get())
		{
			s = g_hSoundpadFrame->GetSoundByRow(num);
		}

		if (s)
		{
			soundPath = s->path;
			volume = s->volume;
			pitch = s->pitch;
			optArgStart = 2;
			matchedNumber = true;
		}
		else
		{
			std::string cat = g_hSoundpadFrame.Get() ? g_hSoundpadFrame->GetSelectedCategory() : "";
			auto sounds = CSoundpadConfig::Get().GetSoundsForCategory(cat);
			if (num >= 1 && num <= static_cast<int>(sounds.size()))
			{
				const auto& item = sounds[num - 1];
				soundPath = item.path;
				volume = item.volume;
				pitch = item.pitch;
				optArgStart = 2;
				matchedNumber = true;
			}
		}
	}

	if (!matchedNumber)
	{
		soundPath = soundArg;
		optArgStart = 2;
	}

	if (argc > optArgStart)
	{
		const char* s = gEngfuncs.Cmd_Argv(optArgStart);
		if (s && *s && _stricmp(s, "q") != 0 && _stricmp(s, "queue") != 0)
		{
			float v = static_cast<float>(atof(s));
			if (v > 0.0f) volume = (std::max)(0.01f, (std::min)(4.0f, v));
		}
	}

	if (argc > optArgStart + 1)
	{
		const char* s = gEngfuncs.Cmd_Argv(optArgStart + 1);
		if (s && *s && _stricmp(s, "q") != 0 && _stricmp(s, "queue") != 0)
		{
			float p = static_cast<float>(atof(s));
			if (p > 0.0f) pitch = (std::max)(0.5f, (std::min)(2.0f, p));
		}
	}


	bool finalEnqueue = enqueue;
	for (int i = 2; i < argc; ++i)
	{
		const char* a = gEngfuncs.Cmd_Argv(i);
		if (a && (_stricmp(a, "q") == 0 || _stricmp(a, "queue") == 0))
			finalEnqueue = true;
	}

	SoundEngine_Play(soundPath.c_str(), volume, pitch, finalEnqueue);
}

static void SP_Play_f()
{
	// sp_play is the server / Chat-Forwarder path: append instead of cutting the
	// Soundpad queue. Deliberate local playback keeps the cut-in behaviour through
	// spplay (and the GUI "Play (Cut)" button).
	SP_PlayInternal(true);
}

static void SP_Queue_f()
{
	SP_PlayInternal(true);
}

static void SP_PlayLocal_f()
{
	SP_PlayInternal(false);
}

static void SP_QueueLocal_f()
{
	SP_PlayInternal(true);
}

static void SP_Stop_f()
{
	SoundEngine_Stop();
	SafeConPrintf("[Soundpad] Playback stopped.\n");
}

static void SP_Panic_f()
{
	DebuLog("SP_Panic_f: emergency panic stop triggered");
	SoundEngine_Stop();
	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->CloseAllSubDialogs();
	}
	SafeConPrintf("[Soundpad] PANIC STOP: all sound playback, queues, and voice streams terminated.\n");
}

static void SP_Preview_f()
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		if (g_hSoundpadFrame.Get())
		{
			SoundItem* s = g_hSoundpadFrame->GetSelectedSound();
			if (s)
			{
				SoundEngine_PreviewLocal(s->path.c_str(), s->volume, s->pitch);
				return;
			}
		}
		SafeConPrintf("Usage: sp_preview <path.wav> [volume] [pitch]\n");
		return;
	}

	const char* path = gEngfuncs.Cmd_Argv(1);
	float volume = 1.0f;
	if (argc >= 3)
	{
		float v = static_cast<float>(atof(gEngfuncs.Cmd_Argv(2)));
		if (v > 0.0f) volume = (std::max)(0.01f, (std::min)(2.0f, v));
	}

	float pitch = 1.0f;
	if (argc >= 4)
	{
		float p = static_cast<float>(atof(gEngfuncs.Cmd_Argv(3)));
		if (p > 0.0f) pitch = (std::max)(0.1f, (std::min)(2.5f, p));
	}

	SoundEngine_PreviewLocal(path, volume, pitch);
}

static void SP_PreviewLocal_f()
{
	SP_Preview_f();
}

static void SP_Random_f()
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	std::string category = (argc >= 2) ? gEngfuncs.Cmd_Argv(1) : "";
	if (category.empty() && g_hSoundpadFrame.Get())
	{
		const std::string& guiCat = g_hSoundpadFrame->GetSelectedCategory();
		if (!guiCat.empty() && _stricmp(guiCat.c_str(), "All") != 0)
			category = guiCat;
	}

	SoundItem item;
	if (category.empty() || _stricmp(category.c_str(), "All") == 0)
	{
		if (CSoundpadConfig::Get().GetRandomSoundGlobal(item))
		{
			if (item.isSequence || item.path.rfind("seq:", 0) == 0 || item.path.rfind("macro:", 0) == 0)
		{
			std::string seqName = item.path;
			if (seqName.rfind("seq:", 0) == 0) seqName = seqName.substr(4);
			else if (seqName.rfind("macro:", 0) == 0) seqName = seqName.substr(6);
			const auto* seq = CSoundpadConfig::Get().FindSequence(seqName);
			if (seq) SoundEngine_PlaySequence(*seq);
			else SoundEngine_Play(item.path.c_str(), item.volume, item.pitch, false);
		}
		else
		{
			SoundEngine_Play(item.path.c_str(), item.volume, item.pitch, false);
		}
			if (g_hSoundpadFrame.Get())
				g_hSoundpadFrame->SelectSoundByPath(item.path);
		}
		else
			SafeConPrintf("[Soundpad] No sounds found in library.\n");
	}
	else
	{
		if (CSoundpadConfig::Get().GetRandomSound(category, item))
		{
			if (item.isSequence || item.path.rfind("seq:", 0) == 0 || item.path.rfind("macro:", 0) == 0)
		{
			std::string seqName = item.path;
			if (seqName.rfind("seq:", 0) == 0) seqName = seqName.substr(4);
			else if (seqName.rfind("macro:", 0) == 0) seqName = seqName.substr(6);
			const auto* seq = CSoundpadConfig::Get().FindSequence(seqName);
			if (seq) SoundEngine_PlaySequence(*seq);
			else SoundEngine_Play(item.path.c_str(), item.volume, item.pitch, false);
		}
		else
		{
			SoundEngine_Play(item.path.c_str(), item.volume, item.pitch, false);
		}
			if (g_hSoundpadFrame.Get())
				g_hSoundpadFrame->SelectSoundByPath(item.path);
		}
		else
			SafeConPrintf("[Soundpad] No sounds found in category '%s'.\n", category.c_str());
	}
}

static void SP_RandomLocal_f()
{
	SP_Random_f();
}

static void SP_Next_f()
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	std::string category = (argc >= 2) ? gEngfuncs.Cmd_Argv(1) : "";
	if (category.empty() && g_hSoundpadFrame.Get())
	{
		const std::string& guiCat = g_hSoundpadFrame->GetSelectedCategory();
		if (!guiCat.empty() && _stricmp(guiCat.c_str(), "All") != 0)
			category = guiCat;
	}

	PlaybackStatus st = SoundEngine_GetStatus();
	std::string currentSound = st.currentSound;
	if (currentSound.empty() && g_hSoundpadFrame.Get())
	{
		SoundItem* sel = g_hSoundpadFrame->GetSelectedSound();
		if (sel) currentSound = sel->path;
	}

	SoundItem item;
	if (CSoundpadConfig::Get().GetNextSound(category, currentSound, item))
	{
		if (item.isSequence || item.path.rfind("seq:", 0) == 0 || item.path.rfind("macro:", 0) == 0)
		{
			std::string seqName = item.path;
			if (seqName.rfind("seq:", 0) == 0) seqName = seqName.substr(4);
			else if (seqName.rfind("macro:", 0) == 0) seqName = seqName.substr(6);
			const auto* seq = CSoundpadConfig::Get().FindSequence(seqName);
			if (seq) SoundEngine_PlaySequence(*seq);
			else SoundEngine_Play(item.path.c_str(), item.volume, item.pitch, false);
		}
		else
		{
			SoundEngine_Play(item.path.c_str(), item.volume, item.pitch, false);
		}
		if (g_hSoundpadFrame.Get())
			g_hSoundpadFrame->SelectSoundByPath(item.path);
	}
	else
	{
		SafeConPrintf("[Soundpad] No sounds found to navigate.\n");
	}
}

static void SP_NextLocal_f()
{
	SP_Next_f();
}

static void SP_Prev_f()
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	std::string category = (argc >= 2) ? gEngfuncs.Cmd_Argv(1) : "";
	if (category.empty() && g_hSoundpadFrame.Get())
	{
		const std::string& guiCat = g_hSoundpadFrame->GetSelectedCategory();
		if (!guiCat.empty() && _stricmp(guiCat.c_str(), "All") != 0)
			category = guiCat;
	}

	PlaybackStatus st = SoundEngine_GetStatus();
	std::string currentSound = st.currentSound;
	if (currentSound.empty() && g_hSoundpadFrame.Get())
	{
		SoundItem* sel = g_hSoundpadFrame->GetSelectedSound();
		if (sel) currentSound = sel->path;
	}

	SoundItem item;
	if (CSoundpadConfig::Get().GetPrevSound(category, currentSound, item))
	{
		if (item.isSequence || item.path.rfind("seq:", 0) == 0 || item.path.rfind("macro:", 0) == 0)
		{
			std::string seqName = item.path;
			if (seqName.rfind("seq:", 0) == 0) seqName = seqName.substr(4);
			else if (seqName.rfind("macro:", 0) == 0) seqName = seqName.substr(6);
			const auto* seq = CSoundpadConfig::Get().FindSequence(seqName);
			if (seq) SoundEngine_PlaySequence(*seq);
			else SoundEngine_Play(item.path.c_str(), item.volume, item.pitch, false);
		}
		else
		{
			SoundEngine_Play(item.path.c_str(), item.volume, item.pitch, false);
		}
		if (g_hSoundpadFrame.Get())
			g_hSoundpadFrame->SelectSoundByPath(item.path);
	}
	else
	{
		SafeConPrintf("[Soundpad] No sounds found to navigate.\n");
	}
}

static void SP_PrevLocal_f()
{
	SP_Prev_f();
}

static void SP_Dump_f()
{
	SoundEngine_DumpReport();
}

static void SP_Add_f()
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: sp_add <path.wav> [category] [title]\n");
		return;
	}

	const char* path = gEngfuncs.Cmd_Argv(1);
	std::string category = (argc >= 3) ? gEngfuncs.Cmd_Argv(2) : "Custom";
	std::string title = (argc >= 4) ? gEngfuncs.Cmd_Argv(3) : "";

	CSoundpadFrame::AddSoundFile(path, category);

	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->RefreshCategories();
		g_hSoundpadFrame->RefreshSoundList();
	}
}

static void SP_AddFolder_f()
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: sp_addfolder <folder_path> [category]\n");
		return;
	}

	const char* folder = gEngfuncs.Cmd_Argv(1);
	std::string category = (argc >= 3) ? gEngfuncs.Cmd_Argv(2) : "";

	CSoundpadFrame::AddSoundFolder(folder, category);

	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->RefreshCategories();
		g_hSoundpadFrame->RefreshSoundList();
	}
}

static void SP_Save_f()
{
	CSoundpadConfig::Get().Save(true);
}

static void SP_SaveLocal_f()
{
	SP_Save_f();
}

static void SP_Reload_f()
{
	CSoundpadConfig::Get().Reload();
	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->RefreshCategories();
		g_hSoundpadFrame->RefreshSoundList();
	}
	SafeConPrintf("[Soundpad] Configuration reloaded from soundpad.vdf.\n");
}

static void SP_Trigger_f()
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: sp_trigger <death|damage|kill|chat|cmd|teamdeath> [param/condition]\n");
		return;
	}
	const char* ev = gEngfuncs.Cmd_Argv(1);
	const char* param = (argc >= 3) ? gEngfuncs.Cmd_Argv(2) : "";
	// From console, do not force-test: respect reaction cooldowns and chance rolls to prevent command spam
	ReactionManager_TriggerManual(ev, param, false);
}

static void SP_SeqPlay_f()
{
	const int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: sp_seq_play <sequence_name>\n");
		return;
	}
	const char* name = gEngfuncs.Cmd_Argv(1);
	const auto& seqs = CSoundpadConfig::Get().GetSequences();
	for (const auto& s : seqs)
	{
		if (_stricmp(s.name.c_str(), name) == 0)
		{
			SoundEngine_PlaySequence(s);
			SafeConPrintf("[Soundpad] Playing sequence '%s' (%u steps)\n", s.name.c_str(), static_cast<unsigned>(s.steps.size()));
			return;
		}
	}
	SafeConPrintf("[Soundpad] Sequence '%s' not found.\n", name);
}

static void SP_AimTTS_f()
{
	cl_entity_t* local = gEngfuncs.GetLocalPlayer ? gEngfuncs.GetLocalPlayer() : nullptr;
	if (!local || local->player == 0 || local->index <= 0) return;

	float forward[3] = {}, right[3] = {}, up[3] = {};
	gEngfuncs.pfnAngleVectors(local->angles, forward, right, up);

	float start[3] = { local->origin[0], local->origin[1], local->origin[2] + 17.0f };
	float end[3] = {
		start[0] + forward[0] * 2048.0f,
		start[1] + forward[1] * 2048.0f,
		start[2] + forward[2] * 2048.0f
	};

	pmtrace_t tr = {};
	if (gEngfuncs.pEventAPI && gEngfuncs.pEventAPI->EV_PlayerTrace)
	{
		gEngfuncs.pEventAPI->EV_SetTraceHull(2);
		gEngfuncs.pEventAPI->EV_PlayerTrace(start, end, 0, local->index, &tr);
	}

	std::string targetName = "nobody";
	const int maxClients = (gEngfuncs.GetMaxClients && gEngfuncs.GetMaxClients() > 0) ? gEngfuncs.GetMaxClients() : 0;

	if (tr.ent > 0 && tr.ent <= maxClients)
	{
		hud_player_info_t info = {};
		if (gEngfuncs.pfnGetPlayerInfo)
		{
			gEngfuncs.pfnGetPlayerInfo(tr.ent, &info);
			if (info.name && info.name[0])
				targetName = info.name;
		}
	}
	else if (tr.ent > maxClients)
	{
		cl_entity_t* ent = gEngfuncs.GetEntityByIndex ? gEngfuncs.GetEntityByIndex(tr.ent) : nullptr;
		if (ent && ent->model && ent->model->name)
		{
			targetName = ent->model->name;
			size_t slash = targetName.find_last_of("/\\");
			if (slash != std::string::npos)
				targetName = targetName.substr(slash + 1);
			size_t dot = targetName.find_last_of('.');
			if (dot != std::string::npos)
				targetName = targetName.substr(0, dot);
		}
	}

	SafeConPrintf("[Soundpad] Aim target: %s\n", targetName.c_str());

	float vol = sp_tts_volume ? sp_tts_volume->value : 1.0f;
	float pitch = sp_tts_pitch ? sp_tts_pitch->value : 1.0f;
	bool jitter = (!sp_tts_jitter || sp_tts_jitter->value > 0.0f);
	bool whisper = (sp_tts_whisper && sp_tts_whisper->value > 0.0f);
	bool randStyle = (sp_tts_random_style && sp_tts_random_style->value > 0.0f) || CSoundpadConfig::Get().GetTtsRandomStyle();
	char aimPhrase[128] = {};
	snprintf(aimPhrase, sizeof(aimPhrase), "Target %s", targetName.c_str());
	SoundEngine_SpeakTTS(aimPhrase, vol, pitch, jitter, whisper, false, true, randStyle);
}

static void SP_SpeakTTS_f()
{
	int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: tts <text to speak> [volume] [pitch]\n");
		return;
	}

	float vol = sp_tts_volume ? sp_tts_volume->value : 1.0f;
	float pitch = sp_tts_pitch ? sp_tts_pitch->value : 1.0f;
	bool jitter = (!sp_tts_jitter || sp_tts_jitter->value > 0.0f);
	bool whisper = (sp_tts_whisper && sp_tts_whisper->value > 0.0f);

	// Check if trailing arguments are numeric volume / pitch overrides
	int endIdx = argc;
	if (argc >= 4)
	{
		char* endp1 = nullptr;
		char* endp2 = nullptr;
		float p = static_cast<float>(strtod(gEngfuncs.Cmd_Argv(argc - 1), &endp1));
		float v = static_cast<float>(strtod(gEngfuncs.Cmd_Argv(argc - 2), &endp2));
		if (endp1 && *endp1 == '\0' && endp2 && *endp2 == '\0' && endp1 != gEngfuncs.Cmd_Argv(argc - 1) && endp2 != gEngfuncs.Cmd_Argv(argc - 2))
		{
			vol = v;
			pitch = p;
			jitter = false;
			endIdx = argc - 2;
		}
	}
	else if (argc >= 3)
	{
		char* endp = nullptr;
		float v = static_cast<float>(strtod(gEngfuncs.Cmd_Argv(argc - 1), &endp));
		if (endp && *endp == '\0' && endp != gEngfuncs.Cmd_Argv(argc - 1))
		{
			vol = v;
			endIdx = argc - 1;
		}
	}

	std::string text;
	for (int i = 1; i < endIdx; ++i)
	{
		const char* arg = gEngfuncs.Cmd_Argv(i);
		if (arg && *arg)
		{
			if (!text.empty()) text += " ";
			text += arg;
		}
	}

	if (text.empty())
		return;

	std::string formatted = CGameState::Get().FormatTokens(text);
	bool randStyle = (sp_tts_random_style && sp_tts_random_style->value > 0.0f) || CSoundpadConfig::Get().GetTtsRandomStyle();
	SoundEngine_SpeakTTS(formatted.c_str(), vol, pitch, jitter, whisper, false, true, randStyle, true);
}

static void SP_Medic_f()
{
	// sp_medic is a convenience alias for triggering a Command reaction matching 'medic'
	if (!CheckAndFireReaction(ReactionEventType::Command, "medic"))
	{
		SafeConPrintf("[Soundpad] No Command reaction configured for 'medic'!\n");
		SafeConPrintf("Tip: In Soundpad, open Game Reactions tab -> Add Reaction -> Event: 'Command', Condition: 'medic' (or 'medic <35').\n");
	}
}

static void SP_Cmd_f()
{
	int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: sp_cmd <command_name>\n");
		SafeConPrintf("Triggers Game Reactions with Event 'Command' matching the command name and conditions (e.g. 'medic <35').\n");
		return;
	}
	const char* cmdName = gEngfuncs.Cmd_Argv(1);
	if (!CheckAndFireReaction(ReactionEventType::Command, cmdName))
	{
		SafeConPrintf("[Soundpad] No Command reactions matched '%s'.\n", cmdName);
	}
}

static void SP_Ban_f()
{
	int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: sp_ban <player slot 1..32, SteamID64, or Nickname> [reason]\n");
		return;
	}

	const char* targetArg = gEngfuncs.Cmd_Argv(1);
	std::string reason = (argc >= 3) ? gEngfuncs.Cmd_Argv(2) : "Console ban";

	uint64_t targetSteamID = 0;
	std::string targetName = targetArg;

	char* endp = nullptr;
	unsigned long long val = std::strtoull(targetArg, &endp, 10);
	if (endp && *endp == '\0' && val >= 1 && val <= 32 && gEngfuncs.pfnGetPlayerInfo)
	{
		hud_player_info_t info = {};
		gEngfuncs.pfnGetPlayerInfo(static_cast<int>(val), &info);
		if (info.name && info.name[0])
			targetName = info.name;
		targetSteamID = GetPlayerSteamID(static_cast<int>(val));

		if (info.thisplayer)
		{
			SafeConPrintf("[Soundpad] Cannot ban yourself!\n");
			return;
		}
	}
	else if (endp && *endp == '\0' && val > 32)
	{
		targetSteamID = val;
		if (gEngfuncs.pfnGetPlayerInfo)
		{
			for (int slot = 1; slot <= 32; ++slot)
			{
				hud_player_info_t info = {};
				gEngfuncs.pfnGetPlayerInfo(slot, &info);
				uint64_t slotSteamID = GetPlayerSteamID(slot);
				if (slotSteamID == targetSteamID && info.name && info.name[0])
				{
					targetName = info.name;
					break;
				}
			}
		}
	}
	else
	{
		// Target is a player name string
		targetSteamID = 0;
		targetName = targetArg;
		if (gEngfuncs.pfnGetPlayerInfo)
		{
			for (int slot = 1; slot <= 32; ++slot)
			{
				hud_player_info_t info = {};
				gEngfuncs.pfnGetPlayerInfo(slot, &info);
				if (info.name && _stricmp(info.name, targetArg) == 0)
				{
					if (info.thisplayer)
					{
						SafeConPrintf("[Soundpad] Cannot ban yourself!\n");
						return;
					}
					targetSteamID = GetPlayerSteamID(slot);
					targetName = info.name;
					break;
				}
			}
		}
	}

	if (targetSteamID == 0 && targetName.empty())
	{
		SafeConPrintf("[Soundpad] Invalid slot, SteamID, or Nickname: '%s'\n", targetArg);
		return;
	}

	cl_entity_t* localEnt = gEngfuncs.GetLocalPlayer ? gEngfuncs.GetLocalPlayer() : nullptr;
	if (localEnt && localEnt->player != 0 && localEnt->index > 0 && gEngfuncs.pfnGetPlayerInfo)
	{
		hud_player_info_t localInfo = {};
		gEngfuncs.pfnGetPlayerInfo(localEnt->index, &localInfo);
		if (localInfo.name && _stricmp(localInfo.name, targetName.c_str()) == 0)
		{
			SafeConPrintf("[Soundpad] Cannot ban yourself!\n");
			return;
		}
		uint64_t mySteamID = GetPlayerSteamID(localEnt->index);
		if (mySteamID == 0) mySteamID = localInfo.m_nSteamID;
		if (targetSteamID != 0 && mySteamID != 0 && mySteamID == targetSteamID)
		{
			SafeConPrintf("[Soundpad] Cannot ban yourself!\n");
			return;
		}
	}

	CSoundpadConfig::Get().BanPlayer(targetSteamID, targetName, reason);
	if (targetSteamID != 0)
	{
		SafeConPrintf("[Soundpad] Banned player %s (SteamID: %llu) - %s\n",
			targetName.c_str(), static_cast<unsigned long long>(targetSteamID), reason.c_str());
	}
	else
	{
		SafeConPrintf("[Soundpad] Banned player '%s' by nickname (SteamID is 0 on this server) - %s\n",
			targetName.c_str(), reason.c_str());
	}
}

static void SP_Unban_f()
{
	int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: sp_unban <SteamID64 or Nickname>\n");
		return;
	}

	const char* targetArg = gEngfuncs.Cmd_Argv(1);
	char* endp = nullptr;
	uint64_t targetSteamID = std::strtoull(targetArg, &endp, 10);
	if (endp && *endp == '\0' && targetSteamID != 0)
	{
		if (CSoundpadConfig::Get().UnbanPlayer(targetSteamID))
		{
			SafeConPrintf("[Soundpad] Unbanned SteamID %llu.\n", static_cast<unsigned long long>(targetSteamID));
			return;
		}
	}

	if (CSoundpadConfig::Get().UnbanPlayerByName(targetArg))
	{
		SafeConPrintf("[Soundpad] Unbanned player '%s'.\n", targetArg);
	}
	else
	{
		SafeConPrintf("[Soundpad] '%s' is not in the ban list.\n", targetArg);
	}
}

static void SP_BanList_f()
{
	const auto& bans = CSoundpadConfig::Get().GetBannedPlayers();
	SafeConPrintf("==================== Soundpad Ban List (%u) ====================\n",
		static_cast<unsigned>(bans.size()));
	for (size_t i = 0; i < bans.size(); ++i)
	{
		const auto& b = bans[i];
		SafeConPrintf("#%u: %s | SteamID: %llu | Reason: %s | Date: %s\n",
			static_cast<unsigned>(i + 1), b.playerName.c_str(),
			static_cast<unsigned long long>(b.steamID), b.reason.c_str(), b.banDate.c_str());
	}
	SafeConPrintf("================================================================\n");
}

static void SP_DotStop_f()
{
	int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc >= 2)
	{
		int val = atoi(gEngfuncs.Cmd_Argv(1));
		CSoundpadConfig::Get().SetDotStopEnabled(val != 0);
	}
	SafeConPrintf("[Soundpad] Dot-Stop in chat: %s\n",
		CSoundpadConfig::Get().IsDotStopEnabled() ? "ENABLED ('.' stops playback)" : "DISABLED");
}

static void SP_AliasAdd_f()
{
	int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 3)
	{
		SafeConPrintf("Usage: sp_alias_add <command> <soundpath> [volume] [pitch] [cooldown]\n");
		return;
	}

	ChatAliasItem item;
	item.command = gEngfuncs.Cmd_Argv(1);
	item.soundPath = gEngfuncs.Cmd_Argv(2);
	if (argc >= 4) item.volume = static_cast<float>(atof(gEngfuncs.Cmd_Argv(3)));
	if (argc >= 5) item.pitch = static_cast<float>(atof(gEngfuncs.Cmd_Argv(4)));
	if (argc >= 6) item.cooldown = static_cast<float>(atof(gEngfuncs.Cmd_Argv(5)));

	ChatAliasItem* existing = CSoundpadConfig::Get().FindChatAlias(item.command);
	if (existing)
	{
		existing->soundPath = item.soundPath;
		existing->volume = item.volume;
		existing->pitch = item.pitch;
		existing->cooldown = item.cooldown;
		CSoundpadConfig::Get().Save(false);
		SafeConPrintf("[Soundpad] Updated alias '%s' -> '%s'\n", item.command.c_str(), item.soundPath.c_str());
	}
	else
	{
		CSoundpadConfig::Get().AddChatAlias(item);
		CSoundpadConfig::Get().Save(false);
		SafeConPrintf("[Soundpad] Added alias '%s' -> '%s'\n", item.command.c_str(), item.soundPath.c_str());
	}
}

static void SP_AliasDel_f()
{
	int argc = gEngfuncs.Cmd_Argc ? gEngfuncs.Cmd_Argc() : 0;
	if (argc < 2)
	{
		SafeConPrintf("Usage: sp_alias_del <command>\n");
		return;
	}

	const char* cmd = gEngfuncs.Cmd_Argv(1);
	auto& list = CSoundpadConfig::Get().GetChatAliases();
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (_stricmp(list[i].command.c_str(), cmd) == 0)
		{
			CSoundpadConfig::Get().RemoveChatAlias(i);
			CSoundpadConfig::Get().Save(false);
			SafeConPrintf("[Soundpad] Removed alias '%s'.\n", cmd);
			return;
		}
	}

	// Also check if set as a chat trigger on a sound library item
	SoundItem* s = CSoundpadConfig::Get().FindSoundByChatTrigger(cmd);
	if (s)
	{
		s->chatTrigger.clear();
		CSoundpadConfig::Get().Save(false);
		if (g_hSoundpadFrame.Get())
		{
			g_hSoundpadFrame->RefreshSoundList();
		}
		SafeConPrintf("[Soundpad] Cleared library chat trigger '%s'.\n", cmd);
		return;
	}

	SafeConPrintf("[Soundpad] Alias '%s' not found.\n", cmd);
}

static void CollectLibraryTriggersRecursive(const std::vector<CategoryNode>& cats,
	std::vector<std::pair<std::string, const SoundItem*>>& outTriggers)
{
	for (const auto& cat : cats)
	{
		for (const auto& s : cat.sounds)
		{
			if (!s.chatTrigger.empty())
			{
				outTriggers.push_back({ cat.name, &s });
			}
		}
		CollectLibraryTriggersRecursive(cat.subcategories, outTriggers);
	}
}

static void SP_AliasList_f()
{
	const auto& aliases = CSoundpadConfig::Get().GetChatAliases();
	std::vector<std::pair<std::string, const SoundItem*>> libTriggers;
	CollectLibraryTriggersRecursive(CSoundpadConfig::Get().GetRootCategories(), libTriggers);

	size_t total = aliases.size() + libTriggers.size();
	SafeConPrintf("==================== Soundpad Chat Aliases & Triggers (%u) ====================\n",
		static_cast<unsigned>(total));

	unsigned idx = 1;
	if (!aliases.empty())
	{
		SafeConPrintf("--- Explicit Chat Aliases (%u) ---\n", static_cast<unsigned>(aliases.size()));
		for (size_t i = 0; i < aliases.size(); ++i)
		{
			const auto& a = aliases[i];
			SafeConPrintf("#%u: '%s' -> '%s' (vol: %.2f, pitch: %.2f, cd: %.1fs)\n",
				idx++, a.command.c_str(), a.soundPath.c_str(),
				a.volume, a.pitch, a.cooldown);
		}
	}

	if (!libTriggers.empty())
	{
		SafeConPrintf("--- Library Sound Triggers (%u) ---\n", static_cast<unsigned>(libTriggers.size()));
		for (size_t i = 0; i < libTriggers.size(); ++i)
		{
			const auto& catName = libTriggers[i].first;
			const auto* s = libTriggers[i].second;
			SafeConPrintf("#%u: '%s' -> '%s' [%s] (vol: %.2f, pitch: %.2f)\n",
				idx++, s->chatTrigger.c_str(), s->path.c_str(), catName.c_str(),
				s->volume, s->pitch);
		}
	}

	if (total == 0)
	{
		SafeConPrintf("No chat aliases or triggers registered.\n");
	}
	SafeConPrintf("========================================================================\n");
}

static void UnregisterVGUI2ExtensionCallbacks()
{
	if (IVGUI2Extension* pExtension = VGUI2Extension())
	{
		pExtension->UnregisterBaseUICallbacks(&s_BaseUICallbacks);
		pExtension->UnregisterGameUICallbacks(&g_GameUICallbacks);
		pExtension->UnregisterClientVGUICallbacks(&g_ClientVGUICallbacks);
		pExtension->UnregisterKeyValuesCallbacks(&s_KeyValuesCallbacks);
		pExtension->UnregisterGameUITaskBarCallbacks(&s_TaskBarCallbacks);
	}

	VGUI2Extension_Shutdown();
}

void DllLoadNotification(mh_load_dll_notification_context_t* ctx)
{
	if (!ctx || !ctx->BaseDllName)
		return;

	const bool isVoiceExtension = !_wcsicmp(ctx->BaseDllName, L"VoiceExtension.dll") ||
	                              !_wcsicmp(ctx->BaseDllName, L"VoiceExtension_AVX2.dll");
	const bool isVGUI2Extension = !_wcsicmp(ctx->BaseDllName, L"VGUI2Extension.dll") ||
	                              !_wcsicmp(ctx->BaseDllName, L"VGUI2Extension_AVX2.dll") ||
	                              !_wcsicmp(ctx->BaseDllName, L"VGUI2Extension_AVX.dll") ||
	                              !_wcsicmp(ctx->BaseDllName, L"VGUI2Extension_SSE2.dll") ||
	                              !_wcsicmp(ctx->BaseDllName, L"VGUI2Extension_SSE.dll");

	if (ctx->flags & LOAD_DLL_NOTIFICATION_IS_LOAD)
	{
		// The loader lock is held here. Only schedule the reload.
		if (isVoiceExtension)
			g_pendingVoiceReload.store(true);
		else if (isVGUI2Extension)
			g_pendingVguiReload.store(true);
	}
	else if (ctx->flags & LOAD_DLL_NOTIFICATION_IS_UNLOAD)
	{
		if (isVoiceExtension)
		{
			// Never call into a module that is detaching.
			SoundEngine_OnVoiceExtensionUnloaded();
			g_pendingVoiceReload.store(false);
		}
		else if (isVGUI2Extension)
		{
			VGUI2Extension_Forget();
			g_pendingVguiReload.store(false);
		}
	}
}

void IPluginsV4::Init(metahook_api_t* pAPI, mh_interface_t* pIface, mh_enginesave_t* pSave)
{
	DebuLog("Soundpad IPluginsV4::Init: pAPI=%p, pInterface=%p, pSave=%p", pAPI, pIface, pSave);
	g_pMetaHookAPI = pAPI;
	g_pInterface = pIface;
	g_pMetaSave = pSave;

	if (g_pInterface && g_pInterface->MetaHookAPIVersion < METAHOOK_API_VERSION)
	{
		char buf[128] = {};
		StringCchPrintfA(buf, sizeof(buf), "[Soundpad] WARNING: MetaHookAPIVersion is %d (expected %d)\n",
			g_pInterface->MetaHookAPIVersion, METAHOOK_API_VERSION);
		if (gEngfuncs.Con_DPrintf)
			gEngfuncs.Con_DPrintf("%s", buf);
		else
			OutputDebugStringA(buf);
	}
}

void IPluginsV4::Shutdown(void)
{
	DebuLog("Soundpad IPluginsV4::Shutdown entered");
	if (g_pMetaHookAPI)
	{
		g_pMetaHookAPI->UnregisterLoadDllNotificationCallback(DllLoadNotification);
	}

	SoundEngine_Shutdown();
	UnregisterVGUI2ExtensionCallbacks();
}

void IPluginsV4::LoadEngine(cl_enginefunc_t* pEngfuncs)
{
	DebuLog("Soundpad IPluginsV4::LoadEngine: pEngfuncs=%p", pEngfuncs);
	if (!pEngfuncs)
		return;

	if (g_pInterface)
	{
		g_pFileSystem = g_pInterface->FileSystem;
		if (!g_pFileSystem)
		{
			g_pFileSystem = (IFileSystem*)g_pInterface->FileSystem_HL25;
		}
	}
	g_pFullFileSystem = g_pFileSystem;

	if (g_pMetaHookAPI)
	{
		g_iEngineType = g_pMetaHookAPI->GetEngineType();
		g_dwEngineBuildnum = g_pMetaHookAPI->GetEngineBuildnum();
	}

	memcpy(&gEngfuncs, pEngfuncs, sizeof(gEngfuncs));

	if (g_pMetaHookAPI)
	{
		g_pMetaHookAPI->RegisterLoadDllNotificationCallback(DllLoadNotification);
	}

	if (!g_pKeyValuesSystem)
	{
		HINTERFACEMODULE hVGUI2 = (HINTERFACEMODULE)GetModuleHandleA("vgui2.dll");
		if (hVGUI2)
		{
			CreateInterfaceFn fnVGUI2CreateInterface = Sys_GetFactory(hVGUI2);
			if (fnVGUI2CreateInterface)
			{
				g_pKeyValuesSystem = (IKeyValuesSystem*)fnVGUI2CreateInterface(KEYVALUESSYSTEM_INTERFACE_VERSION, NULL);
				if (g_pKeyValuesSystem)
				{
					g_pKeyValuesSystem->RegisterSizeofKeyValues(sizeof(KeyValues));
				}
			}
		}
	}

	VGUI2Extension_Init();
	RegisterVGUI2ExtensionCallbacks();

	SoundEngine_Init();
}

void IPluginsV4::LoadClient(cl_exportfuncs_t* pExportFunc)
{
	DebuLog("Soundpad IPluginsV4::LoadClient: pExportFunc=%p", pExportFunc);
	if (!pExportFunc)
		return;

	memcpy(&gExportfuncs, pExportFunc, sizeof(gExportfuncs));

	static bool bClientInitialized = false;
	if (!bClientInitialized)
	{
		bClientInitialized = true;
		srand(static_cast<unsigned int>(time(nullptr)));

		if (gEngfuncs.pfnRegisterVariable)
		{
			sp_allow_server = gEngfuncs.pfnRegisterVariable("sp_allow_server", "1", 0);
			sp_pause_on_mic = gEngfuncs.pfnRegisterVariable("sp_pause_on_mic", "0", 0);
			sp_mic_duck = gEngfuncs.pfnRegisterVariable("sp_mic_duck", "1", 0);
#if SOUNDPAD_ENABLE_LEGACY_COMMANDS
			sp_duck_level = gEngfuncs.pfnRegisterVariable("sp_duck_level", "0.25", 0);
#endif
			sp_reactions_enabled = gEngfuncs.pfnRegisterVariable("sp_reactions", "1", 0);
			sp_autohide = gEngfuncs.pfnRegisterVariable("sp_autohide", "1", 0);
			sp_tts_engine = gEngfuncs.pfnRegisterVariable("sp_tts_engine", "klatter", 0);
			sp_sapi_voice = gEngfuncs.pfnRegisterVariable("sp_sapi_voice", "", 0);
			sp_tts_volume = gEngfuncs.pfnRegisterVariable("sp_tts_volume", "1.0", 0);
			sp_tts_pitch = gEngfuncs.pfnRegisterVariable("sp_tts_pitch", "1.0", 0);
			sp_tts_jitter = gEngfuncs.pfnRegisterVariable("sp_tts_jitter", "1", 0);
			sp_tts_whisper = gEngfuncs.pfnRegisterVariable("sp_tts_whisper", "0", 0);
			sp_tts_autosay = gEngfuncs.pfnRegisterVariable("sp_tts_autosay", "0", 0);
			sp_tts_random_style = gEngfuncs.pfnRegisterVariable("sp_tts_random_style", "0", 0);
#if SOUNDPAD_ENABLE_LEGACY_COMMANDS
			sp_voice_hangtime = gEngfuncs.pfnRegisterVariable("sp_voice_hangtime", "0.4", 0);
#endif
			sp_medic_auto = gEngfuncs.pfnRegisterVariable("sp_medic_auto", "0", 0);
			sp_debug = gEngfuncs.pfnRegisterVariable("sp_debug", "0", 0);

			if (gEngfuncs.Cvar_Set)
			{
				const std::string& eng = CSoundpadConfig::Get().GetTtsEngine();
				if (!eng.empty())
					gEngfuncs.Cvar_Set("sp_tts_engine", eng.c_str());
				const std::string& voice = CSoundpadConfig::Get().GetSapiVoice();
				if (!voice.empty())
					gEngfuncs.Cvar_Set("sp_sapi_voice", voice.c_str());
			}
		}

		if (gEngfuncs.pfnAddCommand)
		{
			gEngfuncs.pfnAddCommand("soundpad", Soundpad_ToggleGUI);
			gEngfuncs.pfnAddCommand("sp_gui", Soundpad_ToggleGUI);
			gEngfuncs.pfnAddCommand("sp_medic", SP_Medic_f);
			gEngfuncs.pfnAddCommand("sp_call_medic", SP_Medic_f);
			gEngfuncs.pfnAddCommand("sp_cmd", SP_Cmd_f);
			gEngfuncs.pfnAddCommand("spplay", SP_PlayLocal_f);
			gEngfuncs.pfnAddCommand("spqueue", SP_QueueLocal_f);
			gEngfuncs.pfnAddCommand("sp_play", SP_Play_f);
			gEngfuncs.pfnAddCommand("sp_queue", SP_Queue_f);
			gEngfuncs.pfnAddCommand("sp_stop", SP_Stop_f);
			gEngfuncs.pfnAddCommand("sp_panic", SP_Panic_f);
			gEngfuncs.pfnAddCommand("sppreview", SP_PreviewLocal_f);
			gEngfuncs.pfnAddCommand("sp_preview", SP_Preview_f);
			gEngfuncs.pfnAddCommand("sprandom", SP_RandomLocal_f);
			gEngfuncs.pfnAddCommand("sp_random", SP_Random_f);
			gEngfuncs.pfnAddCommand("spnext", SP_NextLocal_f);
			gEngfuncs.pfnAddCommand("sp_next", SP_Next_f);
			gEngfuncs.pfnAddCommand("spprev", SP_PrevLocal_f);
			gEngfuncs.pfnAddCommand("sp_prev", SP_Prev_f);
			gEngfuncs.pfnAddCommand("sp_dump", SP_Dump_f);
			gEngfuncs.pfnAddCommand("sp_add", SP_Add_f);
			gEngfuncs.pfnAddCommand("sp_addfolder", SP_AddFolder_f);
			gEngfuncs.pfnAddCommand("spsave", SP_SaveLocal_f);
			gEngfuncs.pfnAddCommand("sp_save", SP_Save_f);
			gEngfuncs.pfnAddCommand("sp_reload", SP_Reload_f);
			gEngfuncs.pfnAddCommand("sp_trigger", SP_Trigger_f);
			gEngfuncs.pfnAddCommand("sp_seq_play", SP_SeqPlay_f);
			gEngfuncs.pfnAddCommand("sp_aim_tts", SP_AimTTS_f);
			gEngfuncs.pfnAddCommand("tts", SP_SpeakTTS_f);
			gEngfuncs.pfnAddCommand("sp_tts", SP_SpeakTTS_f);
			gEngfuncs.pfnAddCommand("sp_speak", SP_SpeakTTS_f);
#if SOUNDPAD_ENABLE_LEGACY_COMMANDS
			gEngfuncs.pfnAddCommand("sp_ban", SP_Ban_f);
			gEngfuncs.pfnAddCommand("sp_unban", SP_Unban_f);
			gEngfuncs.pfnAddCommand("sp_banlist", SP_BanList_f);
#endif
			gEngfuncs.pfnAddCommand("sp_dot_stop", SP_DotStop_f);
#if SOUNDPAD_ENABLE_LEGACY_COMMANDS
			gEngfuncs.pfnAddCommand("sp_alias_add", SP_AliasAdd_f);
			gEngfuncs.pfnAddCommand("sp_alias_del", SP_AliasDel_f);
			gEngfuncs.pfnAddCommand("sp_alias_list", SP_AliasList_f);
#endif
		}
	}

	// Hook HUD_Init to clean up all audio tails, damage state and subdialogs across map transitions
	if (pExportFunc->HUD_Init && pExportFunc->HUD_Init != Hook_HUD_Init)
	{
		g_pfnHUD_Init_Original = pExportFunc->HUD_Init;
		pExportFunc->HUD_Init = Hook_HUD_Init;
	}

	// Hook HUD_Frame safely across server/map transitions
	if (pExportFunc->HUD_Frame && pExportFunc->HUD_Frame != HookHUD_Frame)
	{
		g_originalHudFrame = pExportFunc->HUD_Frame;
		pExportFunc->HUD_Frame = HookHUD_Frame;
	}

	// Hook HUD_GetStudioModelInterface to capture g_engineStudio for player SteamIDs
	if (pExportFunc->HUD_GetStudioModelInterface && pExportFunc->HUD_GetStudioModelInterface != Hook_HUD_GetStudioModelInterface)
	{
		g_pfnHUD_GetStudioModelInterface = pExportFunc->HUD_GetStudioModelInterface;
		pExportFunc->HUD_GetStudioModelInterface = Hook_HUD_GetStudioModelInterface;
	}

	ProcessPendingModuleNotifications();
}

void IPluginsV4::ExitGame(int iResult)
{
	SoundEngine_Shutdown();
	UnregisterVGUI2ExtensionCallbacks();
}

const char* IPluginsV4::GetVersion(void)
{
	return "1.0.0";
}

EXPOSE_SINGLE_INTERFACE(IPluginsV4, IPluginsV4, METAHOOK_PLUGIN_API_VERSION_V4);
