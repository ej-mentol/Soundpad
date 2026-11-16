#include "SoundpadFrame.h"
#include "SoundEngine.h"
#include "SapiEngine.h"
#include "plugins.h"
#include "GameState.h"
#include "DebugLog.h"

#include <vgui/isurface.h>
#include <vgui/ilocalize.h>
#include <vgui/IInput.h>
#include <vgui_controls/Controls.h>
#include <KeyValues.h>
#include <strsafe.h>
#include <algorithm>
#include <filesystem>
#include <unordered_map>

vgui::DHANDLE<CSoundpadFrame> g_hSoundpadFrame;

// -------------------------------------------------------------------------------------------------
// Dynamic Game Reactions Dialog
// -------------------------------------------------------------------------------------------------
class CReactionEditDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CReactionEditDialog, vgui::Frame);

public:
	CReactionEditDialog(vgui::Panel* parent, CSoundpadFrame* mainFrame, int reactionIndex = -1);
	virtual ~CReactionEditDialog();

protected:
	void OnClose() override;
	void OnCommand(const char* command) override;
	void PerformLayout() override;
	void OnKeyCodeTyped(vgui::KeyCode code) override;

	MESSAGE_FUNC_PTR(OnTextChanged, "TextChanged", panel);
	MESSAGE_FUNC_PTR(OnItemSelected, "ItemSelected", panel);
	MESSAGE_FUNC_PARAMS(OnFileSelected, "FileSelected", kv);

private:
	void UpdateConditionHint(bool keepCurrentText = false);

	CSoundpadFrame* m_pMainFrame;
	int m_reactionIndex;

	vgui::Label* m_pLabelActionType;
	vgui::ComboBox* m_pComboActionType;

	vgui::Label* m_pLabelSound;
	vgui::TextEntry* m_pEntrySound;
	vgui::Button* m_pBtnBrowseSound;
	vgui::DHANDLE<vgui::FileOpenDialog> m_hFileOpenDialog;

	vgui::Label* m_pLabelEvent;
	vgui::ComboBox* m_pComboEvent;

	vgui::Label* m_pLabelCondition;
	vgui::ComboBox* m_pComboCondition;
	vgui::Label* m_pLabelHint;

	vgui::Label* m_pLabelChance;
	vgui::TextEntry* m_pEntryChance;

	vgui::Label* m_pLabelCooldown;
	vgui::TextEntry* m_pEntryCooldown;

	vgui::Label* m_pLabelVol;
	vgui::Slider* m_pSliderVol;

	vgui::Label* m_pLabelPitch;
	vgui::Slider* m_pSliderPitch;

	vgui::CheckButton* m_pCheckReverb;
	vgui::CheckButton* m_pCheckZigzag;
	vgui::Label* m_pLabelFxHint;

	vgui::Button* m_pBtnTest;
	vgui::Button* m_pBtnSave;
	vgui::Button* m_pBtnCancel;
};

// -------------------------------------------------------------------------------------------------
// Sound Sequencer Step Dialog
// -------------------------------------------------------------------------------------------------
class CSequenceStepDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CSequenceStepDialog, vgui::Frame);

public:
	CSequenceStepDialog(vgui::Panel* parent, CSoundpadFrame* mainFrame, int seqIndex, int stepIndex = -1);
	virtual ~CSequenceStepDialog();

protected:
	void OnClose() override;
	void OnCommand(const char* command) override;
	void PerformLayout() override;
	void OnKeyCodeTyped(vgui::KeyCode code) override;

	MESSAGE_FUNC_PARAMS(OnFileSelected, "FileSelected", kv);

private:
	CSoundpadFrame* m_pMainFrame;
	int m_seqIndex;
	int m_stepIndex;

	vgui::Label* m_pLabelSound;
	vgui::TextEntry* m_pEntrySound;
	vgui::Button* m_pBtnBrowseSound;
	vgui::DHANDLE<vgui::FileOpenDialog> m_hFileOpenDialog;

	vgui::Label* m_pLabelStart;
	vgui::TextEntry* m_pEntryStart;

	vgui::Label* m_pLabelEnd;
	vgui::TextEntry* m_pEntryEnd;

	vgui::Label* m_pLabelRepeats;
	vgui::TextEntry* m_pEntryRepeats;

	vgui::Label* m_pLabelPause;
	vgui::TextEntry* m_pEntryPause;

	vgui::Label* m_pLabelPitchStep;
	vgui::TextEntry* m_pEntryPitchStep;

	vgui::CheckButton* m_pCheckReverb;
	vgui::CheckButton* m_pCheckZigzag;
	vgui::Label* m_pLabelFxHint;

	vgui::Button* m_pBtnSave;
	vgui::Button* m_pBtnCancel;
};

// -------------------------------------------------------------------------------------------------
// Manual Ban Dialog
// -------------------------------------------------------------------------------------------------
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
class CManualBanDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CManualBanDialog, vgui::Frame);

public:
	CManualBanDialog(vgui::Panel* parent, CSoundpadFrame* mainFrame);
	virtual ~CManualBanDialog();

protected:
	void OnClose() override;
	void OnCommand(const char* command) override;
	void PerformLayout() override;
	void OnKeyCodeTyped(vgui::KeyCode code) override;

private:
	CSoundpadFrame* m_pMainFrame;

	vgui::Label* m_pLabelSteamID;
	vgui::TextEntry* m_pEntrySteamID;

	vgui::Label* m_pLabelName;
	vgui::TextEntry* m_pEntryName;

	vgui::Label* m_pLabelReason;
	vgui::TextEntry* m_pEntryReason;

	vgui::Button* m_pBtnSave;
	vgui::Button* m_pBtnCancel;
};
#endif

// -------------------------------------------------------------------------------------------------
// Dynamic Game Reactions Dialog
// -------------------------------------------------------------------------------------------------
CReactionEditDialog::CReactionEditDialog(vgui::Panel* parent, CSoundpadFrame* mainFrame, int reactionIndex)
	: BaseClass(parent, "ReactionEditDialog"), m_pMainFrame(mainFrame), m_reactionIndex(reactionIndex)
{
	SetSize(470, 360);
	SetMinimumSize(440, 340);
	SetSizeable(false);
	SetMoveable(true);
	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);
	SetTitle(reactionIndex >= 0 ? "Edit Reaction Trigger" : "Add Reaction Trigger", true);

	m_pLabelActionType = new vgui::Label(this, "LabelActionType", "Action Type:");
	m_pComboActionType = new vgui::ComboBox(this, "ComboActionType", 3, false);
	m_pComboActionType->AddItem("Sound File (*.wav)", new KeyValues("act", "type", 0));
	m_pComboActionType->AddItem("Macro Sequence", new KeyValues("act", "type", 1));
	m_pComboActionType->AddItem("Text-To-Speech (Klatter)", new KeyValues("act", "type", 2));
	m_pComboActionType->AddActionSignalTarget(this);

	m_pLabelSound = new vgui::Label(this, "LabelSound", "Sound Path:");
	m_pEntrySound = new vgui::TextEntry(this, "EntrySound");
	m_pBtnBrowseSound = new vgui::Button(this, "BtnBrowseSound", "Browse...", this, "browse_sound");
	m_pBtnBrowseSound->AddActionSignalTarget(this);

	m_pLabelEvent = new vgui::Label(this, "LabelEvent", "Trigger Event:");
	m_pComboEvent = new vgui::ComboBox(this, "ComboEvent", 5, false);
	m_pComboEvent->AddItem("Death (On Player Death)", new KeyValues("ev", "id", 0));
	m_pComboEvent->AddItem("Damage (On Taking Damage)", new KeyValues("ev", "id", 1));
	m_pComboEvent->AddItem("Kill (On Killing Monster/Player)", new KeyValues("ev", "id", 2));
	m_pComboEvent->AddItem("Command (Console / Keybind)", new KeyValues("ev", "id", 4));
	m_pComboEvent->AddItem("Team Death (Teammate Died)", new KeyValues("ev", "id", 5));
	m_pComboEvent->AddActionSignalTarget(this);

	m_pLabelCondition = new vgui::Label(this, "LabelCond", "Filter / Condition:");
	m_pComboCondition = new vgui::ComboBox(this, "ComboCond", 16, true);
	m_pComboCondition->AddActionSignalTarget(this);

	m_pLabelHint = new vgui::Label(this, "LabelHint", "Filter: select preset from dropdown or type custom");

	m_pLabelChance = new vgui::Label(this, "LabelChance", "Chance % (0.01..100):");
	m_pEntryChance = new vgui::TextEntry(this, "EntryChance");

	m_pLabelCooldown = new vgui::Label(this, "LabelCD", "Cooldown (seconds):");
	m_pEntryCooldown = new vgui::TextEntry(this, "EntryCD");

	m_pLabelVol = new vgui::Label(this, "LabelVol", "Volume: 1.00");
	m_pSliderVol = new vgui::Slider(this, "SliderVol");
	m_pSliderVol->SetRange(1, 100);
	m_pSliderVol->SetValue(100, false);

	m_pLabelPitch = new vgui::Label(this, "LabelPitch", "Pitch: 1.00");
	m_pSliderPitch = new vgui::Slider(this, "SliderPitch");
	m_pSliderPitch->SetRange(50, 200);
	m_pSliderPitch->SetValue(100, false);

#if SOUNDPAD_ENABLE_DSP
	m_pCheckReverb = new vgui::CheckButton(this, "CheckReverb", "Reverb Echo (Bunker FX)");
	m_pCheckZigzag = new vgui::CheckButton(this, "CheckZigzag", "Zigzag Tremolo (Chat FX)");
	m_pLabelFxHint = new vgui::Label(this, "FxHint", "Reverb / Zigzag apply to WAV only; MP3 and OGG play unprocessed.");
#endif

	m_pBtnTest = new vgui::Button(this, "BtnTest", "Test (Play/Speak)");
	m_pBtnTest->SetCommand("test_dialog_sound");
	m_pBtnTest->AddActionSignalTarget(this);

	m_pBtnSave = new vgui::Button(this, "BtnSave", "Save Reaction");
	m_pBtnSave->SetCommand("save_dialog");
	m_pBtnSave->AddActionSignalTarget(this);

	m_pBtnCancel = new vgui::Button(this, "BtnCancel", "Cancel");
	m_pBtnCancel->SetCommand("cancel_dialog");
	m_pBtnCancel->AddActionSignalTarget(this);

	if (reactionIndex >= 0)
	{
		const auto& reactions = CSoundpadConfig::Get().GetReactions();
		if (reactionIndex < static_cast<int>(reactions.size()))
		{
			const auto& r = reactions[reactionIndex];
			if (r.soundPath.rfind("tts:", 0) == 0)
			{
				m_pComboActionType->ActivateItemByRow(2);
				m_pLabelSound->SetText("TTS Phrase:");
				m_pEntrySound->SetText(r.soundPath.substr(4).c_str());
				m_pLabelHint->SetText("Tokens: %hp%, %armor%, %dmg%, %killer%, %victim%, %deaths%, %kills%, %map%, %name%");
			}
			else if (r.soundPath.rfind("macro:", 0) == 0 || r.soundPath.rfind("seq:", 0) == 0)
			{
				m_pComboActionType->ActivateItemByRow(1);
				m_pLabelSound->SetText("Macro Name:");
				std::string mName = (r.soundPath.rfind("macro:", 0) == 0) ? r.soundPath.substr(6) : r.soundPath.substr(4);
				m_pEntrySound->SetText(mName.c_str());
			}
			else
			{
				m_pComboActionType->ActivateItemByRow(0);
				m_pLabelSound->SetText("Sound Path:");
				m_pEntrySound->SetText(r.soundPath.c_str());
			}

			int targetId = static_cast<int>(r.eventType);
			int targetRow = 0;
			for (int row = 0; row < m_pComboEvent->GetItemCount(); ++row)
			{
				int itemID = m_pComboEvent->GetItemIDFromRow(row);
				KeyValues* evKv = m_pComboEvent->GetItemUserData(itemID);
				if (evKv && evKv->GetInt("id", 0) == targetId)
				{
					targetRow = row;
					break;
				}
			}
			m_pComboEvent->ActivateItemByRow(targetRow);
			UpdateConditionHint(true);
			m_pComboCondition->SetText(r.condition.c_str());

			char chBuf[32] = {}, cdBuf[32] = {};
			StringCchPrintfA(chBuf, sizeof(chBuf), (std::fmod(r.chance, 1.0f) != 0.0f || r.chance < 1.0f) ? "%.1f" : "%.0f", r.chance);
			StringCchPrintfA(cdBuf, sizeof(cdBuf), "%.1f", r.cooldown);
			m_pEntryChance->SetText(chBuf);
			m_pEntryCooldown->SetText(cdBuf);

			m_pSliderVol->SetValue(static_cast<int>(r.volume * 100.0f), false);
			m_pSliderPitch->SetValue(static_cast<int>(r.pitch * 100.0f), false);
#if SOUNDPAD_ENABLE_DSP
			m_pCheckReverb->SetSelected(r.fxReverb);
			m_pCheckZigzag->SetSelected(r.fxZigzag);
#endif
		}
	}
	else
	{
		m_pComboActionType->ActivateItemByRow(0);
		m_pComboEvent->ActivateItemByRow(0);
		UpdateConditionHint(false);
		m_pComboCondition->SetText("GENERIC");
		m_pEntryChance->SetText("100");
		m_pEntryCooldown->SetText("2.0");

		if (m_pMainFrame)
		{
			SoundItem* cur = m_pMainFrame->GetSelectedSound();
			if (cur && !cur->path.empty())
			{
				m_pEntrySound->SetText(cur->path.c_str());
			}
		}
	}
}

CReactionEditDialog::~CReactionEditDialog()
{
	if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
	{
		CloseModal();
	}
	if (m_hFileOpenDialog.Get())
	{
		m_hFileOpenDialog->Close();
		m_hFileOpenDialog->MarkForDeletion();
		m_hFileOpenDialog = nullptr;
	}
}

void CReactionEditDialog::OnClose()
{
	if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
	{
		CloseModal();
	}
	BaseClass::OnClose();
}

void CReactionEditDialog::OnKeyCodeTyped(vgui::KeyCode code)
{
	if (code == vgui::KEY_ESCAPE)
	{
		if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
			CloseModal();
		else
			Close();
		return;
	}
	BaseClass::OnKeyCodeTyped(code);
}

void CReactionEditDialog::UpdateConditionHint(bool keepCurrentText)
{
	char curText[128] = {};
	if (keepCurrentText)
		m_pComboCondition->GetText(curText, sizeof(curText));

	m_pComboCondition->RemoveAll();

	KeyValues* kv = m_pComboEvent->GetActiveItemUserData();
	int evId = kv ? kv->GetInt("id", 0) : 0;
	switch (evId)
	{
	case 0: // Death
	{
		m_pLabelHint->SetText("Presets: ANY, SUICIDE, MONSTER, WORLD, PLAYER, damage type (FALL, POISON, BURN) / killer");
		const char* presets[] = {
			"ANY", "GENERIC", "SUICIDE", "MONSTER", "WORLD", "PLAYER", "FALL", "POISON", "BURN", "BLAST", "DROWN", "SHOCK", "ACID", "SLASH", "CRUSH", "CLUB", "SONIC", "ENERGY",
			"worldspawn", "monster_barney", "monster_scientist", "monster_grunt", "monster_zombie", "monster_alien_slave",
			"monster_headcrab", "monster_bullsquid", "monster_houndeye", "monster_gargantua"
		};
		for (const char* p : presets)
			m_pComboCondition->AddItem(p, nullptr);
		break;
	}
	case 1: // Damage
	{
		m_pLabelHint->SetText("Presets: damage type / HP threshold (>20, >50, <25)");
		const char* presets[] = {
			"GENERIC", "ANY", ">20", ">50", ">75", "<25", "FALL", "POISON", "BURN", "BLAST", "SHOCK", "ACID", "DROWN", "SLASH", "BULLET", "CRUSH", "CLUB", "SONIC", "ENERGY"
		};
		for (const char* p : presets)
			m_pComboCondition->AddItem(p, nullptr);
		break;
	}
	case 2: // Kill
	{
		m_pLabelHint->SetText("Presets: ANY, MONSTER, PLAYER / monster classname / victim player name");
		const char* presets[] = {
			"ANY", "MONSTER", "PLAYER", "monster_grunt", "monster_zombie", "monster_alien_slave", "monster_headcrab",
			"monster_bullsquid", "monster_houndeye", "monster_gargantua", "monster_babygarg", "monster_apache",
			"monster_osprey", "monster_ichthyosaur", "monster_alien_controller", "monster_human_assassin",
			"monster_shocktrooper", "monster_pitdrone", "monster_voltigore"
		};
		for (const char* p : presets)
			m_pComboCondition->AddItem(p, nullptr);
		break;
	}
	case 3: // Chat
	{
		m_pLabelHint->SetText("Presets: chat message trigger (or regex)");
		const char* presets[] = {
			"ANY", "medic", "help", "hello", "heal", "!sound", "lol", "gg", "ready", "yes", "no", "stop"
		};
		for (const char* p : presets)
			m_pComboCondition->AddItem(p, nullptr);
		break;
	}
	case 4: // Command
	{
		m_pLabelHint->SetText("Presets: cmd / cmd <HP (medic, medic <35, <25)");
		const char* presets[] = {
			"medic", "medic <35", "medic <50", "<25", "<35", "<50", "help", "taunt", "cheer", "thanks", "ANY"
		};
		for (const char* p : presets)
			m_pComboCondition->AddItem(p, nullptr);
		break;
	}
	case 5: // OtherDeath / Team Death
	{
		m_pLabelHint->SetText("Presets: ANY / player name / killer name");
		const char* presets[] = {
			"ANY", "GENERIC", "worldspawn", "monster_grunt", "monster_zombie", "monster_alien_slave"
		};
		for (const char* p : presets)
			m_pComboCondition->AddItem(p, nullptr);
		break;
	}
	}

	if (keepCurrentText && curText[0])
	{
		m_pComboCondition->SetText(curText);
	}
	else
	{
		m_pComboCondition->ActivateItemByRow(0);
	}
}

void CReactionEditDialog::OnTextChanged(vgui::Panel* panel)
{
	if (panel == m_pComboEvent)
	{
		UpdateConditionHint(false);
	}
	else if (panel == m_pComboActionType)
	{
		KeyValues* kv = m_pComboActionType->GetActiveItemUserData();
		int actType = kv ? kv->GetInt("type", 0) : 0;
		if (actType == 2)
		{
			m_pLabelSound->SetText("TTS Phrase:");
			m_pLabelHint->SetText("Tokens: %hp%, %armor%, %dmg%, %killer%, %victim%, %deaths%, %kills%, %map%, %name%");
		}
		else if (actType == 1)
		{
			m_pLabelSound->SetText("Macro Name:");
			UpdateConditionHint(true);
		}
		else
		{
			m_pLabelSound->SetText("Sound Path:");
			UpdateConditionHint(true);
		}
		InvalidateLayout();
	}
}

void CReactionEditDialog::OnItemSelected(vgui::Panel* panel)
{
	if (panel == m_pComboEvent)
	{
		UpdateConditionHint(false);
	}
	else if (panel == m_pComboActionType)
	{
		KeyValues* kv = m_pComboActionType->GetActiveItemUserData();
		int actType = kv ? kv->GetInt("type", 0) : 0;
		if (actType == 2)
		{
			m_pLabelSound->SetText("TTS Phrase:");
			m_pLabelHint->SetText("Tokens: %hp%, %armor%, %dmg%, %killer%, %victim%, %deaths%, %kills%, %map%, %name%");
		}
		else if (actType == 1)
		{
			m_pLabelSound->SetText("Macro Name:");
			UpdateConditionHint(true);
		}
		else
		{
			m_pLabelSound->SetText("Sound Path:");
			UpdateConditionHint(true);
		}
		InvalidateLayout();
	}
}

