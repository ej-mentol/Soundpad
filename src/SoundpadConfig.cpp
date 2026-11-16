#include "SoundpadConfig.h"
#include "plugins.h"

#include <IKeyValuesSystem.h>
#include <KeyValues.h>
#include <tier1/utlbuffer.h>
#include <fstream>
#include <random>
#include <algorithm>
#include <ctime>
#include <cstdlib>

struct KeyValuesDeleter
{
	void operator()(KeyValues* p) const
	{
		if (p)
			p->deleteThis();
	}
};
using KeyValuesPtr = std::unique_ptr<KeyValues, KeyValuesDeleter>;

CSoundpadConfig& CSoundpadConfig::Get()
{
	static CSoundpadConfig instance;
	return instance;
}

CSoundpadConfig::CSoundpadConfig()
{
	m_configFilePath = "soundpad/soundpad.vdf";
	PopulateDefaults();
}

void CSoundpadConfig::PopulateDefaults()
{
	m_categories.clear();

	CategoryNode hl;
	hl.name = "Half-Life";

	CategoryNode barney;
	barney.name = "Barney";
	barney.sounds.emplace_back(SoundItem{ "Launch Barney", "barney/c2a2_ba_launch.wav", 1.0f, 1.0f, "" }); //-V691
	barney.sounds.emplace_back(SoundItem{ "Bring it on", "barney/ba_bring.wav", 1.0f, 1.0f, "" });
	barney.sounds.emplace_back(SoundItem{ "Yes Sir", "barney/yessir.wav", 1.0f, 1.0f, "" });
	barney.sounds.emplace_back(SoundItem{ "Open It", "barney/openit.wav", 1.0f, 1.0f, "" });
	hl.subcategories.push_back(std::move(barney));

	CategoryNode hgrunt;
	hgrunt.name = "HGrunt";
	hgrunt.sounds.emplace_back(SoundItem{ "Kick!", "hgrunt/kick!.wav", 1.0f, 1.0f, "" });
	hgrunt.sounds.emplace_back(SoundItem{ "Charge", "hgrunt/c2a2_hg_charge.wav", 1.0f, 1.0f, "" });
	hgrunt.sounds.emplace_back(SoundItem{ "Check In", "hgrunt/checkin.wav", 1.0f, 1.0f, "" });
	hl.subcategories.push_back(std::move(hgrunt));

	CategoryNode scientist;
	scientist.name = "Scientist";
	scientist.sounds.emplace_back(SoundItem{ "Scream", "scientist/scream01.wav", 1.0f, 1.0f, "" });
	scientist.sounds.emplace_back(SoundItem{ "Nooo!", "scientist/nooo.wav", 1.0f, 1.0f, "" });
	scientist.sounds.emplace_back(SoundItem{ "Fellow Scientist", "scientist/hello.wav", 1.0f, 1.0f, "" });
	hl.subcategories.push_back(std::move(scientist));

	m_categories.push_back(std::move(hl));

	CategoryNode sfx;
	sfx.name = "Buttons & SFX";
	sfx.sounds.emplace_back(SoundItem{ "Bell", "buttons/bell1.wav", 1.0f, 1.0f, "" });
	sfx.sounds.emplace_back(SoundItem{ "Button 1", "buttons/button1.wav", 1.0f, 1.0f, "" });
	sfx.sounds.emplace_back(SoundItem{ "Spark", "buttons/spark5.wav", 1.0f, 1.0f, "" });
	m_categories.push_back(std::move(sfx));

	// Default Game Reactions
	m_reactions.clear();
	{
		ReactionItem r1;
		r1.id = "death_generic";
		r1.soundPath = "debris/debris7.wav";
		r1.condition = "GENERIC";
		r1.chance = 39.0f;
		r1.cooldown = 3.0f;
		r1.volume = 1.0f;
		m_reactions.push_back(r1);

		ReactionItem r2;
		r2.id = "death_poison";
		r2.soundPath = "scientist/nooo.wav";
		r2.condition = "POISON";
		r2.chance = 45.0f;
		r2.cooldown = 3.0f;
		r2.volume = 1.0f;
		m_reactions.push_back(r2);

		ReactionItem r3;
		r3.id = "kill_monster";
		r3.soundPath = "barney/ba_bring.wav";
		r3.eventType = ReactionEventType::Kill;
		r3.condition = "Monster";
		r3.chance = 20.0f;
		r3.cooldown = 4.0f;
		r3.volume = 1.0f;
		m_reactions.push_back(r3);
	}

	// Default Macro Sequence
	m_sequences.clear();
	{
		SequenceItem seq;
		seq.name = "Security Stutter";
		seq.hotkey = "KP_5";

		SequenceStep step1;
		step1.soundPath = "barney/c2a2_ba_launch.wav"; //-V691
		step1.startSec = 0.0f;
		step1.endSec = 0.24f; // "sec..."
		step1.repeats = 4;    // sec-sec-sec-sec
		step1.pauseAfterSec = 0.02f;
		step1.volume = 1.0f;
		step1.pitch = 1.0f;
		step1.pitchStep = 0.03f;
		seq.steps.push_back(step1);

		SequenceStep step2;
		step2.soundPath = "barney/c2a2_ba_launch.wav"; //-V691
		step2.startSec = 0.0f;
		step2.endSec = 2.0f; // "...urity!"
		step2.volume = 1.0f;
		step2.pitch = 1.12f;
		seq.steps.push_back(step2);

		m_sequences.push_back(seq);
	}

	// Default Chat Aliases
	m_chatAliases.clear();
	{
		ChatAliasItem a1;
		a1.command = "noo";
		a1.soundPath = "scientist/nooo.wav";
		a1.volume = 1.0f;
		a1.cooldown = 2.0f;
		m_chatAliases.push_back(a1);

		ChatAliasItem a2;
		a2.command = "kick";
		a2.soundPath = "hgrunt/kick!.wav";
		a2.volume = 1.0f;
		a2.cooldown = 2.0f;
		m_chatAliases.push_back(a2);

		ChatAliasItem a3;
		a3.command = "scream";
		a3.soundPath = "scientist/scream01.wav";
		a3.volume = 1.0f;
		a3.cooldown = 2.0f;
		m_chatAliases.push_back(a3);
	}

	m_bannedPlayers.clear();
	m_dotStopEnabled = true;
}

