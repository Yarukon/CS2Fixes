/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2025 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "adminsystem.h"
#include "ctimer.h"
#include "entity/ccsplayercontroller.h"
#include "entity/ccsplayerpawn.h"
#include "eventlistener.h"
#include "gamesystem.h"
#include "vendor/nlohmann/json_fwd.hpp"

using ordered_json = nlohmann::ordered_json;

#define ZR_PREFIX " \7[生化]\1 "
#define HUMAN_CLASS_KEY_NAME "zr_human_class"
#define ZOMBIE_CLASS_KEY_NAME "zr_zombie_class"

enum class EZRRoundState
{
	ROUND_START,
	POST_INFECTION,
	ROUND_END,
};

enum EZRSpawnType
{
	IN_PLACE,
	RESPAWN,
};

// model entries in zr classes
struct ZRModelEntry
{
	std::string szModelPath;
	CUtlVector<int> vecSkins;
	std::string szColor;
	ZRModelEntry(std::shared_ptr<ZRModelEntry> modelEntry);
	ZRModelEntry(ordered_json jsonModelEntry);
	int GetRandomSkin()
	{
		return vecSkins[rand() % vecSkins.Count()];
	}
};

// everything that human and zombie share
struct ZRClass
{
	int iTeam;
	bool bEnabled;
	std::string szClassName;
	int iHealth;
	CUtlVector<std::shared_ptr<ZRModelEntry>> vecModels;
	float flScale;
	float flSpeed;
	float flGravity;
	uint64 iAdminFlag;
	ZRClass(std::shared_ptr<ZRClass> pClass, int iTeam) :
		iTeam(iTeam),
		bEnabled(pClass->bEnabled),
		szClassName(pClass->szClassName),
		iHealth(pClass->iHealth),
		flScale(pClass->flScale),
		flSpeed(pClass->flSpeed),
		flGravity(pClass->flGravity),
		iAdminFlag(pClass->iAdminFlag)
	{
		vecModels.Purge();
		FOR_EACH_VEC(pClass->vecModels, i)
		{
			std::shared_ptr<ZRModelEntry> modelEntry = std::make_shared<ZRModelEntry>(pClass->vecModels[i]);
			vecModels.AddToTail(modelEntry);
		}
	};

	ZRClass(ordered_json jsonKeys, std::string szClassname, int iTeam);
	void PrintInfo()
	{
		std::string szModels = "";
		FOR_EACH_VEC(vecModels, i)
		{
			szModels += "\n\t\t" + vecModels[i]->szModelPath;
			szModels += " Color=\"" + vecModels[i]->szColor + "\"";
			szModels += " Skins=[";
			FOR_EACH_VEC(vecModels[i]->vecSkins, j)
			{
				szModels += std::to_string(vecModels[i]->vecSkins[j]);
				if (j != vecModels[i]->vecSkins.Count() - 1)
					szModels += " ";
			}
			szModels += "]";
		}
		Message(
			"%s:\n"
			"\tenabled: %d\n"
			"\thealth: %d\n"
			"\tmodels: %s\n"
			"\tscale: %f\n"
			"\tspeed: %f\n"
			"\tgravity: %f\n"
			"\tadmin flag: %llu\n",
			szClassName.c_str(),
			bEnabled,
			iHealth,
			szModels.c_str(),
			flScale,
			flSpeed,
			flGravity,
			iAdminFlag);
	};
	void Override(ordered_json jsonKeys, std::string szClassname);
	bool IsApplicableTo(CCSPlayerController* pController);
	uint64 ParseClassFlags(const char* pszFlags);
	std::shared_ptr<ZRModelEntry> GetRandomModelEntry()
	{
		return vecModels[rand() % vecModels.Count()];
	};
};

struct ZRHumanClass : ZRClass
{
	ZRHumanClass(std::shared_ptr<ZRHumanClass> pClass) :
		ZRClass(pClass, CS_TEAM_CT) {};
	ZRHumanClass(ordered_json jsonKeys, std::string szClassname);
};

struct ZRZombieClass : ZRClass
{
	int iHealthRegenCount;
	float flHealthRegenInterval;
	float flKnockback;
	ZRZombieClass(std::shared_ptr<ZRZombieClass> pClass) :
		ZRClass(pClass, CS_TEAM_T),
		iHealthRegenCount(pClass->iHealthRegenCount),
		flHealthRegenInterval(pClass->flHealthRegenInterval),
		flKnockback(pClass->flKnockback) {};
	ZRZombieClass(ordered_json jsonKeys, std::string szClassname);
	void PrintInfo()
	{
		std::string szModels = "";
		FOR_EACH_VEC(vecModels, i)
		{
			szModels += "\n\t\t" + vecModels[i]->szModelPath;
			szModels += " Color=\"" + vecModels[i]->szColor + "\"";
			szModels += " Skins=[";
			FOR_EACH_VEC(vecModels[i]->vecSkins, j)
			{
				szModels += std::to_string(vecModels[i]->vecSkins[j]);
				if (j != vecModels[i]->vecSkins.Count() - 1)
					szModels += " ";
			}
			szModels += "]";
		}
		Message(
			"%s:\n"
			"\tenabled: %d\n"
			"\thealth: %d\n"
			"\tmodels: %s\n"
			"\tscale: %f\n"
			"\tspeed: %f\n"
			"\tgravity: %f\n"
			"\tknockback: %f\n"
			"\tadmin flag: %d\n"
			"\thealth_regen_count: %d\n"
			"\thealth_regen_interval: %f\n",
			szClassName.c_str(),
			bEnabled,
			iHealth,
			szModels.c_str(),
			flScale,
			flSpeed,
			flGravity,
			flKnockback,
			iAdminFlag,
			iHealthRegenCount,
			flHealthRegenInterval);
	};
	void Override(ordered_json jsonKeys, std::string szClassname);
};