void CReactionEditDialog::PerformLayout()
{
	BaseClass::PerformLayout();
	int w = GetWide();
	int curY = 30;
	const int margin = 16;
	const int labelW = 120;
	const int fieldW = (std::max)(150, w - margin * 2 - labelW - 10);

	m_pLabelActionType->SetBounds(margin, curY, labelW, 20);
	m_pComboActionType->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 24;

	m_pLabelSound->SetBounds(margin, curY, labelW, 20);
	KeyValues* actKv = m_pComboActionType->GetActiveItemUserData();
	int actType = actKv ? actKv->GetInt("type", 0) : 0;
	if (actType == 0)
	{
		m_pEntrySound->SetBounds(margin + labelW + 10, curY, fieldW - 75, 20);
		m_pBtnBrowseSound->SetBounds(margin + labelW + 10 + fieldW - 70, curY, 70, 20);
		m_pBtnBrowseSound->SetVisible(true);
	}
	else
	{
		m_pEntrySound->SetBounds(margin + labelW + 10, curY, fieldW, 20);
		m_pBtnBrowseSound->SetVisible(false);
	}
	curY += 24;

	m_pLabelEvent->SetBounds(margin, curY, labelW, 20);
	m_pComboEvent->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 24;

	m_pLabelCondition->SetBounds(margin, curY, labelW, 20);
	m_pComboCondition->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 22;

	m_pLabelHint->SetBounds(margin + labelW + 10, curY, fieldW, 16); curY += 20;

	m_pLabelChance->SetBounds(margin, curY, labelW, 20);
	m_pEntryChance->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 24;

	m_pLabelCooldown->SetBounds(margin, curY, labelW, 20);
	m_pEntryCooldown->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 24;

	m_pLabelVol->SetBounds(margin, curY, labelW, 20);
	m_pSliderVol->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 24;

	m_pLabelPitch->SetBounds(margin, curY, labelW, 20);
	m_pSliderPitch->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 26;

#if SOUNDPAD_ENABLE_DSP
	m_pCheckReverb->SetBounds(margin, curY, 200, 20);
	m_pCheckZigzag->SetBounds(margin + 210, curY, 200, 20); curY += 28;
	m_pLabelFxHint->SetBounds(margin, curY, w - 2 * margin, 16); curY += 20;
#endif

	m_pBtnTest->SetBounds(margin, curY, 110, 24);
	m_pBtnSave->SetBounds(w - 205, curY, 100, 24);
	m_pBtnCancel->SetBounds(w - 95, curY, 80, 24);
}

void CReactionEditDialog::OnCommand(const char* command)
{
	if (_stricmp(command, "cancel_dialog") == 0)
	{
		if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
			CloseModal();
		else
			Close();
	}
	else if (_stricmp(command, "test_dialog_sound") == 0)
	{
		char snd[MAX_PATH] = {};
		m_pEntrySound->GetText(snd, sizeof(snd));
		if (snd[0])
		{
			float vol = static_cast<float>(m_pSliderVol->GetValue()) / 100.0f;
			float pitch = static_cast<float>(m_pSliderPitch->GetValue()) / 100.0f;

			KeyValues* actKv = m_pComboActionType->GetActiveItemUserData();
			int actType = actKv ? actKv->GetInt("type", 0) : 0;

			if (actType == 2 || strncmp(snd, "tts:", 4) == 0)
			{
				const char* phrase = (strncmp(snd, "tts:", 4) == 0) ? snd + 4 : snd;
				std::string formatted = CGameState::Get().FormatTokens(phrase);
				bool randStyle = CSoundpadConfig::Get().GetTtsRandomStyle();
				SoundEngine_SpeakTTS(formatted.c_str(), vol, pitch, true, false, true, true, randStyle);
			}
			else if (actType == 1 || strncmp(snd, "macro:", 6) == 0 || strncmp(snd, "seq:", 4) == 0)
			{
				const char* seqName = snd;
				if (strncmp(snd, "macro:", 6) == 0) seqName = snd + 6;
				else if (strncmp(snd, "seq:", 4) == 0) seqName = snd + 4;
				const auto* seq = CSoundpadConfig::Get().FindSequence(seqName);
				if (seq)
					SoundEngine_PlaySequence(*seq);
			}
			else
			{
				SoundPlayParams p;
				p.volume = vol;
				p.pitch = pitch;
#if SOUNDPAD_ENABLE_DSP
				p.fxReverb = m_pCheckReverb->IsSelected();
				p.fxZigzag = m_pCheckZigzag->IsSelected();
#endif
				p.localPreviewOnly = true;
				SoundEngine_PlayEx(snd, p);
			}
		}
	}
	else if (_stricmp(command, "save_dialog") == 0)
	{
		char snd[MAX_PATH] = {}, cond[128] = {}, chStr[32] = {}, cdStr[32] = {};
		m_pEntrySound->GetText(snd, sizeof(snd));
		m_pComboCondition->GetText(cond, sizeof(cond));
		m_pEntryChance->GetText(chStr, sizeof(chStr));
		m_pEntryCooldown->GetText(cdStr, sizeof(cdStr));

		KeyValues* actKv = m_pComboActionType->GetActiveItemUserData();
		int actType = actKv ? actKv->GetInt("type", 0) : 0;

		std::string finalPath = snd;
		if (actType == 2 && finalPath.rfind("tts:", 0) != 0)
		{
			finalPath = "tts:" + finalPath;
		}
		else if (actType == 1 && finalPath.rfind("macro:", 0) != 0 && finalPath.rfind("seq:", 0) != 0)
		{
			finalPath = "macro:" + finalPath;
		}

		ReactionItem r;
		r.soundPath = finalPath;

		KeyValues* kv = m_pComboEvent->GetActiveItemUserData();
		int evId = kv ? kv->GetInt("id", 0) : 0;
		r.eventType = static_cast<ReactionEventType>(evId);

		r.condition = cond;
		float enteredChance = static_cast<float>(atof(chStr));
		if (enteredChance <= 0.0f)
			enteredChance = 100.0f;
		else if (enteredChance > 100.0f)
			enteredChance = 100.0f;
		else if (enteredChance < 0.01f)
			enteredChance = 0.01f; // below the roll resolution; would never fire
		r.chance = enteredChance;
		r.cooldown = static_cast<float>(atof(cdStr));
		if (r.cooldown < 0.0f) r.cooldown = 0.0f;

		r.volume = static_cast<float>(m_pSliderVol->GetValue()) / 100.0f;
		r.pitch = static_cast<float>(m_pSliderPitch->GetValue()) / 100.0f;
#if SOUNDPAD_ENABLE_DSP
		r.fxReverb = m_pCheckReverb->IsSelected();
		r.fxZigzag = m_pCheckZigzag->IsSelected();
#endif

		if (m_reactionIndex >= 0)
			CSoundpadConfig::Get().UpdateReaction(m_reactionIndex, r);
		else
			CSoundpadConfig::Get().AddReaction(r);

		CSoundpadConfig::Get().Save(false);

		if (m_pMainFrame)
			m_pMainFrame->RefreshReactionsList();

		if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
			CloseModal();
		else
			Close();
	}
	else if (_stricmp(command, "browse_sound") == 0)
	{
		if (m_hFileOpenDialog.Get())
		{
			m_hFileOpenDialog->Close();
			m_hFileOpenDialog->MarkForDeletion();
			m_hFileOpenDialog = nullptr;
		}

		char startDir[MAX_PATH] = {};
		const char* gamedir = gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : "svencoop";
		StringCchPrintfA(startDir, sizeof(startDir), "%s/sound", gamedir);

		m_hFileOpenDialog = new vgui::FileOpenDialog(this, "Select Sound for Trigger (*.wav, *.mp3, *.ogg)", vgui::FOD_OPEN);
		m_hFileOpenDialog->AddFilter("*.wav;*.mp3;*.ogg", "Audio Files (*.wav, *.mp3, *.ogg)", true);
		m_hFileOpenDialog->AddFilter("*.wav", "WAV Audio (*.wav)", false);
		m_hFileOpenDialog->AddFilter("*.mp3", "MP3 Audio (*.mp3)", false);
		m_hFileOpenDialog->AddFilter("*.ogg", "OGG Vorbis (*.ogg)", false);
		m_hFileOpenDialog->SetStartDirectory(startDir);
		m_hFileOpenDialog->AddActionSignalTarget(this);
		m_hFileOpenDialog->DoModal();
	}
	else
	{
		BaseClass::OnCommand(command);
	}
}

void CReactionEditDialog::OnFileSelected(KeyValues* kv)
{
	if (!kv) return;
	const char* fullpath = kv->GetString("fullpath", "");
	if (!fullpath || !*fullpath) return;

	std::string norm = fullpath;
	std::replace(norm.begin(), norm.end(), '\\', '/');
	size_t soundPos = norm.find("/sound/");
	if (soundPos != std::string::npos)
		norm = norm.substr(soundPos + 7);

	m_pEntrySound->SetText(norm.c_str());
}

// -------------------------------------------------------------------------------------------------
// Sequencer Timeline Step Dialog
// -------------------------------------------------------------------------------------------------
CSequenceStepDialog::CSequenceStepDialog(vgui::Panel* parent, CSoundpadFrame* mainFrame, int seqIndex, int stepIndex)
	: BaseClass(parent, "SequenceStepDialog"), m_pMainFrame(mainFrame), m_seqIndex(seqIndex), m_stepIndex(stepIndex)
{
	SetSize(450, 360);
	SetMinimumSize(410, 330);
	SetSizeable(false);
	SetMoveable(true);
	SetTitle(stepIndex >= 0 ? "Edit Timeline Step" : "Add Timeline Step", true);

	m_pLabelSound = new vgui::Label(this, "LabelSound", "Sound Path:");
	m_pEntrySound = new vgui::TextEntry(this, "EntrySound");
	m_pBtnBrowseSound = new vgui::Button(this, "BtnBrowseSound", "Browse...", this, "browse_sound");
	m_pBtnBrowseSound->AddActionSignalTarget(this);

	m_pLabelStart = new vgui::Label(this, "LabelStart", "Start Slice (sec):");
	m_pEntryStart = new vgui::TextEntry(this, "EntryStart");

	m_pLabelEnd = new vgui::Label(this, "LabelEnd", "End Slice (sec):");
	m_pEntryEnd = new vgui::TextEntry(this, "EntryEnd");
#if !SOUNDPAD_ENABLE_SLICER
	m_pLabelStart->SetVisible(false);
	m_pEntryStart->SetVisible(false);
	m_pLabelEnd->SetVisible(false);
	m_pEntryEnd->SetVisible(false);
#endif

	m_pLabelRepeats = new vgui::Label(this, "LabelRepeats", "Repeats (count):");
	m_pEntryRepeats = new vgui::TextEntry(this, "EntryRepeats");

	m_pLabelPause = new vgui::Label(this, "LabelPause", "Pause After (sec):");
	m_pEntryPause = new vgui::TextEntry(this, "EntryPause");

	m_pLabelPitchStep = new vgui::Label(this, "LabelPStep", "Pitch Step/rep:");
	m_pEntryPitchStep = new vgui::TextEntry(this, "EntryPStep");

#if SOUNDPAD_ENABLE_DSP
	m_pCheckReverb = new vgui::CheckButton(this, "CheckReverb", "Reverb Echo");
	m_pCheckZigzag = new vgui::CheckButton(this, "CheckZigzag", "Zigzag Tremolo");
	m_pLabelFxHint = new vgui::Label(this, "FxHint", "Reverb / Zigzag apply to WAV only.");
#endif

	m_pBtnSave = new vgui::Button(this, "BtnSave", "Save Step");
	m_pBtnSave->SetCommand("save_step");
	m_pBtnSave->AddActionSignalTarget(this);

	m_pBtnCancel = new vgui::Button(this, "BtnCancel", "Cancel");
	m_pBtnCancel->SetCommand("cancel_step");
	m_pBtnCancel->AddActionSignalTarget(this);

	if (seqIndex >= 0 && stepIndex >= 0)
	{
		const auto& seqs = CSoundpadConfig::Get().GetSequences();
		if (seqIndex < static_cast<int>(seqs.size()) && stepIndex < static_cast<int>(seqs[seqIndex].steps.size()))
		{
			const auto& st = seqs[seqIndex].steps[stepIndex];
			m_pEntrySound->SetText(st.soundPath.c_str());
			char b1[32] = {}, b2[32] = {}, b3[32] = {}, b4[32] = {}, b5[32] = {};
			StringCchPrintfA(b1, sizeof(b1), "%.2f", st.startSec);
			StringCchPrintfA(b2, sizeof(b2), "%.2f", st.endSec);
			StringCchPrintfA(b3, sizeof(b3), "%d", st.repeats);
			StringCchPrintfA(b4, sizeof(b4), "%.2f", st.pauseAfterSec);
			StringCchPrintfA(b5, sizeof(b5), "%.2f", st.pitchStep);
			m_pEntryStart->SetText(b1);
			m_pEntryEnd->SetText(b2);
			m_pEntryRepeats->SetText(b3);
			m_pEntryPause->SetText(b4);
			m_pEntryPitchStep->SetText(b5);
#if SOUNDPAD_ENABLE_DSP
			m_pCheckReverb->SetSelected(st.fxReverb);
			m_pCheckZigzag->SetSelected(st.fxZigzag);
#endif
		}
	}
	else
	{
		if (m_pMainFrame)
		{
			SoundItem* cur = m_pMainFrame->GetSelectedSound();
			if (cur && !cur->path.empty())
			{
				m_pEntrySound->SetText(cur->path.c_str());
			}
		}
		m_pEntryStart->SetText("0.00");
		m_pEntryEnd->SetText("0.25");
		m_pEntryRepeats->SetText("4");
		m_pEntryPause->SetText("0.02");
		m_pEntryPitchStep->SetText("0.02");
	}
}

CSequenceStepDialog::~CSequenceStepDialog()
{
	if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
	{
		CloseModal();
	}
	if (m_hFileOpenDialog.Get())
	{
		m_hFileOpenDialog->Close();
		m_hFileOpenDialog->MarkForDeletion();
		m_hFileOpenDialog = nullptr;
	}
}

void CSequenceStepDialog::OnClose()
{
	if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
	{
		CloseModal();
	}
	BaseClass::OnClose();
}

void CSequenceStepDialog::OnKeyCodeTyped(vgui::KeyCode code)
{
	if (code == vgui::KEY_ESCAPE)
	{
		if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
			CloseModal();
		else
			Close();
		return;
	}
	BaseClass::OnKeyCodeTyped(code);
}

void CSequenceStepDialog::PerformLayout()
{
	BaseClass::PerformLayout();
	int w = GetWide();
	int curY = 36;
	const int margin = 16;
	const int labelW = 140;
	const int fieldW = w - margin * 2 - labelW - 10;

	m_pLabelSound->SetBounds(margin, curY, labelW, 22);
	m_pEntrySound->SetBounds(margin + labelW + 10, curY, fieldW - 75, 22);
	m_pBtnBrowseSound->SetBounds(margin + labelW + 10 + fieldW - 70, curY, 70, 22);
	curY += 28;

#if SOUNDPAD_ENABLE_SLICER
	m_pLabelStart->SetBounds(margin, curY, labelW, 22);
	m_pEntryStart->SetBounds(margin + labelW + 10, curY, fieldW, 22); curY += 28;

	m_pLabelEnd->SetBounds(margin, curY, labelW, 22);
	m_pEntryEnd->SetBounds(margin + labelW + 10, curY, fieldW, 22); curY += 28;
#endif

	m_pLabelRepeats->SetBounds(margin, curY, labelW, 22);
	m_pEntryRepeats->SetBounds(margin + labelW + 10, curY, fieldW, 22); curY += 28;

	m_pLabelPause->SetBounds(margin, curY, labelW, 22);
	m_pEntryPause->SetBounds(margin + labelW + 10, curY, fieldW, 22); curY += 28;

	m_pLabelPitchStep->SetBounds(margin, curY, labelW, 22);
	m_pEntryPitchStep->SetBounds(margin + labelW + 10, curY, fieldW, 22); curY += 30;

#if SOUNDPAD_ENABLE_DSP
	m_pCheckReverb->SetBounds(margin, curY, 180, 22);
	m_pCheckZigzag->SetBounds(margin + 190, curY, 180, 22); curY += 36;
	m_pLabelFxHint->SetBounds(margin, curY, w - 2 * margin, 16); curY += 20;
#endif

	m_pBtnSave->SetBounds(w - 200, curY, 90, 26);
	m_pBtnCancel->SetBounds(w - 100, curY, 85, 26);
}

void CSequenceStepDialog::OnCommand(const char* command)
{
	if (_stricmp(command, "cancel_step") == 0)
	{
		if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
			CloseModal();
		else
			Close();
	}
	else if (_stricmp(command, "save_step") == 0)
	{
		char snd[MAX_PATH] = {}, bStart[32] = {}, bEnd[32] = {}, bReps[32] = {}, bPause[32] = {}, bPStep[32] = {};
		m_pEntrySound->GetText(snd, sizeof(snd));
		m_pEntryStart->GetText(bStart, sizeof(bStart));
		m_pEntryEnd->GetText(bEnd, sizeof(bEnd));
		m_pEntryRepeats->GetText(bReps, sizeof(bReps));
		m_pEntryPause->GetText(bPause, sizeof(bPause));
		m_pEntryPitchStep->GetText(bPStep, sizeof(bPStep));

		SequenceStep st;
		st.soundPath = snd;
#if SOUNDPAD_ENABLE_SLICER
		st.startSec = static_cast<float>(atof(bStart));
		st.endSec = static_cast<float>(atof(bEnd));
#else
		// Slicer UI is hidden; steps always reference the whole file.
		st.startSec = 0.0f;
		st.endSec = 0.0f;
#endif
		st.repeats = (std::max)(1, atoi(bReps));
		st.pauseAfterSec = static_cast<float>(atof(bPause));
		st.pitchStep = static_cast<float>(atof(bPStep));
#if SOUNDPAD_ENABLE_DSP
		st.fxReverb = m_pCheckReverb->IsSelected();
		st.fxZigzag = m_pCheckZigzag->IsSelected();
#endif

		auto& seqs = CSoundpadConfig::Get().GetSequences();
		if (m_seqIndex >= 0 && m_seqIndex < static_cast<int>(seqs.size()))
		{
			if (m_stepIndex >= 0 && m_stepIndex < static_cast<int>(seqs[m_seqIndex].steps.size()))
				seqs[m_seqIndex].steps[m_stepIndex] = st;
			else
				seqs[m_seqIndex].steps.push_back(st);

			CSoundpadConfig::Get().Save(false);
		}

		if (m_pMainFrame)
			m_pMainFrame->RefreshSequencerList();

		if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
			CloseModal();
		else
			Close();
	}
	else if (_stricmp(command, "browse_sound") == 0)
	{
		if (m_hFileOpenDialog.Get())
		{
			m_hFileOpenDialog->Close();
			m_hFileOpenDialog->MarkForDeletion();
			m_hFileOpenDialog = nullptr;
		}

		char startDir[MAX_PATH] = {};
		const char* gamedir = gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : "svencoop";
		StringCchPrintfA(startDir, sizeof(startDir), "%s/sound", gamedir);

		m_hFileOpenDialog = new vgui::FileOpenDialog(this, "Select Sound for Macro Step (*.wav, *.mp3, *.ogg)", vgui::FOD_OPEN);
		m_hFileOpenDialog->AddFilter("*.wav;*.mp3;*.ogg", "Audio Files (*.wav, *.mp3, *.ogg)", true);
		m_hFileOpenDialog->AddFilter("*.wav", "WAV Audio (*.wav)", false);
		m_hFileOpenDialog->AddFilter("*.mp3", "MP3 Audio (*.mp3)", false);
		m_hFileOpenDialog->AddFilter("*.ogg", "OGG Vorbis (*.ogg)", false);
		m_hFileOpenDialog->SetStartDirectory(startDir);
		m_hFileOpenDialog->AddActionSignalTarget(this);
		m_hFileOpenDialog->DoModal();
	}
	else
	{
		BaseClass::OnCommand(command);
	}
}

void CSequenceStepDialog::OnFileSelected(KeyValues* kv)
{
	if (!kv) return;
	const char* fullpath = kv->GetString("fullpath", "");
	if (!fullpath || !*fullpath) return;

	std::string norm = fullpath;
	std::replace(norm.begin(), norm.end(), '\\', '/');
	size_t soundPos = norm.find("/sound/");
	if (soundPos != std::string::npos)
		norm = norm.substr(soundPos + 7);

	m_pEntrySound->SetText(norm.c_str());
}



// -------------------------------------------------------------------------------------------------
// Dialog for Manual Ban
// -------------------------------------------------------------------------------------------------
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
CManualBanDialog::CManualBanDialog(vgui::Panel* parent, CSoundpadFrame* mainFrame)
	: BaseClass(parent, "ManualBanDialog"), m_pMainFrame(mainFrame)
{
	SetSize(380, 200);
	SetMinimumSize(340, 180);
	SetSizeable(false);
	SetMoveable(true);
	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);
	SetTitle("Manual Player Ban (SteamID or Name)", true);

	m_pLabelSteamID = new vgui::Label(this, "LabelSteamID", "SteamID64 (or 0):");
	m_pEntrySteamID = new vgui::TextEntry(this, "EntrySteamID");

	m_pLabelName = new vgui::Label(this, "LabelName", "Player Name (or Nick):");
	m_pEntryName = new vgui::TextEntry(this, "EntryName");

	m_pLabelReason = new vgui::Label(this, "LabelReason", "Reason (Optional):");
	m_pEntryReason = new vgui::TextEntry(this, "EntryReason");

	m_pBtnSave = new vgui::Button(this, "BtnSave", "Ban Player", this, "save_ban");
	m_pBtnSave->AddActionSignalTarget(this);

	m_pBtnCancel = new vgui::Button(this, "BtnCancel", "Cancel", this, "cancel_ban");
	m_pBtnCancel->AddActionSignalTarget(this);
}