static void SaveCategoryToKeyValues(KeyValues* kv, const CategoryNode& cat)
{
	KeyValues* catKv = kv->CreateNewKey();
	catKv->SetName(cat.name.c_str());

	if (!cat.subcategories.empty())
	{
		KeyValues* subKv = new KeyValues("subcategories");
		for (const auto& sub : cat.subcategories)
			SaveCategoryToKeyValues(subKv, sub);
		catKv->AddSubKey(subKv);
	}

	if (!cat.sounds.empty())
	{
		KeyValues* soundsKv = new KeyValues("sounds");
		for (const auto& s : cat.sounds)
		{
			KeyValues* sKv = soundsKv->CreateNewKey();
			sKv->SetName("sound");
			sKv->SetString("title", s.title.c_str());
			sKv->SetString("path", s.path.c_str());
			sKv->SetFloat("volume", s.volume);
			sKv->SetFloat("pitch", s.pitch);
			sKv->SetString("hotkey", s.hotkey.c_str());
			if (!s.chatTrigger.empty())
				sKv->SetString("trigger", s.chatTrigger.c_str());
			if (s.isSequence)
				sKv->SetInt("is_seq", 1);
		}
		catKv->AddSubKey(soundsKv);
	}
}

static CategoryNode LoadCategoryFromKeyValues(KeyValues* catKv)
{
	CategoryNode cat;
	if (!catKv)
		return cat;

	const char* name = catKv->GetName();
	cat.name = name ? name : "";

	KeyValues* subKv = catKv->FindKey("subcategories");
	if (subKv)
	{
		for (KeyValues* k = subKv->GetFirstSubKey(); k; k = k->GetNextKey())
			cat.subcategories.emplace_back(LoadCategoryFromKeyValues(k));
	}

	KeyValues* soundsKv = catKv->FindKey("sounds");
	if (soundsKv)
	{
		for (KeyValues* k = soundsKv->GetFirstSubKey(); k; k = k->GetNextKey())
		{
			SoundItem s;
			s.title = k->GetString("title", "Untitled");
			s.path = k->GetString("path", "");
			s.volume = k->GetFloat("volume", 1.0f);
			s.pitch = k->GetFloat("pitch", 1.0f);
			s.hotkey = k->GetString("hotkey", "");
			s.chatTrigger = k->GetString("trigger", k->GetString("cmd", ""));
			s.isSequence = (k->GetInt("is_seq", 0) != 0) || (s.path.rfind("seq:", 0) == 0) || (s.path.rfind("macro:", 0) == 0);
			if (!s.path.empty())
				cat.sounds.push_back(std::move(s));
		}
	}

	return cat;
}

static bool EnsureKeyValuesSystem()
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
	return (g_pKeyValuesSystem != nullptr);
}

