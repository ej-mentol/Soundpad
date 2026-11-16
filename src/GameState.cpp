#include "GameState.h"
#include <algorithm>
#include <cctype>

CGameState& CGameState::Get()
{
	static CGameState instance;
	return instance;
}

CGameState::CGameState()
{
	Reset();
}

static std::string CleanGameString(const std::string& input)
{
	std::string res;
	res.reserve(input.size());
	for (unsigned char c : input)
	{
		// Strip GoldSrc color codes: 0x01 (default), 0x02 (team), 0x03 (team), 0x04 (green), 0x07 (RGB)
		if (c == 0x01 || c == 0x02 || c == 0x03 || c == 0x04 || c == 0x07)
			continue;
		if (c < 0x20 && c != '\t')
			continue;
		res.push_back(static_cast<char>(c));
	}
	size_t start = res.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = res.find_last_not_of(" \t\r\n");
	return res.substr(start, end - start + 1);
}

void CGameState::Reset()
{
	m_health = 100;
	m_armor = 0;
	m_lastDamage = 0;
	m_lastDamageBits = 0;
	m_lastDamageType = "GENERIC";
	m_deaths = 0;
	m_kills = 0;
	m_lastKiller = "world";
	m_lastVictim.clear();
	m_mapName.clear();
	m_lastDeathSuicide = false;
	m_lastKillerPlayer = false;
	m_lastKillPlayer = false;
}

void CGameState::OnMapChange(const char* mapName)
{
	m_mapName = mapName ? CleanGameString(mapName) : "";
	m_health = 100;
	m_armor = 0;
	m_lastDamage = 0;
	m_lastDamageBits = 0;
	m_lastDamageType = "GENERIC";
	m_lastDeathSuicide = false;
	m_lastKillerPlayer = false;
	m_lastKillPlayer = false;
}

void CGameState::SetHealth(int hp)
{
	m_health = hp;
}

void CGameState::SetArmor(int ap)
{
	m_armor = ap;
}

void CGameState::RecordDamage(int damageTaken, uint32_t damageBits, const std::string& attacker)
{
	m_lastDamage = damageTaken;
	m_lastDamageBits = damageBits;

	// Resolve damage bit type to readable string
	if (damageBits & (1 << 5)) m_lastDamageType = "FALL";
	else if (damageBits & (1 << 4)) m_lastDamageType = "POISON";
	else if (damageBits & (1 << 3)) m_lastDamageType = "BURN";
	else if (damageBits & (1 << 6)) m_lastDamageType = "BLAST";
	else if (damageBits & (1 << 14)) m_lastDamageType = "DROWN";
	else if (damageBits & (1 << 8)) m_lastDamageType = "SHOCK";
	else if (damageBits & (1 << 15)) m_lastDamageType = "ACID";
	else if (damageBits & (1 << 2)) m_lastDamageType = "SLASH";
	else if (damageBits & (1 << 1)) m_lastDamageType = "BULLET";
	else if (damageBits & (1 << 0)) m_lastDamageType = "CRUSH";
	else if (damageBits & (1 << 7)) m_lastDamageType = "CLUB";
	else if (damageBits & (1 << 9)) m_lastDamageType = "SONIC";
	else if (damageBits & (1 << 10)) m_lastDamageType = "ENERGY";
	else m_lastDamageType = "GENERIC";

	if (!attacker.empty())
	{
		std::string clean = CleanGameString(attacker);
		if (!clean.empty())
			m_lastKiller = clean;
	}
}

void CGameState::RecordDeath(const std::string& killer, bool isSuicide, bool isKillerPlayer)
{
	m_deaths++;
	m_health = 0;
	m_lastDeathSuicide = isSuicide;
	m_lastKillerPlayer = isKillerPlayer;

	std::string clean = CleanGameString(killer);
	if (!clean.empty())
		m_lastKiller = clean;
	else if (isSuicide)
		m_lastKiller = "suicide";
}

void CGameState::RecordKill(const std::string& victim, bool isVictimPlayer)
{
	m_kills++;
	m_lastKillPlayer = isVictimPlayer;

	std::string clean = CleanGameString(victim);
	if (!clean.empty())
		m_lastVictim = clean;
}

void CGameState::SetPlayerName(const std::string& name)
{
	std::string clean = CleanGameString(name);
	if (!clean.empty())
		m_playerName = clean;
}

std::string CGameState::FormatTokens(const std::string& input) const
{
	if (input.empty())
		return "";

	// Robust single-pass token substitution to strictly avoid recursive template injection
	std::string result;
	result.reserve(input.size() + 64);

	size_t i = 0;
	while (i < input.size())
	{
		if (input[i] == '%')
		{
			size_t nextPercent = input.find('%', i + 1);
			if (nextPercent != std::string::npos && nextPercent - i > 1)
			{
				std::string token = input.substr(i + 1, nextPercent - (i + 1));
				std::string tLower = token;
				std::transform(tLower.begin(), tLower.end(), tLower.begin(), ::tolower);

				bool replaced = true;
				if (tLower == "hp" || tLower == "health")
					result += std::to_string(m_health);
				else if (tLower == "armor" || tLower == "ap")
					result += std::to_string(m_armor);
				else if (tLower == "dmg" || tLower == "damage")
					result += std::to_string(m_lastDamage);
				else if (tLower == "dmgtype")
					result += m_lastDamageType;
				else if (tLower == "deaths" || tLower == "dc")
					result += std::to_string(m_deaths);
				else if (tLower == "kills")
					result += std::to_string(m_kills);
				else if (tLower == "killer")
					result += m_lastKiller;
				else if (tLower == "victim")
					result += m_lastVictim;
				else if (tLower == "map")
					result += m_mapName;
				else if (tLower == "name" || tLower == "player")
					result += m_playerName;
				else
					replaced = false;

				if (replaced)
				{
					i = nextPercent + 1;
					continue;
				}
			}
		}
		result += input[i++];
	}

	return result;
}