CManualBanDialog::~CManualBanDialog()
{
	if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
	{
		CloseModal();
	}
}

void CManualBanDialog::OnClose()
{
	if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
	{
		CloseModal();
	}
	BaseClass::OnClose();
	MarkForDeletion();
}

void CManualBanDialog::OnKeyCodeTyped(vgui::KeyCode code)
{
	if (code == vgui::KEY_ESCAPE)
	{
		if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
			CloseModal();
		else
			Close();
		return;
	}
	BaseClass::OnKeyCodeTyped(code);
}

void CManualBanDialog::PerformLayout()
{
	BaseClass::PerformLayout();
	int w = GetWide();
	int curY = 32;
	const int margin = 16;
	const int labelW = 140;
	const int fieldW = (std::max)(120, w - margin * 2 - labelW - 10);

	m_pLabelSteamID->SetBounds(margin, curY, labelW, 20);
	m_pEntrySteamID->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 26;

	m_pLabelName->SetBounds(margin, curY, labelW, 20);
	m_pEntryName->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 26;

	m_pLabelReason->SetBounds(margin, curY, labelW, 20);
	m_pEntryReason->SetBounds(margin + labelW + 10, curY, fieldW, 20); curY += 32;

	m_pBtnSave->SetBounds(w - 200, curY, 95, 24);
	m_pBtnCancel->SetBounds(w - 95, curY, 80, 24);
}

void CManualBanDialog::OnCommand(const char* command)
{
	if (_stricmp(command, "cancel_ban") == 0)
	{
		if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
			CloseModal();
		else
			Close();
	}
	else if (_stricmp(command, "save_ban") == 0)
	{
		char idStr[64] = {}, nameStr[64] = {}, reasonStr[128] = {};
		m_pEntrySteamID->GetText(idStr, sizeof(idStr));
		m_pEntryName->GetText(nameStr, sizeof(nameStr));
		m_pEntryReason->GetText(reasonStr, sizeof(reasonStr));

		uint64_t steamID = std::strtoull(idStr, nullptr, 10);
		if (steamID != 0 || nameStr[0])
		{
			cl_entity_t* localEnt = gEngfuncs.GetLocalPlayer ? gEngfuncs.GetLocalPlayer() : nullptr;
			if (localEnt && localEnt->player != 0 && localEnt->index > 0 && gEngfuncs.pfnGetPlayerInfo)
			{
				hud_player_info_t localInfo = {};
				gEngfuncs.pfnGetPlayerInfo(localEnt->index, &localInfo);
				if (localInfo.name && nameStr[0] && _stricmp(localInfo.name, nameStr) == 0)
				{
					if (gEngfuncs.Con_Printf)
						gEngfuncs.Con_Printf("[Soundpad] Cannot ban yourself!\n");
					return;
				}
				uint64_t mySteamID = GetPlayerSteamID(localEnt->index);
				if (mySteamID == 0) mySteamID = localInfo.m_nSteamID;
				if (steamID != 0 && mySteamID != 0 && mySteamID == steamID)
				{
					if (gEngfuncs.Con_Printf)
						gEngfuncs.Con_Printf("[Soundpad] Cannot ban yourself!\n");
					return;
				}
			}

			CSoundpadConfig::Get().BanPlayer(steamID, nameStr[0] ? nameStr : "Player", reasonStr[0] ? reasonStr : "Manual ban");
			if (m_pMainFrame)
				m_pMainFrame->RefreshPlayersList();
			if (vgui::input() && vgui::input()->GetAppModalSurface() == GetVPanel())
				CloseModal();
			else
				Close();
		}
	}
	else
	{
		BaseClass::OnCommand(command);
	}
}
#endif

// -------------------------------------------------------------------------------------------------
// Main Soundpad Frame
// -------------------------------------------------------------------------------------------------
CSoundpadFrame::CSoundpadFrame(vgui::Panel* parent)
	: BaseClass(parent, "SoundpadFrame")
{
	DebuLog("CSoundpadFrame constructor entered (parent=%p)", parent);
	SetSize(780, 560);
	SetMinimumSize(700, 480);
	SetSizeable(true);
	SetMoveable(true);
	SetProportional(false);
	BaseClass::SetVisible(false);
	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);

	SetTitle("Soundpad - Voice Datagram Controller", true);

	// Top Right Header: Master Volume & Mute
	DebuLog("CSoundpadFrame: creating header controls");
	m_pCheckMute = new vgui::CheckButton(this, "CheckMute", "Mute");
	m_pCheckMute->SetSelected(CSoundpadConfig::Get().IsMuted());
	m_pCheckMute->AddActionSignalTarget(this);

	int initMv = static_cast<int>(CSoundpadConfig::Get().GetMasterVolume() * 100.0f + 0.5f);
	char mvBuf[32] = {};
	StringCchPrintfA(mvBuf, sizeof(mvBuf), "Master: %d%%", initMv);
	m_pMasterVolLabel = new vgui::Label(this, "MasterVolLabel", mvBuf);

	m_pMasterVolSlider = new vgui::Slider(this, "MasterVolSlider");
	m_pMasterVolSlider->SetRange(0, 100);
	m_pMasterVolSlider->SetValue(initMv, false);
	m_pMasterVolSlider->AddActionSignalTarget(this);

	// Tabs Controller
	DebuLog("CSoundpadFrame: creating tabs controller");
	m_pTabSheet = new vgui::PropertySheet(this, "TabSheet");
	m_pTabSheet->SetProportional(false);
	m_pTabSheet->AddActionSignalTarget(this);

	m_pTabSoundpad = new vgui::PropertyPage(m_pTabSheet, "TabSoundpad");
	m_pTabSoundpad->SetProportional(false);

	m_pTabReactions = new vgui::PropertyPage(m_pTabSheet, "TabReactions");
	m_pTabReactions->SetProportional(false);

	m_pTabSequencer = new vgui::PropertyPage(m_pTabSheet, "TabSequencer");
	m_pTabSequencer->SetProportional(false);

	m_pTabTTS = new vgui::PropertyPage(m_pTabSheet, "TabTTS");
	m_pTabTTS->SetProportional(false);

#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	m_pTabBans = new vgui::PropertyPage(m_pTabSheet, "TabBans");
	m_pTabBans->SetProportional(false);
#endif

	m_pTabSheet->AddPage(m_pTabSoundpad, "Soundpad");
	m_pTabSheet->AddPage(m_pTabReactions, "Game Reactions");
	m_pTabSheet->AddPage(m_pTabSequencer, SOUNDPAD_ENABLE_SLICER ? "Sequencer & Slicer" : "Macros");
	m_pTabSheet->AddPage(m_pTabTTS, "Text-To-Speech (TTS)");
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	m_pTabSheet->AddPage(m_pTabBans, "Player Blacklist");
#endif

	// ==============================================================================================
	// TAB 1: Soundpad
	// ==============================================================================================
	DebuLog("CSoundpadFrame: creating Tab 1 controls");
	// Left: Category Section
	m_pCatHeader = new vgui::Label(m_pTabSoundpad, "CatHeader", "Categories");
	m_pCatTree = new vgui::TreeView(m_pTabSoundpad, "CatTree");
	m_pCatTree->SetAllowMultipleSelections(false);
	m_pCatTree->EnableExpandTreeOnLeftClick(true);
	m_pCatTree->AddActionSignalTarget(this);

	m_pBtnAddCat = new vgui::Button(m_pTabSoundpad, "BtnAddCat", "+ Category", this, "add_category");
	m_pBtnAddCat->AddActionSignalTarget(this);

	// Right: Sound List Section
	m_pSoundHeader = new vgui::Label(m_pTabSoundpad, "SoundHeader", "Sound Library");
	m_pSearchFilter = new vgui::TextEntry(m_pTabSoundpad, "SearchFilter");
	m_pSearchFilter->SetMultiline(false);
	m_pSearchFilter->AddActionSignalTarget(this);

	m_pBtnAddFile = new vgui::Button(m_pTabSoundpad, "BtnAddFile", "+ File", this, "add_file");
	m_pBtnAddFile->AddActionSignalTarget(this);

	m_pBtnAddFolder = new vgui::Button(m_pTabSoundpad, "BtnAddFolder", "+ Folder", this, "add_folder");
	m_pBtnAddFolder->AddActionSignalTarget(this);

	m_pBtnRemoveSound = new vgui::Button(m_pTabSoundpad, "BtnRemoveSound", "Delete", this, "delete_sound");
	m_pBtnRemoveSound->AddActionSignalTarget(this);

	m_pBtnSave = new vgui::Button(m_pTabSoundpad, "BtnSave", "Save", this, "save_config");
	m_pBtnSave->AddActionSignalTarget(this);

	m_pSoundList = new vgui::ListPanel(m_pTabSoundpad, "SoundList");
	m_pSoundList->SetMultiselectEnabled(true);
	m_pSoundList->AddColumnHeader(0, "num", "#", 30, 0);
	m_pSoundList->AddColumnHeader(1, "title", "Title", 135, 0);
	m_pSoundList->AddColumnHeader(2, "size", "Size", 55, 0);
	m_pSoundList->AddColumnHeader(3, "hotkey", "Hotkey", 60, 0);
	m_pSoundList->AddColumnHeader(4, "trigger", "Chat Trigger", 90, 0);
	m_pSoundList->AddColumnHeader(5, "vol", "Vol", 40, 0);
	m_pSoundList->AddColumnHeader(6, "pitch", "Pitch", 45, 0);
	m_pSoundList->AddColumnHeader(7, "path", "File Path", 180, vgui::ListPanel::COLUMN_RESIZEWITHWINDOW);
	m_pSoundList->AddColumnHeader(8, "status", "State", 55, 0);
	m_pSoundList->AddActionSignalTarget(this);

	// Bottom: Controls
	m_pBtnPlay = new vgui::Button(m_pTabSoundpad, "BtnPlay", "Play (Cut)", this, "play_cut");
	m_pBtnPlay->AddActionSignalTarget(this);

	m_pBtnQueue = new vgui::Button(m_pTabSoundpad, "BtnQueue", "Queue", this, "play_queue");
	m_pBtnQueue->AddActionSignalTarget(this);

	m_pBtnPreview = new vgui::Button(m_pTabSoundpad, "BtnPreview", "Preview", this, "preview");
	m_pBtnPreview->AddActionSignalTarget(this);

	m_pBtnStop = new vgui::Button(m_pTabSoundpad, "BtnStop", "Stop", this, "stop");
	m_pBtnStop->AddActionSignalTarget(this);

	m_pBtnRandom = new vgui::Button(m_pTabSoundpad, "BtnRandom", "Random", this, "random");
	m_pBtnRandom->AddActionSignalTarget(this);

	m_pVolLabel = new vgui::Label(m_pTabSoundpad, "VolLabel", "Vol: 1.00");
	m_pVolSlider = new vgui::Slider(m_pTabSoundpad, "VolSlider");
	m_pVolSlider->SetRange(1, 100);
	m_pVolSlider->SetValue(100, false);
	m_pVolSlider->AddActionSignalTarget(this);

	m_pPitchLabel = new vgui::Label(m_pTabSoundpad, "PitchLabel", "Pitch: 1.00");
	m_pPitchSlider = new vgui::Slider(m_pTabSoundpad, "PitchSlider");
	m_pPitchSlider->SetRange(50, 200);
	m_pPitchSlider->SetValue(100, false);
	m_pPitchSlider->AddActionSignalTarget(this);


	m_pCheckDblClickPreview = new vgui::CheckButton(m_pTabSoundpad, "CheckDblClickPreview", "Dbl-click = Preview");
	m_pCheckDblClickPreview->SetSelected(true);
	m_pCheckDblClickPreview->AddActionSignalTarget(this);

	m_pCheckAutoHide = new vgui::CheckButton(m_pTabSoundpad, "CheckAutoHide", "Auto-hide in gameplay");
	m_pCheckAutoHide->SetSelected(sp_autohide ? (sp_autohide->value > 0.0f) : true);
	m_pCheckAutoHide->AddActionSignalTarget(this);

	m_pStatusLabel = new vgui::Label(m_pTabSoundpad, "StatusLabel", "Status: Ready");

	// ==============================================================================================
	// TAB 2: Reactions & Triggers
	// ==============================================================================================
	m_pBtnAddReaction = new vgui::Button(m_pTabReactions, "BtnAddReaction", "+ Add Reaction", this, "add_reaction");
	m_pBtnAddReaction->AddActionSignalTarget(this);

	m_pBtnEditReaction = new vgui::Button(m_pTabReactions, "BtnEditReaction", "Edit Reaction", this, "edit_reaction");
	m_pBtnEditReaction->AddActionSignalTarget(this);

	m_pBtnDeleteReaction = new vgui::Button(m_pTabReactions, "BtnDelReaction", "Delete", this, "delete_reaction");
	m_pBtnDeleteReaction->AddActionSignalTarget(this);

	m_pBtnTestReaction = new vgui::Button(m_pTabReactions, "BtnTestReaction", "Test (Fire)", this, "test_reaction");
	m_pBtnTestReaction->AddActionSignalTarget(this);

	m_pBtnSaveReactions = new vgui::Button(m_pTabReactions, "BtnSaveReactions", "Save Config", this, "save_reactions");
	m_pBtnSaveReactions->AddActionSignalTarget(this);

	m_pReactionList = new vgui::ListPanel(m_pTabReactions, "ReactionList");
	m_pReactionList->AddColumnHeader(0, "num", "#", 32, 0);
	m_pReactionList->AddColumnHeader(1, "sound", "Sound File", 175, 0);
	m_pReactionList->AddColumnHeader(2, "event", "Event", 75, 0);
	m_pReactionList->AddColumnHeader(3, "condition", "Condition / Filter", 120, 0);
	m_pReactionList->AddColumnHeader(4, "chance", "Chance", 65, 0);
	m_pReactionList->AddColumnHeader(5, "cooldown", "CD (s)", 50, 0);
	m_pReactionList->AddColumnHeader(6, "vol", "Vol", 40, 0);
	m_pReactionList->AddColumnHeader(7, "pitch", "Pitch", 45, 0);
	m_pReactionList->AddColumnHeader(8, "fx", "DSP / FX", 100, vgui::ListPanel::COLUMN_RESIZEWITHWINDOW);
	m_pReactionList->AddActionSignalTarget(this);

	// ==============================================================================================
	// TAB 3: Sequencer & Slicer
	// ==============================================================================================
	m_pSeqListLabel = new vgui::Label(m_pTabSequencer, "SeqListLabel", "Macros / Sequences");
	m_pSeqList = new vgui::ListPanel(m_pTabSequencer, "SeqList");
	m_pSeqList->AddColumnHeader(0, "num", "#", 28, 0);
	m_pSeqList->AddColumnHeader(1, "name", "Macro Name", 115, 0);
	m_pSeqList->AddColumnHeader(2, "hotkey", "Hotkey", 50, 0);
	m_pSeqList->AddColumnHeader(3, "steps", "Steps", 45, 0);
	m_pSeqList->AddActionSignalTarget(this);

	m_pBtnAddSeq = new vgui::Button(m_pTabSequencer, "BtnAddSeq", "+ Macro", this, "add_sequence");
	m_pBtnAddSeq->AddActionSignalTarget(this);

	m_pBtnDelSeq = new vgui::Button(m_pTabSequencer, "BtnDelSeq", "Delete", this, "delete_sequence");
	m_pBtnDelSeq->AddActionSignalTarget(this);

	m_pBtnPlaySeq = new vgui::Button(m_pTabSequencer, "BtnPlaySeq", "Play Macro", this, "play_sequence");
	m_pBtnPlaySeq->AddActionSignalTarget(this);

	m_pBtnAddSeqToLib = new vgui::Button(m_pTabSequencer, "BtnAddSeqToLib", "+ To Library", this, "add_seq_to_lib");
	m_pBtnAddSeqToLib->AddActionSignalTarget(this);

	m_pStepsListLabel = new vgui::Label(m_pTabSequencer, "StepsListLabel", "Timeline Sequence Steps");
	m_pSeqStepsList = new vgui::ListPanel(m_pTabSequencer, "SeqStepsList");
	m_pSeqStepsList->AddColumnHeader(0, "step", "#", 28, 0);
	m_pSeqStepsList->AddColumnHeader(1, "sound", "Sound", 130, 0);
#if SOUNDPAD_ENABLE_SLICER
	m_pSeqStepsList->AddColumnHeader(2, "slice", "Slice [In..Out]", 85, 0);
	m_pSeqStepsList->AddColumnHeader(3, "repeats", "Reps", 45, 0);
	m_pSeqStepsList->AddColumnHeader(4, "pause", "Pause", 50, 0);
	m_pSeqStepsList->AddColumnHeader(5, "pitch", "Pitch", 45, 0);
	m_pSeqStepsList->AddColumnHeader(6, "fx", "FX", 70, vgui::ListPanel::COLUMN_RESIZEWITHWINDOW);
#else
	m_pSeqStepsList->AddColumnHeader(2, "repeats", "Reps", 45, 0);
	m_pSeqStepsList->AddColumnHeader(3, "pause", "Pause", 50, 0);
	m_pSeqStepsList->AddColumnHeader(4, "pitch", "Pitch", 45, 0);
	m_pSeqStepsList->AddColumnHeader(5, "fx", "FX", 70, vgui::ListPanel::COLUMN_RESIZEWITHWINDOW);
#endif
	m_pSeqStepsList->AddActionSignalTarget(this);

	m_pBtnAddStep = new vgui::Button(m_pTabSequencer, "BtnAddStep", "+ Step", this, "add_seq_step");
	m_pBtnAddStep->AddActionSignalTarget(this);

	m_pBtnEditStep = new vgui::Button(m_pTabSequencer, "BtnEditStep", "Edit Step", this, "edit_seq_step");
	m_pBtnEditStep->AddActionSignalTarget(this);

	m_pBtnDelStep = new vgui::Button(m_pTabSequencer, "BtnDelStep", "Remove", this, "delete_seq_step");
	m_pBtnDelStep->AddActionSignalTarget(this);

	// Timeline Slicer
	m_pTimelineLabel = new vgui::Label(m_pTabSequencer, "TimelineLabel", "Slice Slicer: 0.00s .. 0.00s (Total: 0.00s)");
	m_pTimelineSlider = new vgui::Slider(m_pTabSequencer, "TimelineSlider");
	m_pTimelineSlider->SetRange(0, 1000);
	m_pTimelineSlider->SetValue(0, false);
	m_pTimelineSlider->AddActionSignalTarget(this);

	m_pBtnSetStart = new vgui::Button(m_pTabSequencer, "BtnSetStart", "[ Set In (Start) ]", this, "set_slice_in");
	m_pBtnSetStart->AddActionSignalTarget(this);

	m_pBtnSetEnd = new vgui::Button(m_pTabSequencer, "BtnSetEnd", "[ Set Out (End) ]", this, "set_slice_out");
	m_pBtnSetEnd->AddActionSignalTarget(this);

	m_pBtnPreviewSlice = new vgui::Button(m_pTabSequencer, "BtnPreviewSlice", "Preview Slice", this, "preview_slice");
	m_pBtnPreviewSlice->AddActionSignalTarget(this);

#if !SOUNDPAD_ENABLE_SLICER
	// Slicer is hidden until it works for every format; the timeline strip stays empty.
	m_pTimelineLabel->SetVisible(false);
	m_pTimelineSlider->SetVisible(false);
	m_pBtnSetStart->SetVisible(false);
	m_pBtnSetEnd->SetVisible(false);
	m_pBtnPreviewSlice->SetVisible(false);