void CSoundpadConfig::Save(bool verbose)
{
	if (!EnsureKeyValuesSystem())
		return;

	KeyValuesPtr root(new KeyValues("Soundpad"));
	root->SetFloat("master_volume", m_masterVolume);
	root->SetInt("muted", m_isMuted ? 1 : 0);
	root->SetInt("tts_random_style", m_ttsRandomStyle ? 1 : 0);
	root->SetString("tts_engine", m_ttsEngine.c_str());
	root->SetString("sapi_voice", m_sapiVoice.c_str());

	KeyValues* catsKv = new KeyValues("categories");
	for (const auto& cat : m_categories)
		SaveCategoryToKeyValues(catsKv, cat);
	root->AddSubKey(catsKv);

	if (!m_reactions.empty())
	{
		KeyValues* reactKv = new KeyValues("reactions");
		for (const auto& r : m_reactions)
		{
			KeyValues* item = reactKv->CreateNewKey();
			item->SetName("reaction");
			item->SetString("id", r.id.c_str());
			item->SetString("sound", r.soundPath.c_str());
			item->SetInt("event", static_cast<int>(r.eventType));
			item->SetString("condition", r.condition.c_str());
			item->SetFloat("chance", r.chance);
			item->SetFloat("cooldown", r.cooldown);
			item->SetFloat("volume", r.volume);
			item->SetFloat("pitch", r.pitch);
			item->SetFloat("fadein", r.fadeInSec);
			item->SetFloat("fadeout", r.fadeOutSec);
			item->SetInt("fadecurve", r.fadeCurve);
			item->SetInt("reverb", r.fxReverb ? 1 : 0);
			item->SetInt("zigzag", r.fxZigzag ? 1 : 0);
		}
		root->AddSubKey(reactKv);
	}

	if (!m_sequences.empty())
	{
		KeyValues* seqsKv = new KeyValues("sequences");
		for (const auto& seq : m_sequences)
		{
			KeyValues* sItem = seqsKv->CreateNewKey();
			sItem->SetName("sequence");
			sItem->SetString("name", seq.name.c_str());
			sItem->SetString("hotkey", seq.hotkey.c_str());
			KeyValues* stepsKv = new KeyValues("steps");
			for (const auto& step : seq.steps)
			{
				KeyValues* stepKv = stepsKv->CreateNewKey();
				stepKv->SetName("step");
				stepKv->SetString("sound", step.soundPath.c_str());
				stepKv->SetFloat("start", step.startSec);
				stepKv->SetFloat("end", step.endSec);
				stepKv->SetInt("repeats", step.repeats);
				stepKv->SetFloat("pause", step.pauseAfterSec);
				stepKv->SetFloat("volume", step.volume);
				stepKv->SetFloat("pitch", step.pitch);
				stepKv->SetFloat("pitchstep", step.pitchStep);
				stepKv->SetFloat("fadein", step.fadeInSec);
				stepKv->SetFloat("fadeout", step.fadeOutSec);
				stepKv->SetInt("fadecurve", step.fadeCurve);
				stepKv->SetInt("reverb", step.fxReverb ? 1 : 0);
				stepKv->SetInt("zigzag", step.fxZigzag ? 1 : 0);
			}
			sItem->AddSubKey(stepsKv);
		}
		root->AddSubKey(seqsKv);
	}

	if (!m_chatAliases.empty())
	{
		KeyValues* aliasKv = new KeyValues("aliases");
		for (const auto& a : m_chatAliases)
		{
			KeyValues* item = aliasKv->CreateNewKey();
			item->SetName("alias");
			item->SetString("cmd", a.command.c_str());
			item->SetString("sound", a.soundPath.c_str());
			item->SetFloat("volume", a.volume);
			item->SetFloat("pitch", a.pitch);
			item->SetFloat("cooldown", a.cooldown);
		}
		root->AddSubKey(aliasKv);
	}

	if (!m_bannedPlayers.empty())
	{
		KeyValues* bansKv = new KeyValues("bans");
		for (const auto& b : m_bannedPlayers)
		{
			KeyValues* item = bansKv->CreateNewKey();
			item->SetName("ban");
			char idStr[32];
			snprintf(idStr, sizeof(idStr), "%llu", static_cast<unsigned long long>(b.steamID));
			item->SetString("steamid", idStr);
			item->SetString("name", b.playerName.c_str());
			item->SetString("reason", b.reason.c_str());
			item->SetString("date", b.banDate.c_str());
		}
		root->AddSubKey(bansKv);
	}

	KeyValues* settingsKv = new KeyValues("settings");
	settingsKv->SetInt("dot_stop", m_dotStopEnabled ? 1 : 0);
	settingsKv->SetInt("tts_random_style", m_ttsRandomStyle ? 1 : 0);
	settingsKv->SetString("tts_engine", m_ttsEngine.c_str());
	settingsKv->SetString("sapi_voice", m_sapiVoice.c_str());
	settingsKv->SetFloat("master_volume", m_masterVolume);
	root->AddSubKey(settingsKv);

	bool saved = false;

	// 1. Try IFileSystem with NULL pathID (resolves to mod write path)
	if (g_pFileSystem)
	{
		g_pFileSystem->CreateDirHierarchy("soundpad", NULL);
		saved = root->SaveToFile(g_pFileSystem, m_configFilePath.c_str(), NULL);
		if (!saved)
		{
			saved = root->SaveToFile(g_pFileSystem, "svencoop/soundpad.vdf", NULL);
		}
	}

	// 2. Fallback to std::ofstream using serialized buffer if filesystem failed
	if (!saved)
	{
		CUtlBuffer buf(0, 0, CUtlBuffer::TEXT_BUFFER);
		root->RecursiveSaveToFile(buf, 0);
		if (buf.TellPut() > 0)
		{
			const char* candidatePaths[] = {
				"svencoop/soundpad.vdf",
				"soundpad.vdf"
			};
			for (const char* p : candidatePaths)
			{
				std::ofstream ofs(p, std::ios::binary);
				if (ofs.is_open())
				{
					ofs.write(reinterpret_cast<const char*>(buf.Base()), buf.TellPut());
					ofs.close();
					saved = true;
					break;
				}
			}
		}
	}

	if (saved)
	{
		if (verbose && gEngfuncs.Con_Printf)
			gEngfuncs.Con_Printf("[Soundpad] Saved sound library to 'soundpad.vdf'.\n");
	}
	else
	{
		if (gEngfuncs.Con_Printf)
			gEngfuncs.Con_Printf("[Soundpad] ERROR: Failed to save sound library to disk!\n");
	}
}

