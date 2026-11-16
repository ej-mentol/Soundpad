#include <metahook.h>
#include <IScheme2.h>
#include <ISurface2.h>
#include <IInput2.h>
#include <IPanel2.h>

#include "plugins.h"
#include "VGUI2ExtensionImport.h"

IVGUI2Extension* g_pVGUI2Extension = nullptr;
IDpiManager* g_pDpiManager = nullptr;

vgui::ISchemeManager2* g_pVGuiSchemeManager2 = nullptr;
vgui::ISurface2* g_pVGuiSurface2 = nullptr;
vgui::IInput2* g_pVGuiInput2 = nullptr;
vgui::IPanel2* g_pVGuiPanel2 = nullptr;

static HMODULE FindVGUI2Extension()
{
	// MetaHook may load an instruction-set variant when the baseline file is absent.
	static const char* kNames[] = {
		"VGUI2Extension.dll",
		"VGUI2Extension_AVX2.dll",
		"VGUI2Extension_AVX.dll",
		"VGUI2Extension_SSE2.dll",
		"VGUI2Extension_SSE.dll"
	};

	for (const char* name : kNames)
	{
		if (HMODULE h = GetModuleHandleA(name))
			return h;
	}
	return nullptr;
}

bool VGUI2Extension_IsLoaded()
{
	return FindVGUI2Extension() != nullptr;
}

// Drops every cached pointer without calling into the module. The unload
// notification runs while the DLL is detaching, so no virtual call is safe.
void VGUI2Extension_Forget()
{
	g_pVGUI2Extension = nullptr;
	g_pDpiManager = nullptr;
	g_pVGuiSchemeManager2 = nullptr;
	g_pVGuiSurface2 = nullptr;
	g_pVGuiInput2 = nullptr;
	g_pVGuiPanel2 = nullptr;
}

void VGUI2Extension_Init()
{
	VGUI2Extension_Forget();

	auto hVGUI2Extension = FindVGUI2Extension();
	if (!hVGUI2Extension)
		return;

	auto factory = Sys_GetFactory((HINTERFACEMODULE)hVGUI2Extension);
	if (!factory)
		return;

	g_pVGUI2Extension = (IVGUI2Extension*)factory(VGUI2_EXTENSION_INTERFACE_VERSION, NULL);
	g_pDpiManager = (IDpiManager*)factory(DPI_MANAGER_INTERFACE_VERSION, NULL);
	g_pVGuiSurface2 = (vgui::ISurface2*)factory(VGUI_SURFACE2_INTERFACE_VERSION, NULL);
	g_pVGuiSchemeManager2 = (vgui::ISchemeManager2*)factory(VGUI_SCHEME2_INTERFACE_VERSION, NULL);
	g_pVGuiInput2 = (vgui::IInput2*)factory(VGUI_INPUT2_INTERFACE_VERSION, NULL);
}

void VGUI2Extension_Shutdown()
{
	VGUI2Extension_Forget();
}

IVGUI2Extension* VGUI2Extension()
{
	if (!VGUI2Extension_IsLoaded())
	{
		VGUI2Extension_Forget();
		return nullptr;
	}

	return g_pVGUI2Extension;
}

IDpiManager* DpiManager()
{
	if (!VGUI2Extension_IsLoaded())
	{
		VGUI2Extension_Forget();
		return nullptr;
	}

	return g_pDpiManager;
}