#endif

	// ==============================================================================================
	// TAB 4: Text-To-Speech (Klatter Formant Engine)
	// ==============================================================================================
	m_pTtsEngineLabel = new vgui::Label(m_pTabTTS, "TtsEngineLabel", "TTS Engine:");
	m_pComboTtsEngine = new vgui::ComboBox(m_pTabTTS, "ComboTtsEngine", 2, false);
	m_pComboTtsEngine->AddItem("Klatter Formant (R.E.P.O. Robotic Cadence)", new KeyValues("data", "engine", "klatter"));
	m_pComboTtsEngine->AddItem("Windows SAPI 5 (Natural / Multilingual)", new KeyValues("data", "engine", "sapi"));

	const std::string& curEngine = CSoundpadConfig::Get().GetTtsEngine();
	if (_stricmp(curEngine.c_str(), "sapi") == 0)
		m_pComboTtsEngine->ActivateItemByRow(1);
	else
		m_pComboTtsEngine->ActivateItemByRow(0);

	m_pComboTtsEngine->AddActionSignalTarget(this);

	m_pSapiVoiceLabel = new vgui::Label(m_pTabTTS, "SapiVoiceLabel", "SAPI Voice:");
	m_pComboSapiVoice = new vgui::ComboBox(m_pTabTTS, "ComboSapiVoice", 8, false);
	m_pComboSapiVoice->AddItem("Default / Auto-Detect (Language Aware)", new KeyValues("data", "voice", ""));

	std::vector<std::string> sapiVoices = Sapi::GetAvailableVoices();
	int activeVoiceRow = 0;
	const std::string& curVoice = CSoundpadConfig::Get().GetSapiVoice();
	for (size_t i = 0; i < sapiVoices.size(); ++i)
	{
		m_pComboSapiVoice->AddItem(sapiVoices[i].c_str(), new KeyValues("data", "voice", sapiVoices[i].c_str()));
		if (!curVoice.empty() && sapiVoices[i].find(curVoice) != std::string::npos)
		{
			activeVoiceRow = static_cast<int>(i + 1);
		}
	}
	m_pComboSapiVoice->ActivateItemByRow(activeVoiceRow);
	m_pComboSapiVoice->AddActionSignalTarget(this);

	m_pTtsTextLabel = new vgui::Label(m_pTabTTS, "TtsTextLabel", "Enter message or phrase to synthesize into voice chat:");
	m_pTtsTextEntry = new vgui::TextEntry(m_pTabTTS, "TtsTextEntry");
	m_pTtsTextEntry->SetMultiline(false);
	m_pTtsTextEntry->SetText("Hello squad! Target down, I am at %hp% hp.");
	m_pTtsTextEntry->AddActionSignalTarget(this);

	m_pBtnTtsSpeak = new vgui::Button(m_pTabTTS, "BtnTtsSpeak", "Speak (Broadcast)", this, "tts_speak");
	m_pBtnTtsSpeak->AddActionSignalTarget(this);

	m_pBtnTtsPreview = new vgui::Button(m_pTabTTS, "BtnTtsPreview", "Preview (Local)", this, "tts_preview");
	m_pBtnTtsPreview->AddActionSignalTarget(this);

	m_pTtsVolLabel = new vgui::Label(m_pTabTTS, "TtsVolLabel", "Volume: 1.00");
	m_pTtsVolSlider = new vgui::Slider(m_pTabTTS, "TtsVolSlider");
	m_pTtsVolSlider->SetRange(1, 100);
	m_pTtsVolSlider->SetValue(static_cast<int>((sp_tts_volume ? sp_tts_volume->value : 1.0f) * 100.0f), false);
	m_pTtsVolSlider->AddActionSignalTarget(this);

	m_pTtsPitchLabel = new vgui::Label(m_pTabTTS, "TtsPitchLabel", "Base Pitch: 1.00");
	m_pTtsPitchSlider = new vgui::Slider(m_pTabTTS, "TtsPitchSlider");
	m_pTtsPitchSlider->SetRange(50, 200);
	m_pTtsPitchSlider->SetValue(static_cast<int>((sp_tts_pitch ? sp_tts_pitch->value : 1.0f) * 100.0f), false);
	m_pTtsPitchSlider->AddActionSignalTarget(this);

	m_pCheckTtsRepoJitter = new vgui::CheckButton(m_pTabTTS, "CheckTtsRepoJitter", "R.E.P.O. Robotic Pitch Jitter (Discrete steps on vowels)");
	m_pCheckTtsRepoJitter->SetSelected(!sp_tts_jitter || sp_tts_jitter->value > 0.0f);
	m_pCheckTtsRepoJitter->AddActionSignalTarget(this);

	m_pCheckTtsWhisper = new vgui::CheckButton(m_pTabTTS, "CheckTtsWhisper", "Whisper Synthesis (Bandpass Formant Noise)");
	m_pCheckTtsWhisper->SetSelected(sp_tts_whisper && sp_tts_whisper->value > 0.0f);
	m_pCheckTtsWhisper->AddActionSignalTarget(this);

	m_pCheckTtsAutoSay = new vgui::CheckButton(m_pTabTTS, "CheckTtsAutoSay", "Auto-voice all in-game chat messages");
	m_pCheckTtsAutoSay->SetSelected(sp_tts_autosay && sp_tts_autosay->value > 0.0f);
	m_pCheckTtsAutoSay->AddActionSignalTarget(this);

	m_pCheckTtsRandomStyle = new vgui::CheckButton(m_pTabTTS, "CheckTtsRandomStyle", "Random voice style at start (Regular / Squeak / Whisper)");
	m_pCheckTtsRandomStyle->SetSelected(CSoundpadConfig::Get().GetTtsRandomStyle());
	m_pCheckTtsRandomStyle->AddActionSignalTarget(this);

	m_pTtsHintLabel = new vgui::Label(m_pTabTTS, "TtsHintLabel", "Chat command: tts <text> | Tokens: %hp%, %armor%, %dmg%, %killer%, %victim%, %deaths%, %kills%, %map%, %name%");

#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	// ==============================================================================================
	// TAB 5: Player Blacklist & Bans
	// ==============================================================================================
	m_pPlayerHeaderLabel = new vgui::Label(m_pTabBans, "PlayerHeader", "Connected Players & Blacklist (Bans from chat voice commands):");
	m_pBtnBanPlayer = new vgui::Button(m_pTabBans, "BtnBanPlayer", "Ban / Unban Selected", this, "ban_player");
	m_pBtnBanPlayer->AddActionSignalTarget(this);

	m_pBtnAddBanManual = new vgui::Button(m_pTabBans, "BtnManualBan", "Manual Ban (SteamID64)...", this, "add_ban_manual");
	m_pBtnAddBanManual->AddActionSignalTarget(this);

	m_pBtnRefreshPlayers = new vgui::Button(m_pTabBans, "BtnRefreshPlayers", "Refresh Players", this, "refresh_players");
	m_pBtnRefreshPlayers->AddActionSignalTarget(this);

	m_pCheckDotStop = new vgui::CheckButton(m_pTabBans, "CheckDotStop", "Allow '.' in chat to stop audio");
	m_pCheckDotStop->SetSelected(CSoundpadConfig::Get().IsDotStopEnabled());
	m_pCheckDotStop->AddActionSignalTarget(this);

	m_pPlayerList = new vgui::ListPanel(m_pTabBans, "PlayerList");
	m_pPlayerList->AddColumnHeader(0, "slot", "Slot", 55, 0);
	m_pPlayerList->AddColumnHeader(1, "name", "Player Name", 200, 0);
	m_pPlayerList->AddColumnHeader(2, "steamid", "SteamID64", 200, 0);
	m_pPlayerList->AddColumnHeader(3, "status", "Status", 110, 0);
	m_pPlayerList->SetEmptyListText("No players connected");
	m_pPlayerList->AddActionSignalTarget(this);
#endif

	m_pContextMenu = new vgui::Menu(this, "ContextMenu");
	m_pContextMenu->AddMenuItem("preview", "Preview (Local Only)", "preview", this);
	m_pContextMenu->AddSeparator();
	m_pContextMenu->AddMenuItem("play_cut", "Play (Cut)", "play_cut", this);
	m_pContextMenu->AddMenuItem("play_queue", "Play (Queue)", "play_queue", this);
	m_pContextMenu->AddSeparator();
	m_pContextMenu->AddMenuItem("set_hotkey", "Set Hotkey...", "set_hotkey", this);
	m_pContextMenu->AddMenuItem("set_chat_trigger", "Set Chat Trigger...", "set_chat_trigger", this);
	m_pContextMenu->AddMenuItem("use_filename_trigger", "Use Filename as Trigger", "use_filename_trigger", this);
	m_pContextMenu->AddMenuItem("clear_chat_trigger", "Clear Chat Trigger", "clear_chat_trigger", this);
	m_pContextMenu->AddSeparator();
	m_pContextMenu->AddMenuItem("move_to_category", "Move to Category...", "move_to_category", this);
	m_pContextMenu->AddSeparator();
	m_pContextMenu->AddMenuItem("add_reaction_for_sound", "Create Game Reaction...", "add_reaction", this);
	m_pContextMenu->AddSeparator();
	m_pContextMenu->AddMenuItem("add_file", "Add Sound File (*.wav;*.mp3;*.ogg)...", "add_file", this);
	m_pContextMenu->AddMenuItem("add_folder", "Add Folder of Sounds...", "add_folder", this);
	m_pContextMenu->AddMenuItem("delete_sound", "Delete Sound", "delete_sound", this);
	m_pContextMenu->AddSeparator();
	m_pContextMenu->AddMenuItem("add_category", "New Category...", "add_category", this);
	m_pContextMenu->AddMenuItem("save_config", "Save Library (soundpad.vdf)", "save_config", this);

	m_pReactionContextMenu = new vgui::Menu(this, "ReactionContextMenu");
	m_pReactionContextMenu->AddMenuItem("add_reaction", "Add Reaction Trigger...", "add_reaction", this);
	m_pReactionContextMenu->AddMenuItem("edit_reaction", "Edit Selected Trigger...", "edit_reaction", this);
	m_pReactionContextMenu->AddMenuItem("test_reaction", "Test Trigger", "test_reaction", this);
	m_pReactionContextMenu->AddSeparator();
	m_pReactionContextMenu->AddMenuItem("delete_reaction", "Delete Reaction Trigger", "delete_reaction", this);

	m_selectedCategory = "All";
	DebuLog("CSoundpadFrame: calling initial Refresh lists...");
	RefreshCategories();
	RefreshSoundList();
	RefreshReactionsList();
	RefreshSequencerList();
	RefreshPlayersList();
	DebuLog("CSoundpadFrame: constructor finished successfully");
}

template<class T>
static void SafeCloseDialog(vgui::DHANDLE<T>& hDlg)
{
	if (hDlg.Get())
	{
		if (vgui::input() && vgui::input()->GetAppModalSurface() == hDlg->GetVPanel())
		{
			hDlg->CloseModal();
		}
		hDlg->Close();
		hDlg->MarkForDeletion();
		hDlg = nullptr;
	}
}

void CSoundpadFrame::CloseAllSubDialogs()
{
	SafeCloseDialog(m_hFileOpenDialog);
	SafeCloseDialog(m_hInputDialog);
	SafeCloseDialog(m_hReactionDialog);
	SafeCloseDialog(m_hStepDialog);
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	SafeCloseDialog(m_hManualBanDialog);
#endif

	if (m_pContextMenu)
	{
		if (m_pContextMenu->IsVisible())
		{
			m_pContextMenu->SetVisible(false);
		}
	}
	if (m_pReactionContextMenu)
	{
		if (m_pReactionContextMenu->IsVisible())
		{
			m_pReactionContextMenu->SetVisible(false);
		}
	}
}

static void InvalidateSoundFileCache();

void CSoundpadFrame::SetVisible(bool state)
{
	DebuLog("CSoundpadFrame::SetVisible: state=%d", state);
	if (!state)
	{
		CloseAllSubDialogs();

		if (vgui::input() && vgui::ipanel())
		{
			vgui::VPANEL myVPanel = GetVPanel();
			if (myVPanel != 0)
			{
				vgui::VPANEL modal = vgui::input()->GetAppModalSurface();
				if (modal != 0 && (modal == myVPanel || vgui::ipanel()->HasParent(modal, myVPanel)))
				{
					vgui::input()->ReleaseAppModalSurface();
				}
			}
		}
	}
	else
	{
		InvalidateSoundFileCache();
	}

	BaseClass::SetVisible(state);
}

void CSoundpadFrame::OnClose()
{
	g_bOpenedFromCommand = false;
	g_bReopenOnGameUI = false;
	BaseClass::OnClose();
}

void CSoundpadFrame::OnCloseFrameButtonPressed()
{
	g_bOpenedFromCommand = false;
	g_bReopenOnGameUI = false;
	BaseClass::OnCloseFrameButtonPressed();
}

CSoundpadFrame::~CSoundpadFrame()
{
	CloseAllSubDialogs();

	m_pContextMenu = nullptr;
	m_pReactionContextMenu = nullptr;
}

void CSoundpadFrame::ApplySchemeSettings(vgui::IScheme* pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);

	if (pScheme && m_pCatTree)
	{
		Color textColor = pScheme->GetColor("ListPanel.TextColor", pScheme->GetColor("WindowFgColor", Color(216, 222, 211, 255)));
		Color bgColor = pScheme->GetColor("ListPanel.BgColor", pScheme->GetColor("WindowBgColor", Color(32, 32, 32, 255)));
		Color selectBg = pScheme->GetColor("ListPanel.SelectedBgColor", Color(60, 95, 140, 255));
		Color selectFg = pScheme->GetColor("ListPanel.SelectedTextColor", Color(255, 255, 255, 255));

		for (int i = 0; i <= m_pCatTree->GetHighestItemID(); ++i)
		{
			if (m_pCatTree->IsItemIDValid(i))
			{
				m_pCatTree->SetItemFgColor(i, textColor);
				m_pCatTree->SetItemBgColor(i, bgColor);
				m_pCatTree->SetItemSelectionTextColor(i, selectFg);
				m_pCatTree->SetItemSelectionBgColor(i, selectBg);
			}
		}
	}
}

void CSoundpadFrame::RefreshCategories()
{
	if (!m_pCatTree)
		return;

	DebuLog("RefreshCategories: entered");
	m_pCatTree->RemoveAll();

	Color textColor(216, 222, 211, 255);
	Color bgColor(32, 32, 32, 255);
	Color selectBg(60, 95, 140, 255);
	Color selectFg(255, 255, 255, 255);
	if (vgui::scheme() && GetScheme() != 0)
	{
		vgui::IScheme* pScheme = vgui::scheme()->GetIScheme(GetScheme());
		if (pScheme)
		{
			textColor = pScheme->GetColor("ListPanel.TextColor", pScheme->GetColor("WindowFgColor", textColor));
			bgColor = pScheme->GetColor("ListPanel.BgColor", pScheme->GetColor("WindowBgColor", bgColor));
			selectBg = pScheme->GetColor("ListPanel.SelectedBgColor", selectBg);
			selectFg = pScheme->GetColor("ListPanel.SelectedTextColor", selectFg);
		}
	}

	KeyValues* kvAll = new KeyValues("root");
	kvAll->SetString("text", "All Sounds");
	kvAll->SetString("cat", "All");
	int rootID = m_pCatTree->AddItem(kvAll, -1);
	kvAll->deleteThis();
	if (rootID >= 0)
	{
		m_pCatTree->SetItemFgColor(rootID, textColor);
		m_pCatTree->SetItemBgColor(rootID, bgColor);
		m_pCatTree->SetItemSelectionTextColor(rootID, selectFg);
		m_pCatTree->SetItemSelectionBgColor(rootID, selectBg);
	}

	const auto& cats = CSoundpadConfig::Get().GetRootCategories();
	for (const auto& c : cats)
	{
		KeyValues* kv = new KeyValues("cat");
		kv->SetString("text", c.name.c_str());
		kv->SetString("cat", c.name.c_str());
		int catID = m_pCatTree->AddItem(kv, rootID);
		kv->deleteThis();
		if (catID >= 0)
		{
			m_pCatTree->SetItemFgColor(catID, textColor);
			m_pCatTree->SetItemBgColor(catID, bgColor);
			m_pCatTree->SetItemSelectionTextColor(catID, selectFg);
			m_pCatTree->SetItemSelectionBgColor(catID, selectBg);
		}

		for (const auto& sub : c.subcategories)
		{
			KeyValues* subKv = new KeyValues("subcat");
			subKv->SetString("text", sub.name.c_str());
			subKv->SetString("cat", sub.name.c_str());
			int subID = m_pCatTree->AddItem(subKv, catID);
			subKv->deleteThis();
			if (subID >= 0)
			{
				m_pCatTree->SetItemFgColor(subID, textColor);
				m_pCatTree->SetItemBgColor(subID, bgColor);
				m_pCatTree->SetItemSelectionTextColor(subID, selectFg);
				m_pCatTree->SetItemSelectionBgColor(subID, selectBg);
			}
		}
		m_pCatTree->ExpandItem(catID, true);
	}
	m_pCatTree->ExpandItem(rootID, true);
}

static inline char SafeToLower(char c)
{
	unsigned char uc = static_cast<unsigned char>(c);
	if (uc >= 'A' && uc <= 'Z')
		return static_cast<char>(uc + ('a' - 'A'));
	if (uc >= 0xC0 && uc <= 0xDF) // Windows-1251 Cyrillic uppercase
		return static_cast<char>(uc + 0x20);
	if (uc == 0xA8) // Russian Yo (0xA8)
		return static_cast<char>(0xB8);
	return static_cast<char>(uc);
}

struct SoundFileInfo
{
	bool exists = false;
	unsigned int fileSize = 0;
};

static std::unordered_map<std::string, SoundFileInfo> s_soundFileExistsCache;

static void InvalidateSoundFileCache()
{
	s_soundFileExistsCache.clear();
}

