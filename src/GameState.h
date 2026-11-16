#pragma once

#include <string>
#include <cstdint>

// Centralized runtime game state and statistics tracker.
// Completely decoupled from UI and pointers to pointers.
class CGameState
{
public:
	static CGameState& Get();

	void Reset();
	void OnMapChange(const char* mapName);

	// State setters
	void SetHealth(int hp);
	void SetArmor(int ap);
	void RecordDamage(int damageTaken, uint32_t damageBits, const std::string& attacker = "");
	void RecordDeath(const std::string& killer, bool isSuicide = false, bool isKillerPlayer = false);
	void RecordKill(const std::string& victim, bool isVictimPlayer = false);
	void SetPlayerName(const std::string& name);

	// State getters
	int GetHealth() const { return m_health; }
	int GetArmor() const { return m_armor; }
	int GetLastDamage() const { return m_lastDamage; }
	uint32_t GetLastDamageBits() const { return m_lastDamageBits; }
	const std::string& GetLastDamageType() const { return m_lastDamageType; }
	int GetDeaths() const { return m_deaths; }
	int GetKills() const { return m_kills; }
	const std::string& GetLastKiller() const { return m_lastKiller; }
	const std::string& GetLastVictim() const { return m_lastVictim; }
	const std::string& GetMapName() const { return m_mapName; }
	const std::string& GetPlayerName() const { return m_playerName; }
	bool IsLastDeathSuicide() const { return m_lastDeathSuicide; }
	bool WasLastKillerPlayer() const { return m_lastKillerPlayer; }
	bool WasLastKillPlayer() const { return m_lastKillPlayer; }

	// Template token formatter:
	// Replaces %hp%, %armor%, %dmg%, %damage%, %dmgtype%, %deaths%, %dc%, %kills%,
	// %killer%, %victim%, %map%, %name%, %player% with real values.
	std::string FormatTokens(const std::string& input) const;

private:
	CGameState();

	int m_health = 100;
	int m_armor = 0;
	int m_lastDamage = 0;
	uint32_t m_lastDamageBits = 0;
	std::string m_lastDamageType = "GENERIC";
	int m_deaths = 0;
	int m_kills = 0;
	std::string m_lastKiller = "world";
	std::string m_lastVictim = "";
	std::string m_mapName = "";
	std::string m_playerName = "Player";
	bool m_lastDeathSuicide = false;
	bool m_lastKillerPlayer = false;
	bool m_lastKillPlayer = false;
};