void CSoundpadConfig::Load()
{
	if (!EnsureKeyValuesSystem())
		return;

	KeyValuesPtr root(new KeyValues("Soundpad"));
	bool loaded = false;

	// 1. Try loading via g_pFileSystem
	if (g_pFileSystem)
	{
		const char* searchPaths[] = {
			m_configFilePath.c_str(),
			"svencoop/soundpad.vdf",
			"soundpad.vdf"
		};
		for (const char* p : searchPaths)
		{
			if (root->LoadFromFile(g_pFileSystem, p, NULL) ||
			    root->LoadFromFile(g_pFileSystem, p, "GAME"))
			{
				loaded = true;
				break;
			}
		}
	}

	// 2. Fallback to std::ifstream
	if (!loaded)
	{
		const char* fallbackPaths[] = {
			"svencoop/soundpad.vdf",
			"soundpad.vdf"
		};
		for (const char* p : fallbackPaths)
		{
			std::ifstream ifs(p, std::ios::binary | std::ios::ate);
			if (ifs.is_open())
			{
				std::streamsize size = ifs.tellg();
				ifs.seekg(0, std::ios::beg);
				if (size > 0)
				{
					std::vector<char> buf(size + 1, 0);
					if (ifs.read(buf.data(), size))
					{
						if (root->LoadFromBuffer("soundpad.vdf", buf.data(), g_pFileSystem))
						{
							loaded = true;
							break;
						}
					}
				}
			}
		}
	}

	if (!loaded)
	{
		PopulateDefaults();
		Save();
		return;
	}

	m_masterVolume = root->GetFloat("master_volume", 1.0f);
	m_isMuted = (root->GetInt("muted", 0) != 0);
	m_ttsRandomStyle = (root->GetInt("tts_random_style", 0) != 0);
	m_ttsEngine = root->GetString("tts_engine", "klatter");
	m_sapiVoice = root->GetString("sapi_voice", "");

	m_categories.clear();
	KeyValues* catsKv = root->FindKey("categories");
	if (catsKv)
	{
		for (KeyValues* k = catsKv->GetFirstSubKey(); k; k = k->GetNextKey())
			m_categories.emplace_back(LoadCategoryFromKeyValues(k));
	}

	m_reactions.clear();
	KeyValues* reactKv = root->FindKey("reactions");
	if (reactKv)
	{
		for (KeyValues* k = reactKv->GetFirstSubKey(); k; k = k->GetNextKey())
		{
			ReactionItem r;
			r.id = k->GetString("id", "");
			r.soundPath = k->GetString("sound", "");
			r.eventType = static_cast<ReactionEventType>(k->GetInt("event", 0));
			r.condition = k->GetString("condition", "");
			r.chance = k->GetFloat("chance", 100.0f);
			r.cooldown = k->GetFloat("cooldown", 2.0f);
			r.volume = k->GetFloat("volume", 1.0f);
			r.pitch = k->GetFloat("pitch", 1.0f);
			r.fadeInSec = k->GetFloat("fadein", 0.0f);
			r.fadeOutSec = k->GetFloat("fadeout", 0.0f);
			r.fadeCurve = k->GetInt("fadecurve", 2);
			r.fxReverb = (k->GetInt("reverb", 0) != 0);
			r.fxZigzag = (k->GetInt("zigzag", 0) != 0);
			if (r.eventType == ReactionEventType::Chat)
			{
				SoundItem* s = FindSoundByPath(r.soundPath);
				if (s && s->chatTrigger.empty() && !r.condition.empty())
					s->chatTrigger = r.condition;
				continue; // Migrated to direct chat trigger on sound; skip adding as reaction
			}
			if (!r.soundPath.empty())
				m_reactions.push_back(std::move(r));
		}
	}

	m_sequences.clear();
	KeyValues* seqsKv = root->FindKey("sequences");
	if (seqsKv)
	{
		for (KeyValues* k = seqsKv->GetFirstSubKey(); k; k = k->GetNextKey())
		{
			SequenceItem seq;
			seq.name = k->GetString("name", "Untitled Macro");
			seq.hotkey = k->GetString("hotkey", "");
			KeyValues* stepsKv = k->FindKey("steps");
			if (stepsKv)
			{
				for (KeyValues* sk = stepsKv->GetFirstSubKey(); sk; sk = sk->GetNextKey())
				{
					SequenceStep step;
					step.soundPath = sk->GetString("sound", "");
					step.startSec = sk->GetFloat("start", 0.0f);
					step.endSec = sk->GetFloat("end", 0.0f);
					step.repeats = (std::max)(1, sk->GetInt("repeats", 1));
					step.pauseAfterSec = sk->GetFloat("pause", 0.0f);
					step.volume = sk->GetFloat("volume", 1.0f);
					step.pitch = sk->GetFloat("pitch", 1.0f);
					step.pitchStep = sk->GetFloat("pitchstep", 0.0f);
					step.fadeInSec = sk->GetFloat("fadein", 0.0f);
					step.fadeOutSec = sk->GetFloat("fadeout", 0.0f);
					step.fadeCurve = sk->GetInt("fadecurve", 2);
					step.fxReverb = (sk->GetInt("reverb", 0) != 0);
					step.fxZigzag = (sk->GetInt("zigzag", 0) != 0);
					if (!step.soundPath.empty())
						seq.steps.push_back(std::move(step));
				}
			}
			m_sequences.push_back(std::move(seq));
		}
	}

	KeyValues* settingsKv = root->FindKey("settings");
	if (settingsKv)
	{
		m_dotStopEnabled = (settingsKv->GetInt("dot_stop", 1) != 0);
		m_ttsRandomStyle = (settingsKv->GetInt("tts_random_style", 0) != 0);
		m_ttsEngine = settingsKv->GetString("tts_engine", m_ttsEngine.c_str());
		m_sapiVoice = settingsKv->GetString("sapi_voice", m_sapiVoice.c_str());
		m_masterVolume = settingsKv->GetFloat("master_volume", 1.0f);
	}

	m_chatAliases.clear();
	KeyValues* aliasKv = root->FindKey("aliases");
	if (aliasKv)
	{
		for (KeyValues* ak = aliasKv->GetFirstSubKey(); ak != nullptr; ak = ak->GetNextKey())
		{
			ChatAliasItem item;
			item.command = ak->GetString("cmd", "");
			item.soundPath = ak->GetString("sound", "");
			item.volume = ak->GetFloat("volume", 1.0f);
			item.pitch = ak->GetFloat("pitch", 1.0f);
			item.cooldown = ak->GetFloat("cooldown", 2.0f);
			if (!item.command.empty() && !item.soundPath.empty())
			{
				SoundItem* s = FindSoundByPath(item.soundPath);
				if (s && s->chatTrigger.empty())
					s->chatTrigger = item.command;
				m_chatAliases.push_back(std::move(item));
			}
		}
	}

	m_bannedPlayers.clear();
	KeyValues* bansKv = root->FindKey("bans");
	if (bansKv)
	{
		for (KeyValues* bk = bansKv->GetFirstSubKey(); bk != nullptr; bk = bk->GetNextKey())
		{
			BannedPlayerItem b;
			const char* sId = bk->GetString("steamid", "0");
			b.steamID = std::strtoull(sId, nullptr, 10);
			b.playerName = bk->GetString("name", "Unknown");
			b.reason = bk->GetString("reason", "");
			b.banDate = bk->GetString("date", "");
			if (b.steamID != 0 || (!b.playerName.empty() && b.playerName != "Unknown"))
			{
				m_bannedPlayers.push_back(std::move(b));
			}
		}
	}

	root.reset();

	if (m_categories.empty())
	{
		PopulateDefaults();
		Save();
	}
	else
	{
		if (m_reactions.empty())
		{
			ReactionItem r1;
			r1.id = "death_generic";
			r1.soundPath = "debris/debris7.wav";
			r1.condition = "GENERIC";
			r1.chance = 39.0f;
			r1.cooldown = 3.0f;
			r1.volume = 1.0f;
			m_reactions.push_back(r1);

			ReactionItem r2;
			r2.id = "death_poison";
			r2.soundPath = "scientist/nooo.wav";
			r2.condition = "POISON";
			r2.chance = 45.0f;
			r2.cooldown = 3.0f;
			r2.volume = 1.0f;
			m_reactions.push_back(r2);

			ReactionItem r3;
			r3.id = "kill_monster";
			r3.soundPath = "barney/ba_bring.wav";
			r3.eventType = ReactionEventType::Kill;
			r3.condition = "Monster";
			r3.chance = 20.0f;
			r3.cooldown = 4.0f;
			r3.volume = 1.0f;
			m_reactions.push_back(r3);
		}

		if (m_sequences.empty())
		{
			SequenceItem seq;
			seq.name = "Security Stutter";
			seq.hotkey = "KP_5";

			SequenceStep step1;
			step1.soundPath = "barney/c2a2_ba_launch.wav"; //-V691
			step1.startSec = 0.0f;
			step1.endSec = 0.24f;
			step1.repeats = 4;
			step1.pauseAfterSec = 0.02f;
			step1.volume = 1.0f;
			step1.pitch = 1.0f;
			step1.pitchStep = 0.03f;
			seq.steps.push_back(step1);

			SequenceStep step2;
			step2.soundPath = "barney/c2a2_ba_launch.wav"; //-V691
			step2.startSec = 0.0f;
			step2.endSec = 2.0f;
			step2.volume = 1.0f;
			step2.pitch = 1.12f;
			seq.steps.push_back(step2);

			m_sequences.push_back(seq);
		}

		if (m_chatAliases.empty())
		{
			ChatAliasItem a1;
			a1.command = "noo";
			a1.soundPath = "scientist/nooo.wav";
			a1.volume = 1.0f;
			a1.cooldown = 2.0f;
			m_chatAliases.push_back(a1);

			ChatAliasItem a2;
			a2.command = "kick";
			a2.soundPath = "hgrunt/kick!.wav";
			a2.volume = 1.0f;
			a2.cooldown = 2.0f;
			m_chatAliases.push_back(a2);

			ChatAliasItem a3;
			a3.command = "scream";
			a3.soundPath = "scientist/scream01.wav";
			a3.volume = 1.0f;
			a3.cooldown = 2.0f;
			m_chatAliases.push_back(a3);
		}

		if (gEngfuncs.Con_Printf)
		{
			SP_DEBUG_LOG("[Soundpad] Loaded %u category group(s), %u reaction(s), %u sequence(s), %u alias(es), %u ban(s) from soundpad.vdf.\n",
				static_cast<unsigned>(m_categories.size()), static_cast<unsigned>(m_reactions.size()),
				static_cast<unsigned>(m_sequences.size()), static_cast<unsigned>(m_chatAliases.size()),
				static_cast<unsigned>(m_bannedPlayers.size()));
		}
	}
}