static bool SoundFileExists(const std::string& rawPath, unsigned int* outSize = nullptr)
{
	if (outSize)
		*outSize = 0;

	if (rawPath.empty())
		return false;

	auto it = s_soundFileExistsCache.find(rawPath);
	if (it != s_soundFileExistsCache.end())
	{
		if (outSize)
			*outSize = it->second.fileSize;
		return it->second.exists;
	}

	auto cacheAndReturn = [&rawPath, outSize](bool exists, unsigned int size) -> bool {
		SoundFileInfo fi;
		fi.exists = exists;
		fi.fileSize = size;
		s_soundFileExistsCache[rawPath] = fi;
		if (outSize)
			*outSize = size;
		return exists;
	};

	const char* p = rawPath.c_str();
	while (*p == ' ' || *p == '"') ++p;
	std::string clean = p;
	while (!clean.empty() && (clean.back() == ' ' || clean.back() == '"'))
		clean.pop_back();

	if (clean.empty())
		return cacheAndReturn(false, 0);

	for (char& c : clean)
	{
		if (c == '\\') c = '/';
	}

	// 1. Direct Win32 check on clean path (handles absolute paths like C:/... or relative to current working dir)
	WIN32_FILE_ATTRIBUTE_DATA fad = {};
	if (GetFileAttributesExA(clean.c_str(), GetFileExInfoStandard, &fad))
	{
		if (!(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			return cacheAndReturn(true, fad.nFileSizeLow);
		}
	}

	// If clean is an absolute path (drive letter or root slash) and did not exist directly, do not prepend mod prefixes
	bool isAbsolute = (clean.size() >= 2 && clean[1] == ':') || (clean[0] == '/' || clean[0] == '\\');
	if (isAbsolute)
	{
		return cacheAndReturn(false, 0);
	}

	std::string sub = clean;
	if (_strnicmp(sub.c_str(), "sound/", 6) == 0)
		sub = sub.substr(6);

	// 2. Check engine IFileSystem
	if (g_pFileSystem)
	{
		static const char* kExts[] = { "", ".wav", ".mp3", ".ogg" };
		for (const char* ext : kExts)
		{
			char fsCandidate[MAX_PATH] = {};
			StringCchPrintfA(fsCandidate, sizeof(fsCandidate), "sound/%s%s", sub.c_str(), ext);
			if (g_pFileSystem->FileExists(fsCandidate))
			{
				unsigned int sz = g_pFileSystem->Size(fsCandidate);
				return cacheAndReturn(true, sz);
			}
			StringCchPrintfA(fsCandidate, sizeof(fsCandidate), "%s%s", sub.c_str(), ext);
			if (g_pFileSystem->FileExists(fsCandidate))
			{
				unsigned int sz = g_pFileSystem->Size(fsCandidate);
				return cacheAndReturn(true, sz);
			}
		}
	}

	// 3. Direct disk checks across candidate base directories without C++ exceptions
	const char* gamedir = (gEngfuncs.pfnGetGameDirectory && gEngfuncs.pfnGetGameDirectory())
		? gEngfuncs.pfnGetGameDirectory() : "svencoop";

	const char* candidateBases[] = {
		gamedir,
		"svencoop",
		"svencoop_downloads",
		"svencoop_addon",
		"svencoop_schinese",
		"valve",
		""
	};

	static const char* kExts[] = { "", ".wav", ".mp3", ".ogg" };
	for (const char* base : candidateBases)
	{
		for (const char* ext : kExts)
		{
			char diskPath[MAX_PATH] = {};
			if (base && *base)
				StringCchPrintfA(diskPath, sizeof(diskPath), "%s/sound/%s%s", base, sub.c_str(), ext);
			else
				StringCchPrintfA(diskPath, sizeof(diskPath), "sound/%s%s", sub.c_str(), ext);

			if (GetFileAttributesExA(diskPath, GetFileExInfoStandard, &fad))
			{
				if (!(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					return cacheAndReturn(true, fad.nFileSizeLow);
				}
			}

			if (base && *base)
				StringCchPrintfA(diskPath, sizeof(diskPath), "%s/%s%s", base, sub.c_str(), ext);
			else
				StringCchPrintfA(diskPath, sizeof(diskPath), "%s%s", sub.c_str(), ext);

			if (GetFileAttributesExA(diskPath, GetFileExInfoStandard, &fad))
			{
				if (!(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					return cacheAndReturn(true, fad.nFileSizeLow);
				}
			}
		}
	}

	return cacheAndReturn(false, 0);
}

void CSoundpadFrame::RefreshSoundList()
{
	if (!m_pSoundList)
		return;

	DebuLog("RefreshSoundList: entered, category=%s", m_selectedCategory.c_str());
	m_pSoundList->RemoveAll();

	char filterBuf[128] = {};
	if (m_pSearchFilter)
		m_pSearchFilter->GetText(filterBuf, sizeof(filterBuf));
	std::string filter(filterBuf);
	std::transform(filter.begin(), filter.end(), filter.begin(), SafeToLower);

	m_currentSounds = CSoundpadConfig::Get().GetSoundsForCategory(m_selectedCategory);

	for (size_t i = 0; i < m_currentSounds.size(); ++i)
	{
		const auto& s = m_currentSounds[i];
		if (!filter.empty())
		{
			std::string titleLower = s.title;
			std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), SafeToLower);
			std::string pathLower = s.path;
			std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), SafeToLower);

			if (titleLower.find(filter) == std::string::npos && pathLower.find(filter) == std::string::npos)
				continue;
		}

		bool exists = false;
		unsigned fileSize = 0;
		if (s.isSequence)
		{
			exists = true;
		}
		else
		{
			exists = SoundFileExists(s.path, &fileSize);
		}

		char sizeStr[32] = {};
		if (exists && !s.isSequence)
		{
			if (fileSize < 1024)
				StringCchPrintfA(sizeStr, sizeof(sizeStr), "%u B", fileSize);
			else if (fileSize < 1024 * 1024)
				StringCchPrintfA(sizeStr, sizeof(sizeStr), "%u KB", fileSize / 1024);
			else
				StringCchPrintfA(sizeStr, sizeof(sizeStr), "%.1f MB", static_cast<double>(fileSize) / (1024.0 * 1024.0));
		}
		else if (s.isSequence)
		{
			StringCchCopyA(sizeStr, sizeof(sizeStr), "Macro");
		}
		else
		{
			StringCchCopyA(sizeStr, sizeof(sizeStr), "-");
		}

		std::string displayTitle = s.title;
		if (displayTitle.empty())
		{
			std::string base = s.path;
			size_t lastSlash = base.find_last_of("/\\");
			if (lastSlash != std::string::npos) base = base.substr(lastSlash + 1);
			displayTitle = base;
		}
		if (displayTitle.empty())
		{
			displayTitle = "Untitled";
		}

		// Prevent leading '#' from triggering VGUI localization token lookup which can crash on unfound symbols
		if (!displayTitle.empty() && displayTitle[0] == '#')
		{
			displayTitle = " " + displayTitle;
		}

		if (!exists)
		{
			displayTitle += " [!]";
		}

		KeyValues* kv = new KeyValues("sound");
		char numStr[16] = {};
		StringCchPrintfA(numStr, sizeof(numStr), "%d", static_cast<int>(i + 1));
		kv->SetString("num", numStr);
		kv->SetString("title", displayTitle.c_str());
		kv->SetString("size", sizeStr);
		kv->SetString("hotkey", s.hotkey.c_str());
		kv->SetString("trigger", s.chatTrigger.c_str());
		kv->SetString("path", s.path.c_str());

		char volStr[16] = {};
		StringCchPrintfA(volStr, sizeof(volStr), "%.2f", s.volume);
		kv->SetString("vol", volStr);

		char pitchStr[16] = {};
		StringCchPrintfA(pitchStr, sizeof(pitchStr), "%.2f", s.pitch);
		kv->SetString("pitch", pitchStr);

		kv->SetString("status", exists ? "" : "MISSING");

		m_pSoundList->AddItem(kv, static_cast<unsigned int>(i), false, false);
		kv->deleteThis();
	}

	if (m_pSoundList->GetItemCount() > 0)
	{
		int firstID = m_pSoundList->GetItemIDFromRow(0);
		m_pSoundList->SetSingleSelectedItem(firstID);

		SoundItem* s = GetSelectedSound();
		if (s)
		{
			m_bUpdatingSliders = true;
			int volVal = static_cast<int>(s->volume * 100.0f + 0.5f);
			if (volVal < 1) volVal = 1;
			if (volVal > 100) volVal = 100;
			m_pVolSlider->SetValue(volVal, false);

			int pitchVal = static_cast<int>(s->pitch * 100.0f + 0.5f);
			if (pitchVal < 50) pitchVal = 50;
			if (pitchVal > 200) pitchVal = 200;
			m_pPitchSlider->SetValue(pitchVal, false);

			m_bUpdatingSliders = false;
		}
	}
	else
	{
		m_pSoundList->ClearSelectedItems();
	}
}

void CSoundpadFrame::RefreshReactionsList()
{
	if (!m_pReactionList)
		return;

	DebuLog("RefreshReactionsList: entered");
	m_pReactionList->RemoveAll();
	const auto& reactions = CSoundpadConfig::Get().GetReactions();

	for (size_t i = 0; i < reactions.size(); ++i)
	{
		const auto& r = reactions[i];
		KeyValues* kv = new KeyValues("reaction");

		char numStr[16] = {};
		StringCchPrintfA(numStr, sizeof(numStr), "%u", static_cast<unsigned>(i + 1));
		kv->SetString("num", numStr);
		kv->SetString("sound", r.soundPath.c_str());

		const char* evName = "Death";
		if (r.eventType == ReactionEventType::Damage) evName = "Damage";
		else if (r.eventType == ReactionEventType::Kill) evName = "Kill";
		else if (r.eventType == ReactionEventType::Chat) evName = "Chat";
		else if (r.eventType == ReactionEventType::Command) evName = "Command";
		else if (r.eventType == ReactionEventType::OtherDeath) evName = "Team Death";
		kv->SetString("event", evName);

		kv->SetString("condition", r.condition.empty() ? "(any)" : r.condition.c_str());

		char chanceStr[32] = {};
		if (std::fmod(r.chance, 1.0f) != 0.0f || r.chance < 1.0f)
		{
			StringCchPrintfA(chanceStr, sizeof(chanceStr), "%.1f%%", r.chance);
		}
		else
		{
			StringCchPrintfA(chanceStr, sizeof(chanceStr), "%.0f%%", r.chance);
		}
		kv->SetString("chance", chanceStr);

		char cdStr[32] = {};
		StringCchPrintfA(cdStr, sizeof(cdStr), "%.1fs", r.cooldown);
		kv->SetString("cooldown", cdStr);

		char volStr[16] = {}, pitchStr[16] = {};
		StringCchPrintfA(volStr, sizeof(volStr), "%.2f", r.volume);
		StringCchPrintfA(pitchStr, sizeof(pitchStr), "%.2f", r.pitch);
		kv->SetString("vol", volStr);
		kv->SetString("pitch", pitchStr);

		std::string fxDesc;
#if SOUNDPAD_ENABLE_DSP
		if (r.fxReverb) fxDesc += "Reverb ";
		if (r.fxZigzag) fxDesc += "Zigzag ";
#endif
		if (r.fadeInSec > 0.0f) fxDesc += "FadeIn ";
		if (r.fadeOutSec > 0.0f) fxDesc += "FadeOut ";
		if (fxDesc.empty()) fxDesc = "None";
		kv->SetString("fx", fxDesc.c_str());

		m_pReactionList->AddItem(kv, static_cast<unsigned int>(i), false, false);
		kv->deleteThis();
	}
}

void CSoundpadFrame::RefreshSequencerList()
{
	if (!m_pSeqList)
		return;

	DebuLog("RefreshSequencerList: entered");
	m_pSeqList->RemoveAll();
	const auto& seqs = CSoundpadConfig::Get().GetSequences();

	for (size_t i = 0; i < seqs.size(); ++i)
	{
		const auto& s = seqs[i];
		KeyValues* kv = new KeyValues("seq");
		char numStr[16] = {};
		StringCchPrintfA(numStr, sizeof(numStr), "%u", static_cast<unsigned>(i + 1));
		kv->SetString("num", numStr);
		kv->SetString("name", s.name.c_str());
		kv->SetString("hotkey", s.hotkey.empty() ? "-" : s.hotkey.c_str());
		char stepsStr[16] = {};
		StringCchPrintfA(stepsStr, sizeof(stepsStr), "%u", static_cast<unsigned>(s.steps.size()));
		kv->SetString("steps", stepsStr);

		m_pSeqList->AddItem(kv, static_cast<unsigned int>(i), false, false);
		kv->deleteThis();
	}

	m_pSeqStepsList->RemoveAll();
	int selItem = m_pSeqList->GetSelectedItem(0);
	int selSeq = (selItem >= 0) ? static_cast<int>(m_pSeqList->GetItemUserData(selItem)) : -1;
	if (selSeq >= 0 && selSeq < static_cast<int>(seqs.size()))
	{
		const auto& curSeq = seqs[selSeq];
		for (size_t st = 0; st < curSeq.steps.size(); ++st)
		{
			const auto& step = curSeq.steps[st];
			KeyValues* skv = new KeyValues("step");
			char stepNum[16] = {};
			StringCchPrintfA(stepNum, sizeof(stepNum), "%u", static_cast<unsigned>(st + 1));
			skv->SetString("step", stepNum);
			skv->SetString("sound", step.soundPath.c_str());

			char sliceStr[32] = {};
			StringCchPrintfA(sliceStr, sizeof(sliceStr), "%.2f..%.2fs", step.startSec, step.endSec);
			skv->SetString("slice", sliceStr);

			char repStr[16] = {};
			StringCchPrintfA(repStr, sizeof(repStr), "x%d", step.repeats);
			skv->SetString("repeats", repStr);

			char pStr[16] = {};
			StringCchPrintfA(pStr, sizeof(pStr), "%.2fs", step.pauseAfterSec);
			skv->SetString("pause", pStr);

			char pitchStr[16] = {};
			StringCchPrintfA(pitchStr, sizeof(pitchStr), "%.2f", step.pitch);
			skv->SetString("pitch", pitchStr);

			std::string fxDesc;
#if SOUNDPAD_ENABLE_DSP
			if (step.fxReverb) fxDesc += "Reverb ";
			if (step.fxZigzag) fxDesc += "Zigzag ";
#endif
			if (fxDesc.empty()) fxDesc = "-";
			skv->SetString("fx", fxDesc.c_str());

			m_pSeqStepsList->AddItem(skv, static_cast<unsigned int>(st), false, false);
			skv->deleteThis();
		}
	}
}


#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
void CSoundpadFrame::RefreshPlayersList()
{
	if (!m_pPlayerList) return;
	DebuLog("RefreshPlayersList: entered");
	m_pPlayerList->RemoveAll();

	std::vector<uint64_t> activeSteamIDs;
	std::vector<std::string> activeNames;

	cl_entity_t* localEnt = gEngfuncs.GetLocalPlayer ? gEngfuncs.GetLocalPlayer() : nullptr;
	if (!localEnt || localEnt->player == 0 || localEnt->index <= 0)
	{
		m_pPlayerList->SetEmptyListText("No players connected (Main Menu / Disconnected)");
	}
	else
	{
		const int maxClients = (gEngfuncs.GetMaxClients && gEngfuncs.GetMaxClients() > 0) ? (std::min)(32, gEngfuncs.GetMaxClients()) : 0;

		for (int slot = 1; slot <= maxClients; ++slot)
		{
			if (!gEngfuncs.pfnGetPlayerInfo)
				break;

			hud_player_info_t info = {};
			gEngfuncs.pfnGetPlayerInfo(slot, &info);
			if (!info.name || !info.name[0])
				continue;

			KeyValues* kv = new KeyValues("player");

			char slotStr[16] = {};
			StringCchPrintfA(slotStr, sizeof(slotStr), "#%d", slot);
			kv->SetString("slot", slotStr);
			kv->SetString("name", info.name);

			uint64_t playerSteamID = GetPlayerSteamID(slot);
			if (playerSteamID == 0)
				playerSteamID = info.m_nSteamID;

			char idStr[32] = {};
			StringCchPrintfA(idStr, sizeof(idStr), "%llu", static_cast<unsigned long long>(playerSteamID));
			kv->SetString("steamid", idStr);

			bool banned = CSoundpadConfig::Get().IsPlayerBanned(playerSteamID, info.name);
			if (banned)
				kv->SetString("status", "BANNED");
			else if (info.thisplayer)
				kv->SetString("status", "You");
			else
				kv->SetString("status", "OK");

			if (playerSteamID != 0)
				activeSteamIDs.push_back(playerSteamID);
			activeNames.push_back(info.name);

			m_pPlayerList->AddItem(kv, static_cast<unsigned int>(slot), false, false);
			kv->deleteThis();
		}
	}

	const auto& bans = CSoundpadConfig::Get().GetBannedPlayers();
	for (const auto& b : bans)
	{
		bool active = false;
		if (b.steamID != 0)
			active = (std::find(activeSteamIDs.begin(), activeSteamIDs.end(), b.steamID) != activeSteamIDs.end());
		else if (!b.playerName.empty())
		{
			for (const auto& an : activeNames)
			{
				if (_stricmp(an.c_str(), b.playerName.c_str()) == 0) { active = true; break; }
			}
		}

		if (!active)
		{
			KeyValues* kv = new KeyValues("player");
			kv->SetString("slot", "-");
			kv->SetString("name", b.playerName.c_str());
			char idStr[32] = {};
			StringCchPrintfA(idStr, sizeof(idStr), "%llu", static_cast<unsigned long long>(b.steamID));
			kv->SetString("steamid", idStr);
			kv->SetString("status", "BANNED (Off)");
			m_pPlayerList->AddItem(kv, 0, false, false);
			kv->deleteThis();
		}
	}
}
#endif

SoundItem* CSoundpadFrame::GetSelectedSound()
{
	int itemID = m_pSoundList->GetSelectedItem(0);
	if (itemID < 0)
		return nullptr;

	KeyValues* kv = m_pSoundList->GetItem(itemID);
	if (!kv)
		return nullptr;

	const char* path = kv->GetString("path", "");
	SoundItem* realItem = CSoundpadConfig::Get().FindSoundByPath(path);
	if (realItem)
		return realItem;

	return nullptr;
}

SoundItem* CSoundpadFrame::GetSoundByRow(int row1Based)
{
	int row = row1Based - 1;
	if (row < 0 || row >= m_pSoundList->GetItemCount())
		return nullptr;

	int itemID = m_pSoundList->GetItemIDFromRow(row);
	if (itemID < 0)
		return nullptr;

	KeyValues* kv = m_pSoundList->GetItem(itemID);
	if (!kv)
		return nullptr;

	const char* path = kv->GetString("path", "");
	SoundItem* realItem = CSoundpadConfig::Get().FindSoundByPath(path);
	if (realItem)
		return realItem;

	return nullptr;
}

void CSoundpadFrame::SelectSoundByPath(const std::string& path)
{
	for (int i = 0; i < m_pSoundList->GetItemCount(); ++i)
	{
		int itemID = m_pSoundList->GetItemIDFromRow(i);
		KeyValues* kv = m_pSoundList->GetItem(itemID);
		if (kv)
		{
			const char* p = kv->GetString("path", "");
			if (_stricmp(p, path.c_str()) == 0)
			{
				m_pSoundList->SetSingleSelectedItem(itemID);
				break;
			}
		}
	}
}


void CSoundpadFrame::PlaySelected(bool enqueue)
{
	SoundItem* s = GetSelectedSound();
	if (!s)
		return;

	if (!SoundFileExists(s->path))
	{
		char msg[256] = {};
		StringCchPrintfA(msg, sizeof(msg), "[Soundpad] Error: Sound file '%s' not found on disk!\n", s->path.c_str());
		if (gEngfuncs.Con_Printf)
			gEngfuncs.Con_Printf(msg);
		m_pStatusLabel->SetText("ERROR: Sound file MISSING!");
		return;
	}

	float vol = static_cast<float>(m_pVolSlider->GetValue()) / 100.0f;
	float pitch = static_cast<float>(m_pPitchSlider->GetValue()) / 100.0f;

	SoundEngine_Play(s->path.c_str(), vol, pitch, enqueue);
}

void CSoundpadFrame::PlayLocalPreview()
{
	SoundItem* s = GetSelectedSound();
	if (!s)
		return;

	if (!SoundFileExists(s->path))
	{
		char msg[256] = {};
		StringCchPrintfA(msg, sizeof(msg), "[Soundpad] Error: Sound file '%s' not found for preview!\n", s->path.c_str());
		if (gEngfuncs.Con_Printf)
			gEngfuncs.Con_Printf(msg);
		m_pStatusLabel->SetText("ERROR: Preview failed - file MISSING!");
		return;
	}

	float vol = static_cast<float>(m_pVolSlider->GetValue()) / 100.0f;
	float pitch = static_cast<float>(m_pPitchSlider->GetValue()) / 100.0f;

	SoundEngine_PreviewLocal(s->path.c_str(), vol, pitch);

	char stBuf[160] = {};
	StringCchPrintfA(stBuf, sizeof(stBuf), "PREVIEW (Local): %s (no broadcast)", s->path.c_str());
	m_pStatusLabel->SetText(stBuf);
}

void CSoundpadFrame::StopPlayback()
{
	SoundEngine_Stop();
	m_pStatusLabel->SetText("Status: Ready (Idle)");
}

void CSoundpadFrame::PlayRandom()
{
	SoundItem s;
	if (CSoundpadConfig::Get().GetRandomSound(m_selectedCategory, s))
	{
		float vol = static_cast<float>(m_pVolSlider->GetValue()) / 100.0f;
		float pitch = static_cast<float>(m_pPitchSlider->GetValue()) / 100.0f;
		SoundEngine_Play(s.path.c_str(), vol, pitch, false);
	}
}

