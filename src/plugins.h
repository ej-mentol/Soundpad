#pragma once

#include <metahook.h>
#include <cvardef.h>
#include <string>
#include <IVGUI2Extension.h>

namespace vgui
{
	class Panel;
}

#define MHPluginName "Soundpad"

// Slept console surface: ban list (needs reliable SteamID; the UI tab is already
// compiled out) and chat aliases (absorbed by the Chat Trigger column).
#define SOUNDPAD_ENABLE_LEGACY_COMMANDS 0
#define Sys_Error(msg, ...) g_pMetaHookAPI->SysError("[" MHPluginName "] " msg, ##__VA_ARGS__)

extern cl_enginefunc_t gEngfuncs;
extern cl_exportfuncs_t gExportfuncs;
extern metahook_api_t* g_pMetaHookAPI;
extern mh_interface_t* g_pInterface;
extern mh_enginesave_t* g_pMetaSave;
extern IFileSystem* g_pFileSystem;
extern class IKeyValuesSystem* g_pKeyValuesSystem;
extern int g_iEngineType;
extern DWORD g_dwEngineBuildnum;
extern cvar_t* sp_allow_server;
extern cvar_t* sp_pause_on_mic;
extern cvar_t* sp_mic_duck;
extern cvar_t* sp_duck_level;
extern cvar_t* sp_reactions_enabled;
extern cvar_t* sp_autohide;
extern cvar_t* sp_tts_engine;
extern cvar_t* sp_sapi_voice;
extern cvar_t* sp_tts_volume;
extern cvar_t* sp_tts_pitch;
extern cvar_t* sp_tts_jitter;
extern cvar_t* sp_tts_whisper;
extern cvar_t* sp_tts_autosay;
extern cvar_t* sp_tts_random_style;
extern cvar_t* sp_voice_hangtime;
extern cvar_t* sp_debug;

inline bool SP_IsDebug() { return sp_debug && sp_debug->value > 0.0f; }
#define SP_DEBUG_LOG(...) do { if (sp_debug && sp_debug->value > 0.0f) SafeConPrintf(__VA_ARGS__); } while (0)

extern bool g_bOpenedFromCommand;
void SafeConPrintf(const char* fmt, ...);
uint64_t GetPlayerSteamID(int clientIndex);
void Soundpad_OpenGUI(vgui::Panel* parent = nullptr, bool fromCommand = false);
void Soundpad_ToggleGUI();
void SoundEngine_PlaySequence(const struct SequenceItem& seq);
void ReactionManager_TriggerManual(const char* eventName, const char* condition, bool forceTest = false);
void DllLoadNotification(mh_load_dll_notification_context_t* ctx);

class CVGUI2Extension_GameUICallbacks : public IVGUI2Extension_GameUICallbacks
{
public:
	int GetAltitude() const override { return 0; }
	const char* GetControlModuleName() const override { return "Soundpad"; }

	void Initialize(CreateInterfaceFn* factories, int count) override;
	void PreStart(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion, void* system) override {}
	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion, void* system) override {}
	void Shutdown() override;
	void PostShutdown() override {}
	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override;
	void ActivateDemoUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HasExclusiveInput(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void RunFrame(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ConnectToServer(const char*& game, int& IP, int& port, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void DisconnectFromServer(VGUI2Extension_CallbackContext* CallbackContext) override;
	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override;
	void IsGameUIActive(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void LoadingStarted(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override;
	void LoadingFinished(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override;
	void StartProgressBar(const char*& progressType, int& progressSteps, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ContinueProgressBar(int& progressPoint, float& progressFraction, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void StopProgressBar(bool& bError, const char*& failureReason, const char*& extendedReason, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetProgressBarStatusText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetSecondaryProgressBar(float& progress, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetSecondaryProgressBarText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {}
};

extern CVGUI2Extension_GameUICallbacks g_GameUICallbacks;
extern bool g_bReopenOnGameUI;

class CVGUI2Extension_ClientVGUICallbacks : public IVGUI2Extension_ClientVGUICallbacks
{
public:
	int GetAltitude() const override { return 100; }

	void Initialize(CreateInterfaceFn* factories, int count) override;
	void Shutdown() override;
	void Start() override {}
	void SetParent(vgui::VPANEL parent) override {}
	void UseVGUI1(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideScoreBoard(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideAllVGUIMenu(VGUI2Extension_CallbackContext* CallbackContext) override;
	void ActivateClientUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideClientUI(VGUI2Extension_CallbackContext* CallbackContext) override;
};

extern CVGUI2Extension_ClientVGUICallbacks g_ClientVGUICallbacks;
