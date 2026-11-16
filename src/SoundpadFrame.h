#pragma once

// ============================================================================
// STEAMID & BAN LIST SUBSYSTEM TOGGLE
// To re-enable the SteamID player querying and ban list tab, simply uncomment:
// #define SOUNDPAD_ENABLE_STEAMID_BAN_LIST
// ============================================================================

#include <vgui/vgui.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/ListPanel.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/TextEntry.h>
#include <vgui_controls/Slider.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Menu.h>
#include <vgui_controls/TreeView.h>
#include <vgui_controls/FileOpenDialog.h>
#include <vgui_controls/InputDialog.h>
#include <vgui_controls/PropertySheet.h>
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/PHandle.h>

#include "SoundpadConfig.h"

class CSoundpadFrame;
class CReactionEditDialog;
class CSequenceStepDialog;
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
class CManualBanDialog;
#endif

class CSoundpadFrame : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CSoundpadFrame, vgui::Frame);

public:
	CSoundpadFrame(vgui::Panel* parent);
	virtual ~CSoundpadFrame();

	void RefreshCategories();
	void RefreshSoundList();
	void RefreshReactionsList();
	void RefreshSequencerList();
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	void RefreshPlayersList();
#else
	void RefreshPlayersList() {}
#endif

	static bool AddSoundFile(const std::string& rawPath, const std::string& targetCategory, int insertIndex = -1);
	static int AddSoundFolder(const std::string& rawPath, const std::string& targetCategory);
	bool AddSequenceAsSound(const std::string& seqName, const std::string& targetCategory, int insertIndex = -1);

	const std::string& GetSelectedCategory() const { return m_selectedCategory; }
	SoundItem* GetSelectedSound();
	SoundItem* GetSoundByRow(int row1Based);
	std::vector<std::string> CollectSelectedSoundPaths();
	void SelectSoundByPath(const std::string& path);
	void SetVisible(bool state) override;
	void CloseAllSubDialogs();

protected:
	void OnClose() override;
	MESSAGE_FUNC(OnCloseFrameButtonPressed, "CloseFrameButtonPressed");
	void OnCommand(const char* command) override;
	void OnThink() override;
	void PerformLayout() override;
	void ApplySchemeSettings(vgui::IScheme* pScheme) override;
	void OnKeyCodeTyped(vgui::KeyCode code) override;

	MESSAGE_FUNC_PTR(OnTextChanged, "TextChanged", panel);
	MESSAGE_FUNC_PTR(OnItemSelected, "ItemSelected", panel);
	MESSAGE_FUNC_INT(OnTreeViewItemSelected, "TreeViewItemSelected", itemIndex);
	MESSAGE_FUNC_INT(OnOpenContextMenu, "OpenContextMenu", itemID);
	MESSAGE_FUNC_PARAMS(OnFileSelected, "FileSelected", kv);
	MESSAGE_FUNC_PARAMS(OnInputCompleted, "InputCompleted", kv);
	MESSAGE_FUNC(OnInputCanceled, "InputCanceled");
	MESSAGE_FUNC_PARAMS(OnSliderMoved, "SliderMoved", kv);
	MESSAGE_FUNC_PARAMS(OnCheckButtonChecked, "CheckButtonChecked", kv);
	MESSAGE_FUNC(OnPageChanged, "PageChanged");
	MESSAGE_FUNC_INT(OnItemDoubleClicked, "ItemDoubleClicked", itemID);