static void CollectAllSounds(const CategoryNode& cat, std::vector<SoundItem>& out)
{
	for (const auto& s : cat.sounds)
		out.push_back(s);
	for (const auto& sub : cat.subcategories)
		CollectAllSounds(sub, out);
}

static const CategoryNode* FindCategoryNode(const std::vector<CategoryNode>& list, const std::string& name)
{
	for (const auto& c : list)
	{
		if (_stricmp(c.name.c_str(), name.c_str()) == 0)
			return &c;
		const CategoryNode* sub = FindCategoryNode(c.subcategories, name);
		if (sub)
			return sub;
	}
	return nullptr;
}

std::vector<SoundItem> CSoundpadConfig::GetSoundsForCategory(const std::string& categoryPath) const
{
	std::vector<SoundItem> res;
	if (categoryPath.empty() || _stricmp(categoryPath.c_str(), "All") == 0)
	{
		for (const auto& c : m_categories)
			CollectAllSounds(c, res);
		return res;
	}

	const CategoryNode* node = FindCategoryNode(m_categories, categoryPath);
	if (node)
		CollectAllSounds(*node, res);
	return res;
}

bool CSoundpadConfig::GetRandomSound(const std::string& categoryPath, SoundItem& outSound) const
{
	std::vector<SoundItem> list = GetSoundsForCategory(categoryPath);
	if (list.empty())
		return false;

	static std::mt19937 rng(std::random_device{}());
	std::uniform_int_distribution<size_t> dist(0, list.size() - 1);
	outSound = list[dist(rng)];
	return true;
}

bool CSoundpadConfig::GetRandomSoundGlobal(SoundItem& outSound) const
{
	return GetRandomSound("", outSound);
}

bool CSoundpadConfig::GetNextSound(const std::string& categoryPath, const std::string& currentPath, SoundItem& outSound) const
{
	std::vector<SoundItem> list = GetSoundsForCategory(categoryPath);
	if (list.empty())
		return false;

	for (size_t i = 0; i < list.size(); ++i)
	{
		if (_stricmp(list[i].path.c_str(), currentPath.c_str()) == 0)
		{
			outSound = list[(i + 1) % list.size()];
			return true;
		}
	}
	outSound = list[0];
	return true;
}