void CSoundpadFrame::OnCommand(const char* command)
{
	if (_stricmp(command, "play_cut") == 0)
	{
		PlaySelected(false);
	}
	else if (_stricmp(command, "play_queue") == 0)
	{
		PlaySelected(true);
	}
	else if (_stricmp(command, "preview") == 0)
	{
		PlayLocalPreview();
	}
	else if (_stricmp(command, "stop") == 0)
	{
		StopPlayback();
	}
	else if (_stricmp(command, "random") == 0)
	{
		PlayRandom();
	}
	else if (_stricmp(command, "add_file") == 0)
	{
		OnAddFile();
	}
	else if (_stricmp(command, "add_folder") == 0)
	{
		OnAddFolder();
	}
	else if (_stricmp(command, "delete_sound") == 0)
	{
		OnDeleteSound();
	}
	else if (_stricmp(command, "add_category") == 0)
	{
		OnAddCategory();
	}
	else if (_stricmp(command, "save_config") == 0)
	{
		CSoundpadConfig::Get().Save(true);
		m_pStatusLabel->SetText("Status: Library saved to soundpad.vdf.");
	}
	else if (_stricmp(command, "set_hotkey") == 0)
	{
		m_dialogTargets = CollectSelectedSoundPaths();
		if (!m_dialogTargets.empty())
		{
			SoundItem* first = CSoundpadConfig::Get().FindSoundByPath(m_dialogTargets.front());
			char prompt[160] = {};
			StringCchPrintfA(prompt, sizeof(prompt),
				"Enter key name (e.g. F5, KP_PLUS, MOUSE3) for %d selected sound(s):",
				static_cast<int>(m_dialogTargets.size()));
			m_dialogAction = DialogAction::SetHotkey;
			SafeCloseDialog(m_hInputDialog);
			m_hInputDialog = new vgui::InputDialog(this, "Set Hotkey", prompt, first ? first->hotkey.c_str() : "");
			m_hInputDialog->AddActionSignalTarget(this);
			m_hInputDialog->DoModal();
		}
	}
	else if (_stricmp(command, "set_chat_trigger") == 0)
	{
		m_dialogTargets = CollectSelectedSoundPaths();
		if (!m_dialogTargets.empty())
		{
			SoundItem* first = CSoundpadConfig::Get().FindSoundByPath(m_dialogTargets.front());
			char prompt[192] = {};
			StringCchPrintfA(prompt, sizeof(prompt),
				"Enter trigger word (applied to %d selected sound(s); one of them is picked at random):",
				static_cast<int>(m_dialogTargets.size()));
			m_dialogAction = DialogAction::SetChatTrigger;
			SafeCloseDialog(m_hInputDialog);
			m_hInputDialog = new vgui::InputDialog(this, "Set Chat Trigger", prompt, first ? first->chatTrigger.c_str() : "");
			m_hInputDialog->AddActionSignalTarget(this);
			m_hInputDialog->DoModal();
		}
	}
	else if (_stricmp(command, "use_filename_trigger") == 0)
	{
		std::vector<std::string> targets = CollectSelectedSoundPaths();
		int changed = 0;
		for (const auto& path : targets)
		{
			std::string base = path;
			size_t lastSlash = base.find_last_of("/\\");
			if (lastSlash != std::string::npos)
				base = base.substr(lastSlash + 1);
			size_t dot = base.rfind('.');
			if (dot != std::string::npos)
				base = base.substr(0, dot);
			if (base.empty())
				continue;
			if (CSoundpadConfig::Get().SetSoundChatTrigger(path, base))
				++changed;
		}
		if (changed > 0)
		{
			CSoundpadConfig::Get().Save(false);
			RefreshSoundList();
		}
	}
	else if (_stricmp(command, "clear_chat_trigger") == 0)
	{
		std::vector<std::string> targets = CollectSelectedSoundPaths();
		int changed = 0;
		for (const auto& path : targets)
		{
			if (CSoundpadConfig::Get().SetSoundChatTrigger(path, ""))
				++changed;
		}
		if (changed > 0)
		{
			CSoundpadConfig::Get().Save(false);
			RefreshSoundList();
		}
	}
	else if (_stricmp(command, "move_to_category") == 0)
	{
		OnMoveToCategory();
	}
	else if (_stricmp(command, "add_reaction") == 0)
	{
		SafeCloseDialog(m_hReactionDialog);
		m_hReactionDialog = new CReactionEditDialog(this, this, -1);
		m_hReactionDialog->DoModal();
	}
	else if (_stricmp(command, "edit_reaction") == 0)
	{
		int selItem = m_pReactionList->GetSelectedItem(0);
		if (selItem >= 0)
		{
			KeyValues* kv = m_pReactionList->GetItem(selItem);
			int selIdx = kv ? (atoi(kv->GetString("num", "1")) - 1) : static_cast<int>(m_pReactionList->GetItemUserData(selItem));
			SafeCloseDialog(m_hReactionDialog);
			m_hReactionDialog = new CReactionEditDialog(this, this, selIdx);
			m_hReactionDialog->DoModal();
		}
	}
	else if (_stricmp(command, "delete_reaction") == 0)
	{
		int selItem = m_pReactionList->GetSelectedItem(0);
		if (selItem >= 0)
		{
			KeyValues* kv = m_pReactionList->GetItem(selItem);
			int selIdx = kv ? (atoi(kv->GetString("num", "1")) - 1) : static_cast<int>(m_pReactionList->GetItemUserData(selItem));
			CSoundpadConfig::Get().RemoveReaction(selIdx);
			RefreshReactionsList();
		}
	}
	else if (_stricmp(command, "test_reaction") == 0)
	{
		int selItem = m_pReactionList->GetSelectedItem(0);
		if (selItem >= 0)
		{
			KeyValues* kv = m_pReactionList->GetItem(selItem);
			int selIdx = kv ? (atoi(kv->GetString("num", "1")) - 1) : static_cast<int>(m_pReactionList->GetItemUserData(selItem));
			const auto& reactions = CSoundpadConfig::Get().GetReactions();
			if (selIdx >= 0 && selIdx < static_cast<int>(reactions.size()))
			{
				const auto& r = reactions[selIdx];
				if (r.soundPath.rfind("tts:", 0) == 0)
				{
					std::string phrase = r.soundPath.substr(4);
					std::string formatted = CGameState::Get().FormatTokens(phrase);
					float vol = (r.volume > 0.0f) ? r.volume : 0.35f;
					float pitch = (r.pitch > 0.0f) ? r.pitch : 1.0f;
					bool randStyle = CSoundpadConfig::Get().GetTtsRandomStyle();
					SoundEngine_SpeakTTS(formatted.c_str(), vol, pitch, true, false, true, true, randStyle);
				}
				else if (r.soundPath.rfind("macro:", 0) == 0 || r.soundPath.rfind("seq:", 0) == 0)
				{
					std::string sName = (r.soundPath.rfind("macro:", 0) == 0) ? r.soundPath.substr(6) : r.soundPath.substr(4);
					const auto* seq = CSoundpadConfig::Get().FindSequence(sName);
					if (seq) SoundEngine_PlaySequence(*seq);
				}
				else
				{
					SoundPlayParams p;
					p.volume = r.volume;
					p.pitch = r.pitch;
					p.fadeInSec = r.fadeInSec;
					p.fadeOutSec = r.fadeOutSec;
					p.fadeCurve = static_cast<FadeCurveType>(r.fadeCurve);
					p.fxReverb = r.fxReverb;
					p.fxZigzag = r.fxZigzag;
					p.localPreviewOnly = true;
					SoundEngine_PlayEx(r.soundPath.c_str(), p);
				}
				if (gEngfuncs.Con_Printf)
					gEngfuncs.Con_Printf("[Soundpad] Testing reaction #%d: '%s' (filter: '%s')\n",
						selIdx + 1, r.soundPath.c_str(), r.condition.c_str());
			}
		}
	}
	else if (_stricmp(command, "tts_speak") == 0 || _stricmp(command, "tts_preview") == 0)
	{
		char txt[512] = {};
		m_pTtsTextEntry->GetText(txt, sizeof(txt));
		if (txt[0])
		{
			float vol = static_cast<float>(m_pTtsVolSlider->GetValue()) / 100.0f;
			float pitch = static_cast<float>(m_pTtsPitchSlider->GetValue()) / 100.0f;
			bool jitter = m_pCheckTtsRepoJitter->IsSelected();
			bool whisper = m_pCheckTtsWhisper->IsSelected();
			bool localOnly = (_stricmp(command, "tts_preview") == 0);
			bool randStyle = m_pCheckTtsRandomStyle ? m_pCheckTtsRandomStyle->IsSelected() : false;

			std::string formatted = CGameState::Get().FormatTokens(txt);
			SoundEngine_SpeakTTS(formatted.c_str(), vol, pitch, jitter, whisper, localOnly, true, randStyle, true);
			if (m_pStatusLabel)
			{
				char status[128] = {};
				snprintf(status, sizeof(status), "TTS: %s (vol=%.2f, pitch=%.2f)", localOnly ? "Preview" : "Broadcasting", vol, pitch);
				m_pStatusLabel->SetText(status);
			}
		}
	}
	else if (_stricmp(command, "save_reactions") == 0)
	{
		CSoundpadConfig::Get().Save(true);
	}
	else if (_stricmp(command, "add_sequence") == 0)
	{
		m_dialogAction = DialogAction::AddSeqName;
		SafeCloseDialog(m_hInputDialog);
		m_hInputDialog = new vgui::InputDialog(this, "New Macro Sequence", "Enter sequence macro name:");
		m_hInputDialog->AddActionSignalTarget(this);
		m_hInputDialog->DoModal();
	}
	else if (_stricmp(command, "add_seq_to_lib") == 0)
	{
		int selItem = m_pSeqList->GetSelectedItem(0);
		if (selItem >= 0)
		{
			KeyValues* kv = m_pSeqList->GetItem(selItem);
			int seqIdx = kv ? (atoi(kv->GetString("num", "1")) - 1) : static_cast<int>(m_pSeqList->GetItemUserData(selItem));
			const auto& seqs = CSoundpadConfig::Get().GetSequences();
			if (seqIdx >= 0 && seqIdx < static_cast<int>(seqs.size()))
			{
				int insertIdx = -1;
				int selSound = m_pSoundList->GetSelectedItem(0);
				if (selSound >= 0)
				{
					KeyValues* sKv = m_pSoundList->GetItem(selSound);
					if (sKv) insertIdx = atoi(sKv->GetString("num", "0"));
				}
				AddSequenceAsSound(seqs[seqIdx].name, m_selectedCategory, insertIdx);
				if (gEngfuncs.Con_Printf)
					gEngfuncs.Con_Printf("[Soundpad] Added macro '%s' to sound list category '%s'\n",
						seqs[seqIdx].name.c_str(), m_selectedCategory.c_str());
			}
		}
	}
	else if (_stricmp(command, "delete_sequence") == 0)
	{
		int selItem = m_pSeqList->GetSelectedItem(0);
		if (selItem >= 0)
		{
			KeyValues* kv = m_pSeqList->GetItem(selItem);
			int selIdx = kv ? (atoi(kv->GetString("num", "1")) - 1) : static_cast<int>(m_pSeqList->GetItemUserData(selItem));
			CSoundpadConfig::Get().RemoveSequence(selIdx);
			RefreshSequencerList();
		}
	}
	else if (_stricmp(command, "play_sequence") == 0)
	{
		int selItem = m_pSeqList->GetSelectedItem(0);
		if (selItem >= 0)
		{
			KeyValues* kv = m_pSeqList->GetItem(selItem);
			int selIdx = kv ? (atoi(kv->GetString("num", "1")) - 1) : static_cast<int>(m_pSeqList->GetItemUserData(selItem));
			const auto& seqs = CSoundpadConfig::Get().GetSequences();
			if (selIdx >= 0 && selIdx < static_cast<int>(seqs.size()))
			{
				SoundEngine_PlaySequence(seqs[selIdx]);
			}
		}
	}
	else if (_stricmp(command, "add_seq_step") == 0)
	{
		int selItem = m_pSeqList->GetSelectedItem(0);
		if (selItem >= 0)
		{
			KeyValues* kv = m_pSeqList->GetItem(selItem);
			int seqIdx = kv ? (atoi(kv->GetString("num", "1")) - 1) : static_cast<int>(m_pSeqList->GetItemUserData(selItem));
			SafeCloseDialog(m_hStepDialog);
			m_hStepDialog = new CSequenceStepDialog(this, this, seqIdx, -1);
			m_hStepDialog->DoModal();
		}
	}
	else if (_stricmp(command, "edit_seq_step") == 0)
	{
		int selItem = m_pSeqList->GetSelectedItem(0);
		int selStep = m_pSeqStepsList->GetSelectedItem(0);
		if (selItem >= 0 && selStep >= 0)
		{
			KeyValues* kv1 = m_pSeqList->GetItem(selItem);
			KeyValues* kv2 = m_pSeqStepsList->GetItem(selStep);
			int seqIdx = kv1 ? (atoi(kv1->GetString("num", "1")) - 1) : static_cast<int>(m_pSeqList->GetItemUserData(selItem));
			int stepIdx = kv2 ? (atoi(kv2->GetString("step", "1")) - 1) : static_cast<int>(m_pSeqStepsList->GetItemUserData(selStep));
			SafeCloseDialog(m_hStepDialog);
			m_hStepDialog = new CSequenceStepDialog(this, this, seqIdx, stepIdx);
			m_hStepDialog->DoModal();
		}
	}
	else if (_stricmp(command, "delete_seq_step") == 0)
	{
		int selItem = m_pSeqList->GetSelectedItem(0);
		int selStep = m_pSeqStepsList->GetSelectedItem(0);
		if (selItem >= 0 && selStep >= 0)
		{
			KeyValues* kv1 = m_pSeqList->GetItem(selItem);
			KeyValues* kv2 = m_pSeqStepsList->GetItem(selStep);
			int seqIdx = kv1 ? (atoi(kv1->GetString("num", "1")) - 1) : static_cast<int>(m_pSeqList->GetItemUserData(selItem));
			int stepIdx = kv2 ? (atoi(kv2->GetString("step", "1")) - 1) : static_cast<int>(m_pSeqStepsList->GetItemUserData(selStep));
			auto& seqs = CSoundpadConfig::Get().GetSequences();
			if (seqIdx >= 0 && seqIdx < static_cast<int>(seqs.size()) && stepIdx >= 0 && stepIdx < static_cast<int>(seqs[seqIdx].steps.size()))
			{
				seqs[seqIdx].steps.erase(seqs[seqIdx].steps.begin() + stepIdx);
				CSoundpadConfig::Get().Save(false);
				RefreshSequencerList();
			}
		}
	}
	else if (_stricmp(command, "set_slice_in") == 0)
	{
		float val = static_cast<float>(m_pTimelineSlider->GetValue()) / 1000.0f;
		m_sliceStart = val * m_currentSoundDuration;
		char buf[128] = {};
		StringCchPrintfA(buf, sizeof(buf), "Slice Slicer: %.2fs .. %.2fs (Total: %.2fs)", m_sliceStart, m_sliceEnd, m_currentSoundDuration);
		m_pTimelineLabel->SetText(buf);
	}
	else if (_stricmp(command, "set_slice_out") == 0)
	{
		float val = static_cast<float>(m_pTimelineSlider->GetValue()) / 1000.0f;
		m_sliceEnd = val * m_currentSoundDuration;
		if (m_sliceEnd <= m_sliceStart) m_sliceEnd = m_currentSoundDuration;
		char buf[128] = {};
		StringCchPrintfA(buf, sizeof(buf), "Slice Slicer: %.2fs .. %.2fs (Total: %.2fs)", m_sliceStart, m_sliceEnd, m_currentSoundDuration);
		m_pTimelineLabel->SetText(buf);
	}
	else if (_stricmp(command, "preview_slice") == 0)
	{
		SoundItem* s = GetSelectedSound();
		if (s)
		{
			SoundPlayParams p;
			p.volume = static_cast<float>(m_pVolSlider->GetValue()) / 100.0f;
			p.pitch = static_cast<float>(m_pPitchSlider->GetValue()) / 100.0f;
			p.startSec = m_sliceStart;
			p.endSec = m_sliceEnd;
			SoundEngine_PlayEx(s->path.c_str(), p);
		}
	}
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	else if (_stricmp(command, "ban_player") == 0)
	{
		int selItem = m_pPlayerList ? m_pPlayerList->GetSelectedItem(0) : -1;
		if (selItem >= 0)
		{
			KeyValues* kv = m_pPlayerList->GetItem(selItem);
			if (kv)
			{
				const char* idStr = kv->GetString("steamid", "0");
				const char* name = kv->GetString("name", "Player");
				const char* status = kv->GetString("status", "");
				uint64_t steamID = std::strtoull(idStr, nullptr, 10);
				if (steamID != 0 || (name && name[0]))
				{
					if (strstr(status, "You") != nullptr)
					{
						if (gEngfuncs.Con_Printf)
							gEngfuncs.Con_Printf("[Soundpad] Cannot ban yourself!\n");
						return;
					}

					bool isBanned = CSoundpadConfig::Get().IsPlayerBanned(steamID, name);
					if (isBanned)
					{
						if (steamID != 0)
							CSoundpadConfig::Get().UnbanPlayer(steamID);
						else
							CSoundpadConfig::Get().UnbanPlayerByName(name);

						if (gEngfuncs.Con_Printf)
						{
							if (steamID != 0)
								gEngfuncs.Con_Printf("[Soundpad] Unbanned player %s (SteamID: %llu)\n", name, static_cast<unsigned long long>(steamID));
							else
								gEngfuncs.Con_Printf("[Soundpad] Unbanned player '%s' (by nickname)\n", name);
						}
					}
					else
					{
						CSoundpadConfig::Get().BanPlayer(steamID, name, "Banned via GUI");
						if (gEngfuncs.Con_Printf)
						{
							if (steamID != 0)
								gEngfuncs.Con_Printf("[Soundpad] Banned player %s (SteamID: %llu)\n", name, static_cast<unsigned long long>(steamID));
							else
								gEngfuncs.Con_Printf("[Soundpad] Banned player '%s' by nickname (SteamID is 0 on this server)\n", name);
						}
					}
					RefreshPlayersList();
				}
			}
		}
	}
	else if (_stricmp(command, "add_ban_manual") == 0)
	{
		SafeCloseDialog(m_hManualBanDialog);
		m_hManualBanDialog = new CManualBanDialog(this, this);
		m_hManualBanDialog->DoModal();
	}
	else if (_stricmp(command, "refresh_players") == 0)
	{
		RefreshPlayersList();
	}
#endif

	else
	{
		BaseClass::OnCommand(command);
	}
}

static char s_lastBrowseDir[MAX_PATH] = {};

void CSoundpadFrame::OnAddFile()
{
	m_dialogAction = DialogAction::AddFile;
	SafeCloseDialog(m_hFileOpenDialog);

	if (!s_lastBrowseDir[0])
	{
		const char* gamedir = gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : "svencoop";
		StringCchPrintfA(s_lastBrowseDir, sizeof(s_lastBrowseDir), "%s/sound", gamedir);
	}

	m_hFileOpenDialog = new vgui::FileOpenDialog(this, "Select Sound File (*.wav, *.mp3, *.ogg)", vgui::FOD_OPEN);
	m_hFileOpenDialog->AddFilter("*.wav;*.mp3;*.ogg", "Audio Files (*.wav, *.mp3, *.ogg)", true);
	m_hFileOpenDialog->AddFilter("*.wav", "WAV Audio (*.wav)", false);
	m_hFileOpenDialog->AddFilter("*.mp3", "MP3 Audio (*.mp3)", false);
	m_hFileOpenDialog->AddFilter("*.ogg", "OGG Vorbis (*.ogg)", false);
	m_hFileOpenDialog->SetStartDirectory(s_lastBrowseDir);
	m_hFileOpenDialog->AddActionSignalTarget(this);
	m_hFileOpenDialog->DoModal();
}

void CSoundpadFrame::OnAddFolder()
{
	m_dialogAction = DialogAction::AddFolder;
	SafeCloseDialog(m_hFileOpenDialog);

	char startDir[MAX_PATH] = {};
	const char* gamedir = gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : "svencoop";
	StringCchPrintfA(startDir, sizeof(startDir), "%s/sound", gamedir);

	m_hFileOpenDialog = new vgui::FileOpenDialog(this, "Select Folder of Sounds", vgui::FOD_SELECT_DIRECTORY);
	m_hFileOpenDialog->SetStartDirectory(startDir);
	m_hFileOpenDialog->AddActionSignalTarget(this);
	m_hFileOpenDialog->DoModal();
}

std::vector<std::string> CSoundpadFrame::CollectSelectedSoundPaths()
{
	std::vector<std::string> paths;
	if (!m_pSoundList)
		return paths;

	int count = m_pSoundList->GetSelectedItemsCount();
	if (count <= 0)
		count = (m_pSoundList->GetSelectedItem(0) >= 0) ? 1 : 0;

	paths.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		int itemID = m_pSoundList->GetSelectedItem(i);
		if (itemID < 0)
			continue;
		KeyValues* kv = m_pSoundList->GetItem(itemID);
		if (!kv)
			continue;
		const char* path = kv->GetString("path", "");
		if (path && *path)
			paths.push_back(path);
	}
	return paths;
}

void CSoundpadFrame::OnDeleteSound()
{
	int count = m_pSoundList->GetSelectedItemsCount();
	if (count <= 0)
	{
		int itemID = m_pSoundList->GetSelectedItem(0);
		if (itemID < 0)
			return;
		count = 1;
	}

	std::vector<std::string> pathsToDelete;
	for (int i = 0; i < count; ++i)
	{
		int itemID = m_pSoundList->GetSelectedItem(i);
		if (itemID >= 0)
		{
			KeyValues* kv = m_pSoundList->GetItem(itemID);
			if (kv)
			{
				const char* p = kv->GetString("path", "");
				if (p && *p)
					pathsToDelete.push_back(p);
			}
		}
	}

	if (pathsToDelete.empty())
	{
		SoundItem* s = GetSelectedSound();
		if (s && !s->path.empty())
			pathsToDelete.push_back(s->path);
	}

	for (const auto& path : pathsToDelete)
	{
		CSoundpadConfig::Get().RemoveSoundFromCategory(m_selectedCategory, path);
	}

	InvalidateSoundFileCache();
	RefreshSoundList();
}

void CSoundpadFrame::OnAddCategory()
{
	m_dialogAction = DialogAction::AddCategory;
	SafeCloseDialog(m_hInputDialog);

	m_hInputDialog = new vgui::InputDialog(this, "New Category", "Enter category name:");
	m_hInputDialog->AddActionSignalTarget(this);
	m_hInputDialog->DoModal();
}

void CSoundpadFrame::OnMoveToCategory()
{
	if (!m_pSoundList || m_pSoundList->GetSelectedItemsCount() <= 0)
		return;

	// Moving out of the aggregated "All Sounds" view is ambiguous: the sound may
	// live in any category. Require a concrete category so the source is known.
	if (m_selectedCategory.empty() || _stricmp(m_selectedCategory.c_str(), "All") == 0)
	{
		m_pStatusLabel->SetText("Status: select a concrete category (not All Sounds) before moving.");
		return;
	}

	m_dialogAction = DialogAction::MoveToCategory;
	SafeCloseDialog(m_hInputDialog);

	std::string prompt = "Move selected sound(s) to category:";
	const auto& cats = CSoundpadConfig::Get().GetRootCategories();
	if (!cats.empty())
	{
		prompt += " ";
		for (size_t i = 0; i < cats.size() && i < 8; ++i)
		{
			if (i)
				prompt += ", ";
			prompt += cats[i].name;
		}
		if (cats.size() > 8)
			prompt += ", ...";
	}

	m_hInputDialog = new vgui::InputDialog(this, "Move to Category", prompt.c_str());
	m_hInputDialog->AddActionSignalTarget(this);
	m_hInputDialog->DoModal();
}