private:
	void PlaySelected(bool enqueue);
	void PlayLocalPreview();
	void StopPlayback();
	void PlayRandom();
	void OnAddFile();
	void OnAddFolder();
	void OnDeleteSound();
	void OnAddCategory();
	void OnMoveToCategory();
	void MoveSelectedSoundsToCategory(const std::string& targetCategory);

	// Top Header: Master Volume & Mute
	vgui::CheckButton* m_pCheckMute = nullptr;
	vgui::Label* m_pMasterVolLabel = nullptr;
	vgui::Slider* m_pMasterVolSlider = nullptr;

	// Tab container
	vgui::PropertySheet* m_pTabSheet = nullptr;
	vgui::PropertyPage* m_pTabSoundpad = nullptr;
	vgui::PropertyPage* m_pTabReactions = nullptr;
	vgui::PropertyPage* m_pTabSequencer = nullptr;
	vgui::PropertyPage* m_pTabTTS = nullptr;
	vgui::PropertyPage* m_pTabBans = nullptr;

	// === TAB 1: Soundpad Controls ===
	// Left: Category Tree
	vgui::Label* m_pCatHeader = nullptr;
	vgui::TreeView* m_pCatTree = nullptr;
	vgui::Button* m_pBtnAddCat = nullptr;

	// Right: Sound List & Filter
	vgui::Label* m_pSoundHeader = nullptr;
	vgui::TextEntry* m_pSearchFilter = nullptr;
	vgui::Button* m_pBtnAddFile = nullptr;
	vgui::Button* m_pBtnAddFolder = nullptr;
	vgui::Button* m_pBtnRemoveSound = nullptr;
	vgui::Button* m_pBtnSave = nullptr;
	vgui::ListPanel* m_pSoundList = nullptr;

	// Bottom: Controls & Sliders
	vgui::Button* m_pBtnPlay = nullptr;
	vgui::Button* m_pBtnQueue = nullptr;
	vgui::Button* m_pBtnPreview = nullptr;
	vgui::Button* m_pBtnStop = nullptr;
	vgui::Button* m_pBtnRandom = nullptr;

	vgui::Label* m_pVolLabel = nullptr;
	vgui::Slider* m_pVolSlider = nullptr;

	vgui::Label* m_pPitchLabel = nullptr;
	vgui::Slider* m_pPitchSlider = nullptr;

	vgui::CheckButton* m_pCheckDblClickPreview = nullptr;
	vgui::CheckButton* m_pCheckAutoHide = nullptr;
	vgui::Label* m_pStatusLabel = nullptr;

	// === TAB 2: Reactions & Triggers ===
	vgui::Button* m_pBtnAddReaction = nullptr;
	vgui::Button* m_pBtnEditReaction = nullptr;
	vgui::Button* m_pBtnDeleteReaction = nullptr;
	vgui::Button* m_pBtnTestReaction = nullptr;
	vgui::Button* m_pBtnSaveReactions = nullptr;
	vgui::ListPanel* m_pReactionList = nullptr;

	// === TAB 3: Sequencer / Macro Timeline & Slicer ===
	vgui::Label* m_pSeqListLabel = nullptr;
	vgui::ListPanel* m_pSeqList = nullptr;
	vgui::Button* m_pBtnAddSeq = nullptr;
	vgui::Button* m_pBtnDelSeq = nullptr;
	vgui::Button* m_pBtnPlaySeq = nullptr;
	vgui::Button* m_pBtnAddSeqToLib = nullptr;

	// Steps List
	vgui::Label* m_pStepsListLabel = nullptr;
	vgui::ListPanel* m_pSeqStepsList = nullptr;
	vgui::Button* m_pBtnAddStep = nullptr;
	vgui::Button* m_pBtnEditStep = nullptr;
	vgui::Button* m_pBtnDelStep = nullptr;

	// Slicer controls
	vgui::Label* m_pTimelineLabel = nullptr;
	vgui::Slider* m_pTimelineSlider = nullptr;
	vgui::Button* m_pBtnSetStart = nullptr;
	vgui::Button* m_pBtnSetEnd = nullptr;
	vgui::Button* m_pBtnPreviewSlice = nullptr;

	// === TAB 4: Text-To-Speech (TTS) ===
	vgui::Label* m_pTtsEngineLabel = nullptr;
	vgui::ComboBox* m_pComboTtsEngine = nullptr;
	vgui::Label* m_pSapiVoiceLabel = nullptr;
	vgui::ComboBox* m_pComboSapiVoice = nullptr;
	vgui::Label* m_pTtsTextLabel = nullptr;
	vgui::TextEntry* m_pTtsTextEntry = nullptr;
	vgui::Button* m_pBtnTtsSpeak = nullptr;
	vgui::Button* m_pBtnTtsPreview = nullptr;
	vgui::Label* m_pTtsVolLabel = nullptr;
	vgui::Slider* m_pTtsVolSlider = nullptr;
	vgui::Label* m_pTtsPitchLabel = nullptr;
	vgui::Slider* m_pTtsPitchSlider = nullptr;
	vgui::CheckButton* m_pCheckTtsRepoJitter = nullptr;
	vgui::CheckButton* m_pCheckTtsWhisper = nullptr;
	vgui::CheckButton* m_pCheckTtsAutoSay = nullptr;
	vgui::CheckButton* m_pCheckTtsRandomStyle = nullptr;
	vgui::Label* m_pTtsHintLabel = nullptr;

#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	// === TAB 5: Server Players & Blacklist (Bans) ===
	vgui::Label* m_pPlayerHeaderLabel = nullptr;
	vgui::ListPanel* m_pPlayerList = nullptr;
	vgui::Button* m_pBtnBanPlayer = nullptr;
	vgui::Button* m_pBtnAddBanManual = nullptr;
	vgui::Button* m_pBtnRefreshPlayers = nullptr;
	vgui::CheckButton* m_pCheckDotStop = nullptr;
#else
	vgui::CheckButton* m_pCheckDotStop = nullptr;
#endif

	float m_sliceStart = 0.0f;
	float m_sliceEnd = 0.0f;
	float m_currentSoundDuration = 0.0f;

	vgui::Menu* m_pContextMenu = nullptr;
	vgui::Menu* m_pReactionContextMenu = nullptr;

	enum class DialogAction { None, AddFile, AddFolder, AddCategory, AddSeqName, SetChatTrigger, SetHotkey, MoveToCategory };
	DialogAction m_dialogAction = DialogAction::None;
	std::vector<std::string> m_dialogTargets; // Sounds the pending InputDialog applies to
	vgui::DHANDLE<vgui::FileOpenDialog> m_hFileOpenDialog;
	vgui::DHANDLE<vgui::InputDialog> m_hInputDialog;
	vgui::DHANDLE<CReactionEditDialog> m_hReactionDialog;
	vgui::DHANDLE<CSequenceStepDialog> m_hStepDialog;
#ifdef SOUNDPAD_ENABLE_STEAMID_BAN_LIST
	vgui::DHANDLE<CManualBanDialog> m_hManualBanDialog;
#endif

	std::string m_selectedCategory;
	std::vector<SoundItem> m_currentSounds;
	bool m_bUpdatingSliders = false;
};

extern vgui::DHANDLE<CSoundpadFrame> g_hSoundpadFrame;
