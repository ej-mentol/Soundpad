#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

enum class ReactionEventType
{
	Death = 0,      // Local player died (Condition: GENERIC, SUICIDE, MONSTER, WORLD, PLAYER, FALL, POISON, or killer name)
	Damage = 1,     // Local player took damage (Condition: damage type, minimum damage threshold, or <HP)
	Kill = 2,       // Local player scored a kill (Condition: ANY, MONSTER, PLAYER, or monster/player name)
	Chat = 3,       // Incoming chat message matches regex or substring
	Command = 4,    // Console command / keybind trigger (Condition: command name, e.g. "medic", optional "<35")
	OtherDeath = 5  // Teammate or other player died (Condition: ANY, player name, or killer name)
};

struct ReactionItem
{
	std::string id;            // Unique identifier
	std::string soundPath;     // e.g. "debris/debris7.wav"
	ReactionEventType eventType = ReactionEventType::Death;
	std::string condition;     // e.g. "GENERIC", "POISON", "Alien Slave", "(?i).*killed.*"
	float chance = 100.0f;     // 1.0 .. 100.0 %
	float cooldown = 2.0f;     // Seconds between triggers
	float volume = 1.0f;
	float pitch = 1.0f;
	float fadeInSec = 0.0f;
	float fadeOutSec = 0.0f;
	int fadeCurve = 2;         // 0 = Linear, 1 = Quadratic, 2 = Sine
	bool fxReverb = false;
	bool fxZigzag = false;
	double lastTriggeredTime = -1000.0; // Runtime cooldown tracking
};

struct SequenceStep
{
	std::string soundPath;
	float startSec = 0.0f;      // Slice start
	float endSec = 0.0f;        // Slice end (0 = to end of sound)
	int repeats = 1;            // Number of repetitions (e.g. 4 times "sec")
	float pauseAfterSec = 0.0f; // Gap before next step in seconds
	float volume = 1.0f;
	float pitch = 1.0f;
	float pitchStep = 0.0f;     // Pitch increment per repeat
	float fadeInSec = 0.0f;
	float fadeOutSec = 0.0f;
	int fadeCurve = 2;          // 0 = Linear, 1 = Quadratic, 2 = Sine
	bool fxReverb = false;
	bool fxZigzag = false;
};

struct SequenceItem
{
	std::string name;           // Macro name (e.g. "Security Stutter")
	std::string hotkey;
	std::vector<SequenceStep> steps;
};

struct ChatAliasItem
{
	std::string command;               // Chat command/word e.g. "noo", "kick"
	std::string soundPath;             // Sound file e.g. "scientist/nooo.wav"
	float volume = 1.0f;              // 0.0 .. 1.0
	float pitch = 1.0f;               // 0.5 .. 2.0
	float cooldown = 2.0f;             // Seconds between executions
	double lastTriggeredTime = -1000.0;// Monotonic time of last trigger
};

struct BannedPlayerItem
{
	uint64_t steamID = 0;              // 64-bit SteamID
	std::string playerName;            // Name at time of ban
	std::string reason;                // Ban reason
	std::string banDate;               // Formatted date string
};

struct SoundItem
{
	std::string title;
	std::string path; // relative to sound/ (e.g. "barney/c2a2_ba_launch.wav")
	float volume = 1.0f;
	float pitch = 1.0f;
	std::string hotkey; // e.g. "x", "F5", or bind name
	std::string chatTrigger; // e.g. "!laugh", "fart", "gg"
	bool isSequence = false;
	double lastTriggeredTime = -1000.0; // runtime cooldown tracking
};

struct CategoryNode
{
	std::string name;
	std::vector<CategoryNode> subcategories;
	std::vector<SoundItem> sounds;
};

class CSoundpadConfig
{
public:
	static CSoundpadConfig& Get();

	void Load();
	void Save(bool verbose = false);

	const std::vector<CategoryNode>& GetRootCategories() const { return m_categories; }
	std::vector<CategoryNode>& GetRootCategories() { return m_categories; }
	const std::vector<CategoryNode>& GetCategories() const { return m_categories; }
	std::vector<CategoryNode>& GetCategories() { return m_categories; }

	// Finds sounds matching category path (e.g. "Half-Life/Barney").
	std::vector<SoundItem> GetSoundsForCategory(const std::string& categoryPath) const;

	// Random sound queries
	bool GetRandomSound(const std::string& categoryPath, SoundItem& outSound) const;
	bool GetRandomSoundGlobal(SoundItem& outSound) const;

	// Sequential navigation (next/prev)
	bool GetNextSound(const std::string& categoryPath, const std::string& currentPath, SoundItem& outSound) const;
	bool GetPrevSound(const std::string& categoryPath, const std::string& currentPath, SoundItem& outSound) const;

