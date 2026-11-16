#pragma once

#include <IVGUI2Extension.h>
#include <IDpiManager.h>
#include <IScheme2.h>
#include <ISurface2.h>
#include <IInput2.h>
#include <IPanel2.h>

extern IVGUI2Extension* g_pVGUI2Extension;
extern IDpiManager* g_pDpiManager;

extern vgui::ISchemeManager2* g_pVGuiSchemeManager2;
extern vgui::ISurface2* g_pVGuiSurface2;
extern vgui::IInput2* g_pVGuiInput2;
extern vgui::IPanel2* g_pVGuiPanel2;

void VGUI2Extension_Init();
void VGUI2Extension_Shutdown();
void VGUI2Extension_Forget();
bool VGUI2Extension_IsLoaded();

IVGUI2Extension* VGUI2Extension();
IDpiManager* DpiManager();