class CZRPlayerClassManager
{
public:
	CZRPlayerClassManager()
	{
		m_ZombieClassMap.SetLessFunc(DefLessFunc(uint32));
		m_HumanClassMap.SetLessFunc(DefLessFunc(uint32));
	};
	void LoadPlayerClass();
	void ApplyBaseClassVisuals(std::shared_ptr<ZRClass> pClass, CCSPlayerPawn* pPawn);
	std::shared_ptr<ZRHumanClass> GetHumanClass(const char* pszClassName);
	void ApplyHumanClass(std::shared_ptr<ZRHumanClass> pClass, CCSPlayerPawn* pPawn);
	void ApplyPreferredOrDefaultHumanClass(CCSPlayerPawn* pPawn);
	void ApplyPreferredOrDefaultHumanClassVisuals(CCSPlayerPawn* pPawn);
	std::shared_ptr<ZRZombieClass> GetZombieClass(const char* pszClassName);
	void ApplyZombieClass(std::shared_ptr<ZRZombieClass> pClass, CCSPlayerPawn* pPawn);
	void ApplyPreferredOrDefaultZombieClass(CCSPlayerPawn* pPawn);
	void PrecacheModels(IEntityResourceManifest* pResourceManifest);
	void GetZRClassList(int iTeam, CUtlVector<std::shared_ptr<ZRClass>>& vecClasses, CCSPlayerController* pController = nullptr);

private:
	void ApplyBaseClass(std::shared_ptr<ZRClass> pClass, CCSPlayerPawn* pPawn);
	CUtlVector<std::shared_ptr<ZRZombieClass>> m_vecZombieDefaultClass;
	CUtlVector<std::shared_ptr<ZRHumanClass>> m_vecHumanDefaultClass;
	CUtlMap<uint32, std::shared_ptr<ZRZombieClass>> m_ZombieClassMap;
	CUtlMap<uint32, std::shared_ptr<ZRHumanClass>> m_HumanClassMap;
};

class CZRRegenTimer : public CTimerBase
{
public:
	CZRRegenTimer(float flRegenInterval, int iRegenAmount, CHandle<CCSPlayerPawn> hPawnHandle) :
		CTimerBase(flRegenInterval, false, false), m_iRegenAmount(iRegenAmount), m_hPawnHandle(hPawnHandle) {};

	bool Execute();
	static void StartRegen(float flRegenInterval, int iRegenAmount, CCSPlayerController* pController);
	static void StopRegen(CCSPlayerController* pController);
	static int GetIndex(CPlayerSlot slot);
	static void Tick();
	static void RemoveAllTimers();

private:
	static double s_flNextExecution;
	static CZRRegenTimer* s_vecRegenTimers[MAXPLAYERS];
	int m_iRegenAmount;
	CHandle<CCSPlayerPawn> m_hPawnHandle;
};

struct ZRWeapon
{
	float flKnockback;
};

struct ZRHitgroup
{
	float flKnockback;
};

class ZRWeaponConfig
{
public:
	ZRWeaponConfig()
	{
		m_WeaponMap.SetLessFunc(DefLessFunc(uint32));
	};
	void LoadWeaponConfig();
	std::shared_ptr<ZRWeapon> FindWeapon(const char* pszWeaponName);

private:
	CUtlMap<uint32, std::shared_ptr<ZRWeapon>> m_WeaponMap;
};

class ZRHitgroupConfig
{
public:
	ZRHitgroupConfig()
	{
		m_HitgroupMap.SetLessFunc(DefLessFunc(uint32));
	};
	void LoadHitgroupConfig();
	std::shared_ptr<ZRHitgroup> FindHitgroupIndex(int iIndex);

private:
	CUtlMap<uint32, std::shared_ptr<ZRHitgroup>> m_HitgroupMap;
};

extern ZRWeaponConfig* g_pZRWeaponConfig;
extern ZRHitgroupConfig* g_pZRHitgroupConfig;
extern CZRPlayerClassManager* g_pZRPlayerClassManager;

extern CConVar<bool> g_cvarEnableZR;
extern EZRRoundState g_ZRRoundState;

void ZR_OnLevelInit();
void ZR_OnRoundPrestart(IGameEvent* pEvent);
void ZR_OnRoundStart(IGameEvent* pEvent);
void ZR_OnPlayerSpawn(CCSPlayerController* pController);
void ZR_OnPlayerTakeDamage(CCSPlayerPawn* pVictimPawn, const CTakeDamageInfo* pInfo, const int32 damage);
void ZR_OnPlayerDeath(IGameEvent* pEvent);
void ZR_OnRoundFreezeEnd(IGameEvent* pEvent);
void ZR_OnRoundTimeWarning(IGameEvent* pEvent);
bool ZR_Hook_OnTakeDamage_Alive(CTakeDamageInfo* pInfo, CCSPlayerPawn* pVictimPawn);
bool ZR_Detour_CCSPlayer_WeaponServices_CanUse(CCSPlayer_WeaponServices* pWeaponServices, CBasePlayerWeapon* pPlayerWeapon);
void ZR_Detour_CEntityIdentity_AcceptInput(CEntityIdentity* pThis, CUtlSymbolLarge* pInputName, CEntityInstance* pActivator, CEntityInstance* pCaller, variant_t* value, int nOutputID);
void ZR_Hook_ClientPutInServer(CPlayerSlot slot, char const* pszName, int type, uint64 xuid);
void ZR_Hook_ClientCommand_JoinTeam(CPlayerSlot slot, const CCommand& args);
void ZR_Precache(IEntityResourceManifest* pResourceManifest);