	bool AddCategory(const std::string& categoryName, const std::string& parentCategory = "");
	void AddSoundToCategory(const std::string& categoryPath, const SoundItem& item, int insertIndex = -1);
	const SequenceItem* FindSequence(const std::string& name) const;
	void RemoveSoundFromCategory(const std::string& categoryPath, const std::string& soundPath);
	// Moves one sound from a specific category to another (creating the target if needed).
	bool MoveSoundToCategory(const std::string& fromCategory, const std::string& toCategory, const std::string& soundPath);
	// Batch variant: saves the config once for the whole selection.
	size_t MoveSoundsToCategory(const std::string& fromCategory, const std::string& toCategory, const std::vector<std::string>& soundPaths);
	bool UpdateSoundSettings(const std::string& soundPath, float volume, float pitch);
	SoundItem* FindSoundByChatTrigger(const std::string& trigger);
	SoundItem* FindSoundByPath(const std::string& soundPath);
	bool SetSoundChatTrigger(const std::string& soundPath, const std::string& trigger);
	bool SetSoundHotkey(const std::string& soundPath, const std::string& hotkey);

	// Reactions
	const std::vector<ReactionItem>& GetReactions() const { return m_reactions; }
	std::vector<ReactionItem>& GetReactions() { return m_reactions; }
	void AddReaction(const ReactionItem& item);
	void RemoveReaction(size_t index);
	bool UpdateReaction(size_t index, const ReactionItem& item);

	// Sequences / Macro Timelines
	const std::vector<SequenceItem>& GetSequences() const { return m_sequences; }
	std::vector<SequenceItem>& GetSequences() { return m_sequences; }
	void AddSequence(const SequenceItem& item);
	void RemoveSequence(size_t index);
	bool UpdateSequence(size_t index, const SequenceItem& item);

	// Chat Aliases
	const std::vector<ChatAliasItem>& GetChatAliases() const { return m_chatAliases; }
	std::vector<ChatAliasItem>& GetChatAliases() { return m_chatAliases; }
	void AddChatAlias(const ChatAliasItem& item);
	void RemoveChatAlias(size_t index);
	bool UpdateChatAlias(size_t index, const ChatAliasItem& item);
	ChatAliasItem* FindChatAlias(const std::string& command);

	// Player Bans (identified by SteamID64 or Nickname when SteamID is 0)
	const std::vector<BannedPlayerItem>& GetBannedPlayers() const { return m_bannedPlayers; }
	std::vector<BannedPlayerItem>& GetBannedPlayers() { return m_bannedPlayers; }
	bool IsPlayerBanned(uint64_t steamID, const std::string& name = "") const;
	void BanPlayer(uint64_t steamID, const std::string& name = "", const std::string& reason = "Manual ban");
	bool UnbanPlayer(uint64_t steamID);
	bool UnbanPlayerByName(const std::string& name);

	// Dot Stop (".")
	bool IsDotStopEnabled() const { return m_dotStopEnabled; }
	void SetDotStopEnabled(bool enabled);

	// Global Soundpad Settings
	float GetMasterVolume() const { return m_masterVolume; }
	void SetMasterVolume(float vol) { m_masterVolume = (vol < 0.0f) ? 0.0f : ((vol > 1.0f) ? 1.0f : vol); }

	bool IsMuted() const { return m_isMuted; }
	void SetMuted(bool mute) { m_isMuted = mute; }

	bool GetTtsRandomStyle() const { return m_ttsRandomStyle; }
	void SetTtsRandomStyle(bool enabled) { m_ttsRandomStyle = enabled; }

	const std::string& GetTtsEngine() const { return m_ttsEngine; }
	void SetTtsEngine(const std::string& engine) { m_ttsEngine = engine; }

	const std::string& GetSapiVoice() const { return m_sapiVoice; }
	void SetSapiVoice(const std::string& voice) { m_sapiVoice = voice; }

	void Reload();

private:
	CSoundpadConfig();
	void PopulateDefaults();

	float m_masterVolume = 1.0f;
	bool m_isMuted = false;
	bool m_ttsRandomStyle = false;
	std::string m_ttsEngine = "klatter";
	std::string m_sapiVoice;
	bool m_dotStopEnabled = true;

	std::vector<CategoryNode> m_categories;
	std::vector<ReactionItem> m_reactions;
	std::vector<SequenceItem> m_sequences;
	std::vector<ChatAliasItem> m_chatAliases;
	std::vector<BannedPlayerItem> m_bannedPlayers;
	std::string m_configFilePath;
};