void CSoundpadFrame::MoveSelectedSoundsToCategory(const std::string& targetCategory)
{
	if (!m_pSoundList || targetCategory.empty())
		return;

	std::string target = targetCategory;
	size_t first = target.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return;
	size_t last = target.find_last_not_of(" \t\r\n");
	target = target.substr(first, last - first + 1);

	int count = m_pSoundList->GetSelectedItemsCount();
	if (count <= 0)
		return;

	std::vector<std::string> paths;
	paths.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		int itemID = m_pSoundList->GetSelectedItem(i);
		if (itemID < 0)
			continue;
		KeyValues* kv = m_pSoundList->GetItem(itemID);
		if (!kv)
			continue;
		const char* path = kv->GetString("path", "");
		if (path && *path)
			paths.push_back(path);
	}

	const size_t moved = CSoundpadConfig::Get().MoveSoundsToCategory(m_selectedCategory, target, paths);

	if (moved > 0)
	{
		InvalidateSoundFileCache();
		const std::string sourceCategory = m_selectedCategory;
		RefreshCategories();
		// RefreshCategories rebuilds the tree; keep the user on the source category
		// instead of letting a rebuild selection event jump back to "All Sounds".
		m_selectedCategory = sourceCategory;
		RefreshSoundList();

		char status[192] = {};
		StringCchPrintfA(status, sizeof(status), "Status: moved %d sound(s) to '%s'.", static_cast<int>(moved), target.c_str());
		m_pStatusLabel->SetText(status);
	}
	else
	{
		m_pStatusLabel->SetText("Status: nothing moved (same category or sound not found).");
	}
}

void CSoundpadFrame::OnFileSelected(KeyValues* kv)
{
	if (!kv)
		return;

	const char* fullpath = kv->GetString("fullpath", "");
	if (!fullpath || !*fullpath)
		return;

	// Remember directory for next file browse
	std::string fpStr = fullpath;
	size_t slashPos = fpStr.find_last_of("/\\");
	if (slashPos != std::string::npos)
	{
		std::string dir = fpStr.substr(0, slashPos);
		StringCchCopyA(s_lastBrowseDir, sizeof(s_lastBrowseDir), dir.c_str());
	}

	if (m_dialogAction == DialogAction::AddFile)
	{
		int insertIdx = -1;
		int selItem = m_pSoundList->GetSelectedItem(0);
		if (selItem >= 0)
		{
			KeyValues* itemKv = m_pSoundList->GetItem(selItem);
			if (itemKv)
			{
				int num = atoi(itemKv->GetString("num", "0"));
				if (num > 0)
					insertIdx = num; // insert directly after clicked item!
			}
		}

		if (AddSoundFile(fullpath, m_selectedCategory, insertIdx))
			RefreshSoundList();
	}
	else if (m_dialogAction == DialogAction::AddFolder)
	{
		AddSoundFolder(fullpath, m_selectedCategory);
	}
	m_dialogAction = DialogAction::None;
}

void CSoundpadFrame::OnInputCompleted(KeyValues* kv)
{
	if (!kv)
		return;

	const char* text = kv->GetString("text", "");
	if (text)
	{
		if (m_dialogAction == DialogAction::AddCategory)
		{
			if (*text) { CSoundpadConfig::Get().AddCategory(text); RefreshCategories(); }
		}
		else if (m_dialogAction == DialogAction::AddSeqName)
		{
			if (*text) { SequenceItem seq; seq.name = text; CSoundpadConfig::Get().AddSequence(seq); RefreshSequencerList(); }
		}
		else if (m_dialogAction == DialogAction::SetChatTrigger)
		{
			int changed = 0;
			for (const auto& path : m_dialogTargets)
			{
				if (CSoundpadConfig::Get().SetSoundChatTrigger(path, text))
					++changed;
			}
			if (changed == 0)
			{
				SoundItem* s = GetSelectedSound(); // fallback: single-row edit outside the list
				if (s)
				{
					s->chatTrigger = text;
					changed = 1;
				}
			}
			if (changed > 0)
			{
				CSoundpadConfig::Get().Save(false);
				RefreshSoundList();
			}
		}
		else if (m_dialogAction == DialogAction::SetHotkey)
		{
			int changed = 0;
			for (const auto& path : m_dialogTargets)
			{
				if (CSoundpadConfig::Get().SetSoundHotkey(path, text))
					++changed;
			}
			if (changed == 0)
			{
				SoundItem* s = GetSelectedSound();
				if (s)
				{
					s->hotkey = text;
					changed = 1;
				}
			}
			if (changed > 0)
			{
				CSoundpadConfig::Get().Save(false);
				RefreshSoundList();
			}
		}
		else if (m_dialogAction == DialogAction::MoveToCategory)
		{
			if (*text)
				MoveSelectedSoundsToCategory(text);
		}
	}
	m_dialogAction = DialogAction::None;
	m_dialogTargets.clear();
}

void CSoundpadFrame::OnInputCanceled()
{
	// Cancelled dialogs must not leave a pending action or target list behind.
	m_dialogAction = DialogAction::None;
	m_dialogTargets.clear();
}
void CSoundpadFrame::OnOpenContextMenu(int itemID)
{
	vgui::Panel* activePage = m_pTabSheet ? m_pTabSheet->GetActivePage() : nullptr;
	if (activePage == m_pTabReactions)
	{
		if (itemID >= 0 && m_pReactionList)
		{
			m_pReactionList->ClearSelectedItems();
			m_pReactionList->AddSelectedItem(itemID);
		}
		if (m_pReactionContextMenu)
		{
			vgui::Menu::PlaceContextMenu(this, m_pReactionContextMenu);
		}
		return;
	}

	if (activePage == m_pTabSoundpad)
	{
		if (itemID >= 0 && m_pSoundList)
		{
			// Right-clicking an already selected row keeps the whole multi-selection,
			// so "Move to Category..." and "Delete" act on every selected sound.
			if (!m_pSoundList->IsItemSelected(itemID))
			{
				m_pSoundList->ClearSelectedItems();
				m_pSoundList->AddSelectedItem(itemID);
			}
		}
		if (m_pContextMenu)
		{
			vgui::Menu::PlaceContextMenu(this, m_pContextMenu);
		}
		return;
	}
}

bool CSoundpadFrame::AddSequenceAsSound(const std::string& seqName, const std::string& targetCategory, int insertIndex)
{
	if (seqName.empty())
		return false;

	std::string cat = (targetCategory.empty() || _stricmp(targetCategory.c_str(), "All") == 0) ? "Custom" : targetCategory;
	SoundItem item;
	item.title = "[Macro] " + seqName;
	item.path = "macro:" + seqName;
	item.volume = 1.0f;
	item.pitch = 1.0f;
	item.isSequence = true;

	CSoundpadConfig::Get().AddSoundToCategory(cat, item, insertIndex);
	InvalidateSoundFileCache();
	RefreshSoundList();
	return true;
}

static bool IsSupportedSoundFormat(const std::string& path)
{
	std::string ext = std::filesystem::path(path).extension().string();
	return (_stricmp(ext.c_str(), ".wav") == 0 ||
	        _stricmp(ext.c_str(), ".mp3") == 0 ||
	        _stricmp(ext.c_str(), ".ogg") == 0);
}

bool CSoundpadFrame::AddSoundFile(const std::string& rawPath, const std::string& targetCategory, int insertIndex)
{
	if (rawPath.empty())
		return false;

	std::string norm = rawPath;
	for (char& c : norm)
	{
		if (c == '\\')
			c = '/';
	}

	if (!IsSupportedSoundFormat(norm))
	{
		if (gEngfuncs.Con_Printf)
			gEngfuncs.Con_Printf("[Soundpad] Error: Unsupported audio format ('%s'). Supported formats: .wav, .mp3, .ogg.\n", rawPath.c_str());
		return false;
	}

	std::string relPath;
	std::string normLower = norm;
	std::transform(normLower.begin(), normLower.end(), normLower.begin(), SafeToLower);

	size_t soundPos = normLower.rfind("/sound/");
	if (soundPos != std::string::npos)
	{
		relPath = norm.substr(soundPos + 7);
	}
	else if (normLower.rfind("sound/", 0) == 0)
	{
		relPath = norm.substr(6);
	}
	else
	{
		std::error_code ec;
		std::filesystem::path srcPath(norm);
		if (!std::filesystem::exists(srcPath, ec))
		{
			if (gEngfuncs.Con_Printf)
				gEngfuncs.Con_Printf("[Soundpad] Error: File does not exist: '%s'\n", rawPath.c_str());
			return false;
		}

		const char* gamedir = gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : "svencoop";
		std::string catDir = (targetCategory.empty() || _stricmp(targetCategory.c_str(), "All") == 0) ? "custom" : targetCategory;
		for (char& c : catDir)
		{
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
				c = '_';
		}

		char destDirStr[MAX_PATH] = {};
		StringCchPrintfA(destDirStr, sizeof(destDirStr), "%s/sound/soundpad/%s", gamedir, catDir.c_str());
		std::filesystem::create_directories(destDirStr, ec);

		std::string filename = srcPath.filename().string();
		char destFileStr[MAX_PATH] = {};
		StringCchPrintfA(destFileStr, sizeof(destFileStr), "%s/%s", destDirStr, filename.c_str());

		std::filesystem::copy_file(srcPath, destFileStr, std::filesystem::copy_options::overwrite_existing, ec);

		char relStr[MAX_PATH] = {};
		StringCchPrintfA(relStr, sizeof(relStr), "soundpad/%s/%s", catDir.c_str(), filename.c_str());
		relPath = relStr;
	}

	std::filesystem::path p(relPath);
	std::string title = p.stem().string();
	for (char& c : title)
	{
		if (c == '_') c = ' ';
	}

	std::string cat = (targetCategory.empty() || _stricmp(targetCategory.c_str(), "All") == 0) ? "Custom" : targetCategory;
	SoundItem item;
	item.title = title;
	item.path = relPath;
	item.volume = 1.0f;
	item.pitch = 1.0f;

	CSoundpadConfig::Get().AddSoundToCategory(cat, item, insertIndex);
	InvalidateSoundFileCache();

	char msg[300] = {};
	StringCchPrintfA(msg, sizeof(msg), "[Soundpad] Added sound '%s' -> Category '%s' (sound/%s)\n",
		title.c_str(), cat.c_str(), relPath.c_str());
	if (gEngfuncs.Con_Printf)
		gEngfuncs.Con_Printf(msg);

	return true;
}

int CSoundpadFrame::AddSoundFolder(const std::string& rawPath, const std::string& targetCategory)
{
	if (rawPath.empty())
		return 0;

	std::string norm = rawPath;
	for (char& c : norm)
	{
		if (c == '\\')
			c = '/';
	}
	while (!norm.empty() && norm.back() == '/')
		norm.pop_back();

	std::error_code ec;
	std::filesystem::path dirPath(norm);
	if (!std::filesystem::is_directory(dirPath, ec))
	{
		if (gEngfuncs.Con_Printf)
			gEngfuncs.Con_Printf("[Soundpad] Error: '%s' is not a valid directory.\n", rawPath.c_str());
		return 0;
	}

	std::string cat = targetCategory;
	if (cat.empty() || _stricmp(cat.c_str(), "All") == 0)
	{
		cat = dirPath.filename().string();
		if (cat.empty())
			cat = "Imported";
	}

	int count = 0;
	for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec))
	{
		if (entry.is_regular_file(ec))
		{
			if (IsSupportedSoundFormat(entry.path().string()))
			{
				if (AddSoundFile(entry.path().string(), cat))
					++count;
			}
		}
	}

	char msg[256] = {};
	StringCchPrintfA(msg, sizeof(msg), "[Soundpad] Imported %d sound(s) into category '%s'.\n", count, cat.c_str());
	if (gEngfuncs.Con_Printf)
		gEngfuncs.Con_Printf(msg);

	if (g_hSoundpadFrame.Get())
	{
		g_hSoundpadFrame->RefreshCategories();
		g_hSoundpadFrame->RefreshSoundList();
	}
	return count;
}

void CSoundpadFrame::OnTextChanged(vgui::Panel* panel)
{
	if (panel == m_pSearchFilter)
	{
		RefreshSoundList();
	}
	else if (panel == m_pComboTtsEngine)
	{
		KeyValues* kv = m_pComboTtsEngine->GetActiveItemUserData();
		const char* eng = kv ? kv->GetString("engine", "klatter") : "klatter";
		if (sp_tts_engine && gEngfuncs.Cvar_Set)
		{
			gEngfuncs.Cvar_Set("sp_tts_engine", eng);
		}
		CSoundpadConfig::Get().SetTtsEngine(eng);
		CSoundpadConfig::Get().Save();
	}
	else if (panel == m_pComboSapiVoice)
	{
		KeyValues* kv = m_pComboSapiVoice->GetActiveItemUserData();
		const char* voice = kv ? kv->GetString("voice", "") : "";
		if (sp_sapi_voice && gEngfuncs.Cvar_Set)
		{
			gEngfuncs.Cvar_Set("sp_sapi_voice", voice);
		}
		CSoundpadConfig::Get().SetSapiVoice(voice);
		CSoundpadConfig::Get().Save();
	}
}

void CSoundpadFrame::OnTreeViewItemSelected(int itemIndex)
{
	KeyValues* kv = m_pCatTree->GetItemData(itemIndex);
	if (!kv)
		return;

	const char* catName = kv->GetString("cat", "All");
	if (m_selectedCategory != catName)
	{
		m_selectedCategory = catName;
		RefreshSoundList();
	}
}

void CSoundpadFrame::OnItemSelected(vgui::Panel* panel)
{
	if (panel == m_pComboTtsEngine)
	{
		KeyValues* kv = m_pComboTtsEngine->GetActiveItemUserData();
		const char* eng = kv ? kv->GetString("engine", "klatter") : "klatter";
		if (sp_tts_engine && gEngfuncs.Cvar_Set)
		{
			gEngfuncs.Cvar_Set("sp_tts_engine", eng);
		}
		CSoundpadConfig::Get().SetTtsEngine(eng);
		CSoundpadConfig::Get().Save();
	}
	else if (panel == m_pComboSapiVoice)
	{
		KeyValues* kv = m_pComboSapiVoice->GetActiveItemUserData();
		const char* voice = kv ? kv->GetString("voice", "") : "";
		if (sp_sapi_voice && gEngfuncs.Cvar_Set)
		{
			gEngfuncs.Cvar_Set("sp_sapi_voice", voice);
		}
		CSoundpadConfig::Get().SetSapiVoice(voice);
		CSoundpadConfig::Get().Save();
	}
	else if (panel == m_pSoundList)
	{
		SoundItem* s = GetSelectedSound();
		if (s)
		{
			m_bUpdatingSliders = true;
			int volVal = static_cast<int>(s->volume * 100.0f + 0.5f);
			if (volVal < 1) volVal = 1;
			if (volVal > 100) volVal = 100;
			m_pVolSlider->SetValue(volVal, false);

			int pitchVal = static_cast<int>(s->pitch * 100.0f + 0.5f);
			if (pitchVal < 50) pitchVal = 50;
			if (pitchVal > 200) pitchVal = 200;
			m_pPitchSlider->SetValue(pitchVal, false);

			m_bUpdatingSliders = false;

			m_currentSoundDuration = SoundEngine_GetWaveDuration(s->path.c_str());
			m_sliceStart = 0.0f;
			m_sliceEnd = m_currentSoundDuration;
			char buf[128] = {};
			StringCchPrintfA(buf, sizeof(buf), "Slice Slicer: %.2fs .. %.2fs (Total: %.2fs)", m_sliceStart, m_sliceEnd, m_currentSoundDuration);
			m_pTimelineLabel->SetText(buf);
		}
	}
	else if (panel == m_pSeqList)
	{
		RefreshSequencerList();
	}
}

void CSoundpadFrame::OnSliderMoved(KeyValues* kv)
{
	if (m_bUpdatingSliders)
		return;

	vgui::Panel* pCaller = reinterpret_cast<vgui::Panel*>(kv ? kv->GetPtr("panel", nullptr) : nullptr);
	if (pCaller == m_pMasterVolSlider)
	{
		int val = m_pMasterVolSlider->GetValue();
		float mv = static_cast<float>(val) / 100.0f;
		SoundEngine_SetMasterVolume(mv);

		char buf[32] = {};
		StringCchPrintfA(buf, sizeof(buf), "Master: %d%%", val);
		m_pMasterVolLabel->SetText(buf);
		return;
	}
	if (pCaller == m_pTimelineSlider)
	{
		float val = static_cast<float>(m_pTimelineSlider->GetValue()) / 1000.0f;
		float curTime = val * m_currentSoundDuration;
		char buf[128] = {};
		StringCchPrintfA(buf, sizeof(buf), "Slice Slicer: %.2fs .. %.2fs (Pos: %.2fs, Total: %.2fs)",
			m_sliceStart, m_sliceEnd, curTime, m_currentSoundDuration);
		m_pTimelineLabel->SetText(buf);
		return;
	}
	if (pCaller == m_pTtsVolSlider)
	{
		float vol = static_cast<float>(m_pTtsVolSlider->GetValue()) / 100.0f;
		char buf[32] = {};
		StringCchPrintfA(buf, sizeof(buf), "Volume: %.2f", vol);
		m_pTtsVolLabel->SetText(buf);
		if (sp_tts_volume) sp_tts_volume->value = vol;
		return;
	}
	if (pCaller == m_pTtsPitchSlider)
	{
		float pitch = static_cast<float>(m_pTtsPitchSlider->GetValue()) / 100.0f;
		char buf[32] = {};
		StringCchPrintfA(buf, sizeof(buf), "Base Pitch: %.2f", pitch);
		m_pTtsPitchLabel->SetText(buf);
		if (sp_tts_pitch) sp_tts_pitch->value = pitch;
		return;
	}

	SoundItem* s = GetSelectedSound();
	if (!s)
		return;

	float vol = static_cast<float>(m_pVolSlider->GetValue()) / 100.0f;
	float pitch = static_cast<float>(m_pPitchSlider->GetValue()) / 100.0f;

	// Ignore spurious slider events if values haven't actually changed
	if (std::abs(s->volume - vol) < 0.005f && std::abs(s->pitch - pitch) < 0.005f)
		return;

	s->volume = vol;
	s->pitch = pitch;

	CSoundpadConfig::Get().UpdateSoundSettings(s->path, vol, pitch);

	int itemID = m_pSoundList->GetSelectedItem(0);
	if (itemID >= 0)
	{
		KeyValues* itemKv = m_pSoundList->GetItem(itemID);
		if (itemKv)
		{
			char volStr[16] = {}, pitchStr[16] = {};
			StringCchPrintfA(volStr, sizeof(volStr), "%.2f", vol);
			StringCchPrintfA(pitchStr, sizeof(pitchStr), "%.2f", pitch);
			itemKv->SetString("vol", volStr);
			itemKv->SetString("pitch", pitchStr);
			m_pSoundList->ApplyItemChanges(itemID);
		}
	}
}

void CSoundpadFrame::OnCheckButtonChecked(KeyValues* kv)
{
	if (m_bUpdatingSliders)
		return;

	vgui::Panel* pCaller = reinterpret_cast<vgui::Panel*>(kv ? kv->GetPtr("panel", nullptr) : nullptr);
	if (pCaller == m_pCheckAutoHide)
	{
		if (sp_autohide) sp_autohide->value = m_pCheckAutoHide->IsSelected() ? 1.0f : 0.0f;
		return;
	}
	if (pCaller == m_pCheckTtsAutoSay)
	{
		if (sp_tts_autosay) sp_tts_autosay->value = m_pCheckTtsAutoSay->IsSelected() ? 1.0f : 0.0f;
		return;
	}
	if (pCaller == m_pCheckTtsRepoJitter)
	{
		if (sp_tts_jitter) sp_tts_jitter->value = m_pCheckTtsRepoJitter->IsSelected() ? 1.0f : 0.0f;
		return;
	}
	if (pCaller == m_pCheckTtsWhisper)
	{
		if (sp_tts_whisper) sp_tts_whisper->value = m_pCheckTtsWhisper->IsSelected() ? 1.0f : 0.0f;
		return;
	}
	if (pCaller == m_pCheckMute)
	{
		bool muted = m_pCheckMute->IsSelected();
		SoundEngine_SetMuted(muted);
		return;
	}
	if (pCaller == m_pCheckTtsRandomStyle)
	{
		bool randStyle = m_pCheckTtsRandomStyle->IsSelected();
		CSoundpadConfig::Get().SetTtsRandomStyle(randStyle);
		return;
	}
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	if (pCaller == m_pCheckDotStop)
	{
		bool dotStop = m_pCheckDotStop->IsSelected();
		CSoundpadConfig::Get().SetDotStopEnabled(dotStop);
		return;
	}
#endif

}