bool CSoundpadConfig::GetPrevSound(const std::string& categoryPath, const std::string& currentPath, SoundItem& outSound) const
{
	std::vector<SoundItem> list = GetSoundsForCategory(categoryPath);
	if (list.empty())
		return false;

	for (size_t i = 0; i < list.size(); ++i)
	{
		if (_stricmp(list[i].path.c_str(), currentPath.c_str()) == 0)
		{
			outSound = list[(i + list.size() - 1) % list.size()];
			return true;
		}
	}
	outSound = list.back();
	return true;
}

static CategoryNode* FindCategoryNodeMutable(std::vector<CategoryNode>& list, const std::string& name)
{
	for (auto& c : list)
	{
		if (_stricmp(c.name.c_str(), name.c_str()) == 0)
			return &c;
		CategoryNode* sub = FindCategoryNodeMutable(c.subcategories, name);
		if (sub)
			return sub;
	}
	return nullptr;
}

bool CSoundpadConfig::AddCategory(const std::string& categoryName, const std::string& parentCategory)
{
	if (categoryName.empty())
		return false;

	if (!parentCategory.empty() && _stricmp(parentCategory.c_str(), "All") != 0)
	{
		CategoryNode* parent = FindCategoryNodeMutable(m_categories, parentCategory);
		if (parent)
		{
			for (const auto& sub : parent->subcategories)
			{
				if (_stricmp(sub.name.c_str(), categoryName.c_str()) == 0)
					return false;
			}
			CategoryNode newCat;
			newCat.name = categoryName;
			parent->subcategories.push_back(std::move(newCat));
			Save();
			return true;
		}
	}

	for (const auto& c : m_categories)
	{
		if (_stricmp(c.name.c_str(), categoryName.c_str()) == 0)
			return false;
	}

	CategoryNode newCat;
	newCat.name = categoryName;
	m_categories.push_back(std::move(newCat));
	Save();
	return true;
}

void CSoundpadConfig::AddSoundToCategory(const std::string& categoryPath, const SoundItem& item, int insertIndex)
{
	std::string targetCat = (categoryPath.empty() || _stricmp(categoryPath.c_str(), "All") == 0) ? "Custom" : categoryPath;

	CategoryNode* node = FindCategoryNodeMutable(m_categories, targetCat);
	if (!node)
	{
		AddCategory(targetCat);
		node = FindCategoryNodeMutable(m_categories, targetCat);
	}

	if (node)
	{
		for (auto it = node->sounds.begin(); it != node->sounds.end(); ++it)
		{
			if (_stricmp(it->path.c_str(), item.path.c_str()) == 0)
			{
				it->title = item.title;
				it->volume = item.volume;
				it->pitch = item.pitch;
				it->isSequence = item.isSequence;
				Save();
				return;
			}
		}
		if (insertIndex >= 0 && insertIndex <= static_cast<int>(node->sounds.size()))
		{
			node->sounds.insert(node->sounds.begin() + insertIndex, item);
		}
		else
		{
			node->sounds.push_back(item);
		}
		Save();
	}
	else if (!m_categories.empty())
	{
		auto& firstSounds = m_categories[0].sounds;
		if (insertIndex >= 0 && insertIndex <= static_cast<int>(firstSounds.size()))
		{
			firstSounds.insert(firstSounds.begin() + insertIndex, item);
		}
		else
		{
			firstSounds.push_back(item);
		}
		Save();
	}
}

const SequenceItem* CSoundpadConfig::FindSequence(const std::string& name) const
{
	for (const auto& seq : m_sequences)
	{
		if (_stricmp(seq.name.c_str(), name.c_str()) == 0)
			return &seq;
	}
	return nullptr;
}

static std::string NormalizeSoundPath(const std::string& input)
{
	if (input.empty()) return "";
	size_t start = input.find_first_not_of(" \"'");
	if (start == std::string::npos) return "";
	size_t end = input.find_last_not_of(" \"'");
	std::string s = input.substr(start, end - start + 1);
	for (char& c : s)
	{
		if (c == '\\') c = '/';
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	if (s.rfind("sound/", 0) == 0)
		s = s.substr(6);
	return s;
}

static bool PathsEqual(const std::string& a, const std::string& b)
{
	return NormalizeSoundPath(a) == NormalizeSoundPath(b);
}

static bool RemoveSoundRecursive(CategoryNode& node, const std::string& path)
{
	auto it = std::remove_if(node.sounds.begin(), node.sounds.end(), [&](const SoundItem& s) {
		if (PathsEqual(s.path, path))
			return true;
		if (_stricmp(s.title.c_str(), path.c_str()) == 0)
			return true;
		return false;
	});
	if (it != node.sounds.end())
	{
		node.sounds.erase(it, node.sounds.end());
		return true;
	}
	for (auto& sub : node.subcategories)
	{
		if (RemoveSoundRecursive(sub, path))
			return true;
	}
	return false;
}

void CSoundpadConfig::RemoveSoundFromCategory(const std::string& categoryPath, const std::string& soundPath)
{
	bool removed = false;
	if (!categoryPath.empty() && _stricmp(categoryPath.c_str(), "All") != 0)
	{
		CategoryNode* node = FindCategoryNodeMutable(m_categories, categoryPath);
		if (node)
		{
			removed = RemoveSoundRecursive(*node, soundPath);
		}
	}

	if (!removed)
	{
		for (auto& c : m_categories)
		{
			if (RemoveSoundRecursive(c, soundPath))
			{
				removed = true;
				break;
			}
		}
	}

	if (removed)
	{
		Save();
	}
}

// Extracts (and removes) the first matching sound from a category subtree.
static bool ExtractSoundRecursive(CategoryNode& node, const std::string& path, SoundItem& out)
{
	for (auto it = node.sounds.begin(); it != node.sounds.end(); ++it)
	{
		if (PathsEqual(it->path, path) || _stricmp(it->title.c_str(), path.c_str()) == 0)
		{
			out = *it;
			node.sounds.erase(it);
			return true;
		}
	}
	for (auto& sub : node.subcategories)
	{
		if (ExtractSoundRecursive(sub, path, out))
			return true;
	}
	return false;
}

bool CSoundpadConfig::MoveSoundToCategory(const std::string& fromCategory, const std::string& toCategory,
	const std::string& soundPath)
{
	if (soundPath.empty())
		return false;
	return MoveSoundsToCategory(fromCategory, toCategory, { soundPath }) > 0;
}

size_t CSoundpadConfig::MoveSoundsToCategory(const std::string& fromCategory, const std::string& toCategory,
	const std::vector<std::string>& soundPaths)
{
	if (fromCategory.empty() || toCategory.empty() || soundPaths.empty())
		return 0;
	if (_stricmp(fromCategory.c_str(), toCategory.c_str()) == 0)
		return 0;

	// Resolve (or create) the destination first: appending a root category can
	// reallocate m_categories, so the source pointer must be taken afterwards.
	const std::string targetName =
		(_stricmp(toCategory.c_str(), "All") == 0) ? "Custom" : toCategory;
	CategoryNode* target = FindCategoryNodeMutable(m_categories, targetName);
	if (!target)
	{
		CategoryNode newCat;
		newCat.name = targetName;
		m_categories.push_back(std::move(newCat));
		target = &m_categories.back();
	}

	CategoryNode* source = FindCategoryNodeMutable(m_categories, fromCategory);
	if (!source || source == target)
		return 0;

	size_t moved = 0;
	for (const auto& soundPath : soundPaths)
	{
		SoundItem item;
		if (!ExtractSoundRecursive(*source, soundPath, item))
			continue;

		// AddSoundToCategory would save on every item; update/append inline and
		// persist once so a large multi-selection does not stall the UI thread.
		bool updated = false;
		for (auto& existing : target->sounds)
		{
			if (_stricmp(existing.path.c_str(), item.path.c_str()) == 0)
			{
				existing.title = item.title;
				existing.volume = item.volume;
				existing.pitch = item.pitch;
				existing.isSequence = item.isSequence;
				updated = true;
				break;
			}
		}
		if (!updated)
			target->sounds.push_back(std::move(item));
		++moved;
	}

	if (moved > 0)
		Save();
	return moved;
}

static bool UpdateSoundInNode(CategoryNode& node, const std::string& soundPath, float volume, float pitch)
{
	for (auto& s : node.sounds)
	{
		if (PathsEqual(s.path, soundPath))
		{
			if (std::abs(s.volume - volume) < 0.001f &&
				std::abs(s.pitch - pitch) < 0.001f)
			{
				return false;
			}
			s.volume = volume;
			s.pitch = pitch;
			return true;
		}
	}
	for (auto& sub : node.subcategories)
	{
		if (UpdateSoundInNode(sub, soundPath, volume, pitch))
			return true;
	}
	return false;
}

bool CSoundpadConfig::UpdateSoundSettings(const std::string& soundPath, float volume, float pitch)
{
	for (auto& c : m_categories)
	{
		if (UpdateSoundInNode(c, soundPath, volume, pitch))
		{
			Save(false);
			return true;
		}
	}
	return false;
}

void CSoundpadConfig::Reload()
{
	Load();
}

void CSoundpadConfig::AddReaction(const ReactionItem& item)
{
	m_reactions.push_back(item);
	Save(false);
}

void CSoundpadConfig::RemoveReaction(size_t index)
{
	if (index < m_reactions.size())
	{
		m_reactions.erase(m_reactions.begin() + index);
		Save(false);
	}
}

bool CSoundpadConfig::UpdateReaction(size_t index, const ReactionItem& item)
{
	if (index < m_reactions.size())
	{
		m_reactions[index] = item;
		Save(false);
		return true;
	}
	return false;
}

void CSoundpadConfig::AddSequence(const SequenceItem& item)
{
	m_sequences.push_back(item);
	Save(false);
}

void CSoundpadConfig::RemoveSequence(size_t index)
{
	if (index < m_sequences.size())
	{
		m_sequences.erase(m_sequences.begin() + index);
		Save(false);
	}
}

bool CSoundpadConfig::UpdateSequence(size_t index, const SequenceItem& item)
{
	if (index < m_sequences.size())
	{
		m_sequences[index] = item;
		Save(false);
		return true;
	}
	return false;
}

void CSoundpadConfig::SetDotStopEnabled(bool enabled)
{
	m_dotStopEnabled = enabled;
	Save(false);
}

void CSoundpadConfig::AddChatAlias(const ChatAliasItem& item)
{
	m_chatAliases.push_back(item);
	Save(false);
}

void CSoundpadConfig::RemoveChatAlias(size_t index)
{
	if (index < m_chatAliases.size())
	{
		m_chatAliases.erase(m_chatAliases.begin() + index);
		Save(false);
	}
}

bool CSoundpadConfig::UpdateChatAlias(size_t index, const ChatAliasItem& item)
{
	if (index < m_chatAliases.size())
	{
		m_chatAliases[index] = item;
		Save(false);
		return true;
	}
	return false;
}

static std::string CleanAliasCommand(const std::string& input)
{
	std::string s = input;
	while (!s.empty() && (s[0] == '!' || s[0] == '/' || s[0] == '.' || s[0] == ' ' || s[0] == '\t'))
		s.erase(0, 1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
		s.pop_back();
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

static SoundItem* FindSoundByPathRecursive(std::vector<CategoryNode>& cats, const std::string& path)
{
	for (auto& cat : cats)
	{
		for (auto& s : cat.sounds)
		{
			if (_stricmp(s.path.c_str(), path.c_str()) == 0)
				return &s;
		}
		SoundItem* found = FindSoundByPathRecursive(cat.subcategories, path);
		if (found)
			return found;
	}
	return nullptr;
}

SoundItem* CSoundpadConfig::FindSoundByPath(const std::string& soundPath)
{
	return FindSoundByPathRecursive(m_categories, soundPath);
}

static bool TriggerMatches(const std::string& field, const std::string& cleanTrigger)
{
	if (field.empty())
		return false;

	// Support comma/semicolon-separated list of triggers per sound (e.g. "!laugh, laugh, haha")
	size_t start = 0;
	while (start < field.size())
	{
		size_t sep = field.find_first_of(",;", start);
		std::string single = (sep == std::string::npos) ? field.substr(start) : field.substr(start, sep - start);
		if (CleanAliasCommand(single) == cleanTrigger)
			return true;
		if (sep == std::string::npos) break;
		start = sep + 1;
	}
	return false;
}

static void CollectSoundsByTriggerRecursive(std::vector<CategoryNode>& cats, const std::string& cleanTrigger, std::vector<SoundItem*>& matches)
{
	for (auto& cat : cats)
	{
		for (auto& s : cat.sounds)
		{
			if (TriggerMatches(s.chatTrigger, cleanTrigger))
				matches.push_back(&s);
		}
		CollectSoundsByTriggerRecursive(cat.subcategories, cleanTrigger, matches);
	}
}

SoundItem* CSoundpadConfig::FindSoundByChatTrigger(const std::string& trigger)
{
	std::string clean = CleanAliasCommand(trigger);
	if (clean.empty())
		return nullptr;

	std::vector<SoundItem*> matches;
	CollectSoundsByTriggerRecursive(m_categories, clean, matches);
	if (matches.empty())
		return nullptr;

	if (matches.size() == 1)
		return matches[0];

	// Random selection among all candidate sounds sharing this trigger
	static std::mt19937 rng(std::random_device{}());
	std::uniform_int_distribution<size_t> dist(0, matches.size() - 1);
	return matches[dist(rng)];
}

bool CSoundpadConfig::SetSoundChatTrigger(const std::string& soundPath, const std::string& trigger)
{
	SoundItem* s = FindSoundByPath(soundPath);
	if (!s)
		return false;
	s->chatTrigger = trigger;
	Save(false);
	return true;
}

bool CSoundpadConfig::SetSoundHotkey(const std::string& soundPath, const std::string& hotkey)
{
	SoundItem* s = FindSoundByPath(soundPath);
	if (!s)
		return false;
	s->hotkey = hotkey;
	Save(false);
	return true;
}

ChatAliasItem* CSoundpadConfig::FindChatAlias(const std::string& command)
{
	std::string clean = CleanAliasCommand(command);
	if (clean.empty())
		return nullptr;

	std::vector<ChatAliasItem*> matches;
	for (auto& a : m_chatAliases)
	{
		if (TriggerMatches(a.command, clean))
			matches.push_back(&a);
	}

	if (matches.empty())
		return nullptr;

	if (matches.size() == 1)
		return matches[0];

	static std::mt19937 rng(std::random_device{}());
	std::uniform_int_distribution<size_t> dist(0, matches.size() - 1);
	return matches[dist(rng)];
}

bool CSoundpadConfig::IsPlayerBanned(uint64_t steamID, const std::string& name) const
{
	for (const auto& b : m_bannedPlayers)
	{
		if (steamID != 0 && b.steamID != 0 && b.steamID == steamID)
			return true;

		if (!name.empty() && !b.playerName.empty() && _stricmp(b.playerName.c_str(), name.c_str()) == 0)
			return true;
	}
	return false;
}

void CSoundpadConfig::BanPlayer(uint64_t steamID, const std::string& name, const std::string& reason)
{
	if (steamID == 0 && name.empty())
		return;

	for (auto& b : m_bannedPlayers)
	{
		bool match = false;
		if (steamID != 0 && b.steamID != 0 && b.steamID == steamID)
			match = true;
		else if (steamID == 0 && !name.empty() && !b.playerName.empty() && _stricmp(b.playerName.c_str(), name.c_str()) == 0)
			match = true;

		if (match)
		{
			if (!name.empty()) b.playerName = name;
			if (!reason.empty()) b.reason = reason;
			Save(false);
			return;
		}
	}

	BannedPlayerItem item;
	item.steamID = steamID;
	item.playerName = name.empty() ? "Player" : name;
	item.reason = reason.empty() ? "Manual ban" : reason;

	time_t t = time(nullptr);
	tm tmBuf{};
#if defined(_WIN32)
	localtime_s(&tmBuf, &t);
#else
	localtime_r(&t, &tmBuf);
#endif
	char dateBuf[64] = {};
	strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M", &tmBuf);
	item.banDate = dateBuf;

	m_bannedPlayers.push_back(std::move(item));
	Save(false);
}

bool CSoundpadConfig::UnbanPlayer(uint64_t steamID)
{
	if (steamID == 0)
		return false;

	auto it = std::remove_if(m_bannedPlayers.begin(), m_bannedPlayers.end(),
		[steamID](const BannedPlayerItem& b) { return b.steamID != 0 && b.steamID == steamID; });

	if (it != m_bannedPlayers.end())
	{
		m_bannedPlayers.erase(it, m_bannedPlayers.end());
		Save(false);
		return true;
	}
	return false;
}

bool CSoundpadConfig::UnbanPlayerByName(const std::string& name)
{
	if (name.empty())
		return false;

	auto it = std::remove_if(m_bannedPlayers.begin(), m_bannedPlayers.end(),
		[&name](const BannedPlayerItem& b) { return !b.playerName.empty() && _stricmp(b.playerName.c_str(), name.c_str()) == 0; });

	if (it != m_bannedPlayers.end())
	{
		m_bannedPlayers.erase(it, m_bannedPlayers.end());
		Save(false);
		return true;
	}
	return false;
}