void CSoundpadFrame::OnItemDoubleClicked(int itemID)
{
	if (m_pTabSoundpad && m_pTabSoundpad->IsVisible())
	{
		if (itemID >= 0 && m_pSoundList)
			m_pSoundList->SetSingleSelectedItem(itemID);

		if (m_pCheckDblClickPreview && m_pCheckDblClickPreview->IsSelected())
			PlayLocalPreview();
		else
			PlaySelected(false);
	}
	else if (m_pTabReactions && m_pTabReactions->IsVisible())
	{
		OnCommand("edit_reaction");
	}
	else if (m_pTabSequencer && m_pTabSequencer->IsVisible())
	{
		OnCommand("edit_seq_step");
	}
	else if (m_pTabBans && m_pTabBans->IsVisible())
	{
		OnCommand("ban_player");
	}
}

void CSoundpadFrame::OnKeyCodeTyped(vgui::KeyCode code)
{
	if (code == vgui::KEY_ESCAPE)
	{
		// If any child modal dialog or context menu is open, close it first and keep main frame active
		if (m_hFileOpenDialog.Get() || m_hInputDialog.Get() || m_hReactionDialog.Get() ||
			m_hStepDialog.Get() ||
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
			m_hManualBanDialog.Get() ||
#endif
			(m_pContextMenu && m_pContextMenu->IsVisible()) ||
			(m_pReactionContextMenu && m_pReactionContextMenu->IsVisible()))
		{
			CloseAllSubDialogs();
			RequestFocus();
			return;
		}

		// ESC in main menu hides the window, but leaves g_bReopenOnGameUI = true
		// so it will reappear automatically when GameUI is opened again.
		// Only clicking the 'X' button explicitly cancels auto-reopening.
		SetVisible(false);
		return;
	}

	if (code == vgui::KEY_ENTER)
	{
		if (m_pTabReactions && m_pTabReactions->IsVisible() && m_pReactionList && m_pReactionList->GetSelectedItemsCount() > 0)
		{
			OnCommand("edit_reaction");
			return;
		}
		if (m_pTabSequencer && m_pTabSequencer->IsVisible() && m_pSeqStepsList && m_pSeqStepsList->GetSelectedItemsCount() > 0)
		{
			OnCommand("edit_seq_step");
			return;
		}
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
		if (m_pTabBans && m_pTabBans->IsVisible() && m_pPlayerList && m_pPlayerList->GetSelectedItemsCount() > 0)
		{
			OnCommand("ban_player");
			return;
		}
#endif
		if (m_pTabSoundpad && m_pTabSoundpad->IsVisible() && m_pSoundList && m_pSoundList->GetSelectedItemsCount() > 0)
		{
			if (m_pCheckDblClickPreview && m_pCheckDblClickPreview->IsSelected())
				PlayLocalPreview();
			else
				PlaySelected(false);
			return;
		}
	}
	else if (code == vgui::KEY_SPACE)
	{
		if (m_pTabReactions && m_pTabReactions->IsVisible())
		{
			if (m_pReactionList && m_pReactionList->GetSelectedItemsCount() > 0)
			{
				OnCommand("test_reaction");
				return;
			}
		}
		else if (m_pTabSequencer && m_pTabSequencer->IsVisible())
		{
			OnCommand("preview_slice");
			return;
		}
		else if (m_pTabSoundpad && m_pTabSoundpad->IsVisible())
		{
			if (m_pSoundList && m_pSoundList->GetSelectedItemsCount() > 0)
			{
				PlayLocalPreview();
				return;
			}
		}
	}
	else if (code == vgui::KEY_DELETE)
	{
		if (m_pTabReactions && m_pTabReactions->IsVisible())
		{
			if (m_pReactionList && m_pReactionList->GetSelectedItemsCount() > 0)
			{
				OnCommand("delete_reaction");
				return;
			}
		}
		else if (m_pTabSequencer && m_pTabSequencer->IsVisible())
		{
			if (m_pSeqStepsList && m_pSeqStepsList->GetSelectedItemsCount() > 0)
			{
				OnCommand("delete_seq_step");
				return;
			}
			else if (m_pSeqList && m_pSeqList->GetSelectedItemsCount() > 0)
			{
				OnCommand("delete_sequence");
				return;
			}
		}
		else if (m_pTabSoundpad && m_pTabSoundpad->IsVisible())
		{
			if (m_pSoundList && m_pSoundList->GetSelectedItemsCount() > 0)
			{
				OnDeleteSound();
				return;
			}
		}
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
		else if (m_pTabBans && m_pTabBans->IsVisible())
		{
			if (m_pPlayerList && m_pPlayerList->GetSelectedItemsCount() > 0)
			{
				OnCommand("ban_player");
				return;
			}
		}
#endif
	}
	else if (code == vgui::KEY_A)
	{
		// Ctrl+A: select all items in the active list
		bool ctrl = vgui::input()->IsKeyDown(vgui::KEY_LCONTROL) || vgui::input()->IsKeyDown(vgui::KEY_RCONTROL);
		if (ctrl)
		{
			vgui::ListPanel* activeList = nullptr;
			if (m_pTabSoundpad && m_pTabSoundpad->IsVisible()) activeList = m_pSoundList;
			else if (m_pTabReactions && m_pTabReactions->IsVisible()) activeList = m_pReactionList;
			else if (m_pTabSequencer && m_pTabSequencer->IsVisible()) activeList = m_pSeqStepsList;
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
			else if (m_pTabBans && m_pTabBans->IsVisible()) activeList = m_pPlayerList;
#endif
			if (activeList)
			{
				int count = activeList->GetItemCount();
				for (int i = 0; i < count; ++i)
				{
					int id = activeList->GetItemIDFromRow(i);
					if (id >= 0) activeList->AddSelectedItem(id);
				}
				return;
			}
		}
	}
	BaseClass::OnKeyCodeTyped(code);
}

void CSoundpadFrame::OnThink()
{
	BaseClass::OnThink();

	// Update Master Volume label
	if (m_pMasterVolSlider && m_pMasterVolLabel)
	{
		int mv = m_pMasterVolSlider->GetValue();
		char mvBuf[32] = {};
		StringCchPrintfA(mvBuf, sizeof(mvBuf), "Master: %d%%", mv);
		m_pMasterVolLabel->SetText(mvBuf);
	}

	// Update Volume label
	float vol = static_cast<float>(m_pVolSlider->GetValue()) / 100.0f;
	char volBuf[32] = {};
	StringCchPrintfA(volBuf, sizeof(volBuf), "Vol: %.2f", vol);
	m_pVolLabel->SetText(volBuf);

	// Update Pitch label
	float pitch = static_cast<float>(m_pPitchSlider->GetValue()) / 100.0f;
	char pitchBuf[32] = {};
	StringCchPrintfA(pitchBuf, sizeof(pitchBuf), "Pitch: %.2f", pitch);
	m_pPitchLabel->SetText(pitchBuf);

	// Update Status Label
	PlaybackStatus st = SoundEngine_GetStatus();
	if (st.isPlaying)
	{
		char stBuf[160] = {};
		if (st.isDucking)
		{
			StringCchPrintfA(stBuf, sizeof(stBuf), "DUCKING (<30%%): %s (%.1fs left, %u queued)",
				st.currentSound.c_str(), st.secondsRemaining, static_cast<unsigned>(st.chunksRemaining));
		}
		else if (st.isPaused)
		{
			StringCchPrintfA(stBuf, sizeof(stBuf), "PAUSED (MIC): %s (%.1fs left, %u queued)",
				st.currentSound.c_str(), st.secondsRemaining, static_cast<unsigned>(st.chunksRemaining));
		}
		else
		{
			StringCchPrintfA(stBuf, sizeof(stBuf), "PLAYING: %s (%.1fs left, %u queued)",
				st.currentSound.c_str(), st.secondsRemaining, static_cast<unsigned>(st.chunksRemaining));
		}
		m_pStatusLabel->SetText(stBuf);
	}
	else
	{
		m_pStatusLabel->SetText("Status: Ready (Idle)");
	}

	// Update dynamic playback state in Sound List
	if (m_pSoundList)
	{
		static std::string s_lastActiveTrack;
		static bool s_lastPlaying = false;
		static bool s_lastDucking = false;
		static bool s_lastPaused = false;

		if (st.isPlaying != s_lastPlaying || st.isDucking != s_lastDucking ||
		    st.isPaused != s_lastPaused || (st.isPlaying && s_lastActiveTrack != st.currentSound))
		{
			s_lastPlaying = st.isPlaying;
			s_lastDucking = st.isDucking;
			s_lastPaused = st.isPaused;
			s_lastActiveTrack = st.isPlaying ? st.currentSound : "";

			const char* playTag = st.isDucking ? "DUCK" : (st.isPaused ? "PAUSE" : "PLAY");

			for (int r = 0; r < m_pSoundList->GetItemCount(); ++r)
			{
				int itemID = m_pSoundList->GetItemIDFromRow(r);
				if (itemID < 0) continue;
				KeyValues* kv = m_pSoundList->GetItem(itemID);
				if (!kv) continue;

				const char* path = kv->GetString("path", "");
				const char* title = kv->GetString("title", "");
				bool isMissing = (strstr(title, "[!]") != nullptr);

				if (isMissing)
				{
					kv->SetString("status", "MISSING");
				}
				else if (st.isPlaying && !st.currentSound.empty() &&
				         (_stricmp(path, st.currentSound.c_str()) == 0 ||
				          strstr(st.currentSound.c_str(), path) != nullptr ||
				          strstr(path, st.currentSound.c_str()) != nullptr))
				{
					kv->SetString("status", playTag);
				}
				else
				{
					kv->SetString("status", "");
				}

				m_pSoundList->ApplyItemChanges(itemID);
			}
		}
	}
}

void CSoundpadFrame::OnPageChanged()
{
	InvalidateLayout();
	if (m_pTabBans && m_pTabSheet && m_pTabSheet->GetActivePage() == m_pTabBans)
	{
		RefreshPlayersList();
	}
}

void CSoundpadFrame::PerformLayout()
{
	BaseClass::PerformLayout();

	int w = GetWide();
	int h = GetTall();

	if (w < 400 || h < 300)
		return;

	// Position Header Master controls (top right next to Close button)
	if (m_pCheckMute)
	{
		m_pCheckMute->SetBounds(w - 275, 4, 55, 20);
		m_pCheckMute->MoveToFront();
	}
	if (m_pMasterVolLabel)
	{
		m_pMasterVolLabel->SetBounds(w - 215, 4, 85, 20);
		m_pMasterVolLabel->MoveToFront();
	}
	if (m_pMasterVolSlider)
	{
		m_pMasterVolSlider->SetBounds(w - 128, 4, 100, 20);
		m_pMasterVolSlider->MoveToFront();
	}

	const int sheetX = 6;
	const int sheetY = 28;
	const int sheetW = w - 12;
	const int sheetH = h - sheetY - 6;

	m_pTabSheet->SetBounds(sheetX, sheetY, sheetW, sheetH);

	const int tabHeaderH = 28;
	const int pageW = sheetW;
	const int pageH = (std::max)(100, sheetH - tabHeaderH);

	m_pTabSoundpad->SetBounds(0, tabHeaderH, pageW, pageH);
	m_pTabReactions->SetBounds(0, tabHeaderH, pageW, pageH);
	m_pTabSequencer->SetBounds(0, tabHeaderH, pageW, pageH);
	m_pTabTTS->SetBounds(0, tabHeaderH, pageW, pageH);
	if (m_pTabBans)
		m_pTabBans->SetBounds(0, tabHeaderH, pageW, pageH);

	// ==============================================================================================
	// TAB 1: Soundpad
	// ==============================================================================================
	const int tw = pageW;
	const int th = pageH;

	const int catWidth = 170;
	const int margin = 8;
	const int topY = 8;
	const int bottomH = 92;
	const int contentH = (std::max)(100, th - topY - bottomH - margin);

	m_pCatHeader->SetBounds(margin, topY, catWidth, 20);
	m_pCatTree->SetBounds(margin, topY + 22, catWidth, contentH - 22 - 28);
	m_pBtnAddCat->SetBounds(margin, topY + contentH - 24, catWidth, 24);

	const int soundX = margin + catWidth + margin;
	const int soundW = (std::max)(150, tw - soundX - margin);
	m_pSoundHeader->SetBounds(soundX, topY, 85, 20);

	const int btnW = 58;
	const int btnGap = 4;
	const int rightBtnX = soundX + soundW - (btnW * 4 + btnGap * 3);
	const int filterW = (std::max)(60, rightBtnX - (soundX + 90) - 6);
	m_pSearchFilter->SetBounds(soundX + 90, topY, filterW, 20);

	m_pBtnAddFile->SetBounds(rightBtnX, topY, btnW, 20);
	m_pBtnAddFolder->SetBounds(rightBtnX + btnW + btnGap, topY, btnW, 20);
	m_pBtnRemoveSound->SetBounds(rightBtnX + (btnW + btnGap) * 2, topY, btnW, 20);
	m_pBtnSave->SetBounds(rightBtnX + (btnW + btnGap) * 3, topY, btnW, 20);

	m_pSoundList->SetBounds(soundX, topY + 25, soundW, contentH - 25);

	const int bottomY = th - bottomH;
	int curX = margin;
	m_pBtnPlay->SetBounds(curX, bottomY, 80, 26); curX += 84;
	m_pBtnQueue->SetBounds(curX, bottomY, 70, 26); curX += 74;
	m_pBtnPreview->SetBounds(curX, bottomY, 75, 26); curX += 79;
	m_pBtnStop->SetBounds(curX, bottomY, 65, 26); curX += 69;
	m_pBtnRandom->SetBounds(curX, bottomY, 75, 26); curX += 83;

	m_pVolLabel->SetBounds(curX, bottomY, 60, 26); curX += 62;
	m_pVolSlider->SetBounds(curX, bottomY, 95, 26); curX += 101;

	m_pPitchLabel->SetBounds(curX, bottomY, 65, 26); curX += 67;
	m_pPitchSlider->SetBounds(curX, bottomY, (std::max)(80, tw - curX - margin), 26);

	m_pCheckDblClickPreview->SetBounds(margin + 110, bottomY + 32, 130, 24);
	m_pCheckAutoHide->SetBounds(margin + 245, bottomY + 32, 135, 24);
	m_pStatusLabel->SetBounds(margin + 385, bottomY + 32, (std::max)(80, tw - (margin + 385) - margin), 24);

	// ==============================================================================================
	// TAB 2: Reactions & Triggers (Buttons moved to bottom panel, list expanded cleanly)
	// ==============================================================================================
	const int rtw = pageW;
	const int rth = pageH;
	const int rBtnH = 26;
	const int rBottomY = (std::max)(10, rth - rBtnH - 10);
	const int rListH = (std::max)(60, rBottomY - 16);

	m_pReactionList->SetBounds(8, 8, (std::max)(200, rtw - 16), rListH);

	int rx = 8;
	m_pBtnAddReaction->SetBounds(rx, rBottomY, 110, rBtnH); rx += 116;
	m_pBtnEditReaction->SetBounds(rx, rBottomY, 100, rBtnH); rx += 106;
	m_pBtnDeleteReaction->SetBounds(rx, rBottomY, 80, rBtnH); rx += 86;
	m_pBtnTestReaction->SetBounds(rx, rBottomY, 90, rBtnH); rx += 96;
	m_pBtnSaveReactions->SetBounds((std::max)(rx + 10, rtw - 110), rBottomY, 100, rBtnH);

	// ==============================================================================================
	// TAB 3: Sequencer & Slicer
	// ==============================================================================================
	const int stw = pageW;
	const int sth = pageH;

	const int seqLeftW = 230;
	const int slicerH = 80;
	const int seqTopH = (std::max)(120, sth - slicerH - 16);

	// Left: Macro list
	m_pSeqListLabel->SetBounds(8, 6, seqLeftW, 20);
	m_pSeqList->SetBounds(8, 28, seqLeftW, seqTopH - 28 - 28);
	m_pBtnAddSeq->SetBounds(8, seqTopH - 24, 55, 24);
	m_pBtnDelSeq->SetBounds(67, seqTopH - 24, 52, 24);
	m_pBtnPlaySeq->SetBounds(123, seqTopH - 24, 52, 24);
	m_pBtnAddSeqToLib->SetBounds(179, seqTopH - 24, 60, 24);

	// Right: Steps list
	const int stepsX = 8 + seqLeftW + 8;
	const int stepsW = (std::max)(200, stw - stepsX - 8);
	m_pStepsListLabel->SetBounds(stepsX, 6, stepsW, 20);
	m_pSeqStepsList->SetBounds(stepsX, 28, stepsW, seqTopH - 28 - 28);
	m_pBtnAddStep->SetBounds(stepsX, seqTopH - 24, 75, 24);
	m_pBtnEditStep->SetBounds(stepsX + 80, seqTopH - 24, 80, 24);
	m_pBtnDelStep->SetBounds(stepsX + 165, seqTopH - 24, 75, 24);

	// Bottom: Timeline Slicer
	const int sliceY = sth - slicerH;
	m_pTimelineLabel->SetBounds(8, sliceY, (std::max)(200, stw - 16), 20);
	m_pTimelineSlider->SetBounds(8, sliceY + 22, (std::max)(200, stw - 16), 24);

	int slBtnX = 8;
	m_pBtnSetStart->SetBounds(slBtnX, sliceY + 50, 120, 24); slBtnX += 126;
	m_pBtnSetEnd->SetBounds(slBtnX, sliceY + 50, 120, 24); slBtnX += 126;
	m_pBtnPreviewSlice->SetBounds(slBtnX, sliceY + 50, 110, 24);

	// ==============================================================================================
	// TAB 4: Text-To-Speech (Klatter Formant Engine)
	// ==============================================================================================
	const int ttsW = pageW;
	int ttsY = 12;

	m_pTtsEngineLabel->SetBounds(16, ttsY, 90, 24);
	m_pComboTtsEngine->SetBounds(110, ttsY, 320, 24); ttsY += 28;

	m_pSapiVoiceLabel->SetBounds(16, ttsY, 90, 24);
	m_pComboSapiVoice->SetBounds(110, ttsY, 320, 24); ttsY += 34;

	m_pTtsTextLabel->SetBounds(16, ttsY, ttsW - 32, 20); ttsY += 24;
	m_pTtsTextEntry->SetBounds(16, ttsY, ttsW - 32, 28); ttsY += 38;

	m_pBtnTtsSpeak->SetBounds(16, ttsY, 150, 28);
	m_pBtnTtsPreview->SetBounds(176, ttsY, 140, 28); ttsY += 38;

	m_pTtsVolLabel->SetBounds(16, ttsY, 100, 24);
	m_pTtsVolSlider->SetBounds(120, ttsY, 220, 24); ttsY += 28;

	m_pTtsPitchLabel->SetBounds(16, ttsY, 100, 24);
	m_pTtsPitchSlider->SetBounds(120, ttsY, 220, 24); ttsY += 32;

	m_pCheckTtsRepoJitter->SetBounds(16, ttsY, 450, 24); ttsY += 26;
	m_pCheckTtsWhisper->SetBounds(16, ttsY, 450, 24); ttsY += 26;
	if (m_pCheckTtsRandomStyle)
	{
		m_pCheckTtsRandomStyle->SetBounds(16, ttsY, 450, 24); ttsY += 26;
	}
	m_pCheckTtsAutoSay->SetBounds(16, ttsY, 450, 24); ttsY += 32;

	m_pTtsHintLabel->SetBounds(16, ttsY, ttsW - 32, 24);

#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	// ==============================================================================================
	// TAB 5: Player Blacklist (Bans)
	// ==============================================================================================
	if (m_pTabBans)
	{
		const int bMargin = 12;
		const int bBtnH = 26;
		const int bTopY = 8;
		const int bW = pageW - bMargin * 2;

		m_pPlayerHeaderLabel->SetBounds(bMargin, bTopY, bW, 20);

		int bBtnX = bMargin;
		m_pBtnBanPlayer->SetBounds(bBtnX, bTopY + 24, 150, bBtnH); bBtnX += 156;
		m_pBtnAddBanManual->SetBounds(bBtnX, bTopY + 24, 180, bBtnH); bBtnX += 186;
		m_pBtnRefreshPlayers->SetBounds(bBtnX, bTopY + 24, 110, bBtnH);

		m_pCheckDotStop->SetBounds(bMargin, pageH - 32, bW, 24);
		m_pPlayerList->SetBounds(bMargin, bTopY + 56, bW, (std::max)(100, pageH - (bTopY + 56 + 38)));
	}
#endif
}
