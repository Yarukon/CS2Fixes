/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023 Source2ZE
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

#include "protobuf/generated/usermessages.pb.h"

#include "adminsystem.h"
#include "KeyValues.h"
#include "interfaces/interfaces.h"
#include "icvar.h"
#include "playermanager.h"
#include "commands.h"
#include "ctimer.h"
#include "detours.h"
#include "discord.h"
#include "utils/entity.h"
#include "entity/cgamerules.h"

extern IVEngineServer2 *g_pEngineServer2;
extern CEntitySystem *g_pEntitySystem;
extern CGlobalVars *gpGlobals;
extern CCSGameRules *g_pGameRules;

CAdminSystem* g_pAdminSystem = nullptr;

CUtlMap<uint32, CChatCommand *> g_CommandList(0, 0, DefLessFunc(uint32));

void PrintSingleAdminAction(const char *pszAdminName, const char *pszTargetName, const char *pszAction, const char *pszAction2 = "")
{
	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "%s %s%s.", pszAdminName, pszAction, pszTargetName, pszAction2);
}

void PrintMultiAdminAction(ETargetType nType, const char *pszAdminName, const char *pszAction, const char *pszAction2 = "")
{
	switch (nType)
	{
	case ETargetType::ALL:
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "%s 所有人%s.", pszAdminName, pszAction, pszAction2);
		break;
	case ETargetType::T:
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "%s Terrorists%s.", pszAdminName, pszAction, pszAction2);
		break;
	case ETargetType::CT:
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "%s Counter-Terrorists%s.", pszAdminName, pszAction, pszAction2);
		break;
	}
}

CON_COMMAND_F(c_reload_admins, "Reload admin config", FCVAR_SPONLY | FCVAR_LINKED_CONCOMMAND)
{
	if (!g_pAdminSystem->LoadAdmins())
		return;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (!pPlayer || pPlayer->IsFakeClient() || !pPlayer->IsAuthenticated())
			continue;

		pPlayer->CheckAdmin();
	}

	Message("Admins reloaded\n");
}

CON_COMMAND_F(c_reload_infractions, "Reload infractions file", FCVAR_SPONLY | FCVAR_LINKED_CONCOMMAND)
{
	if (!g_pAdminSystem->LoadInfractions())
		return;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (!pPlayer || pPlayer->IsFakeClient())
			continue;

		pPlayer->CheckInfractions();
	}

	Message("Infractions reloaded\n");
}

CON_COMMAND_CHAT_FLAGS(ban, "ban a player", ADMFLAG_BAN)
{
	if (args.ArgC() < 3)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !ban <name> <持续时长/0 (永久)>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	if (g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlot) != ETargetType::PLAYER || iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"你只能指定一个玩家来封禁.");
		return;
	}

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"未找到玩家.");
		return;
	}

	int iDuration = V_StringToInt32(args[2], -1);

	if (iDuration == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"无效时长.");
		return;
	}
	CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[0]);

	if (!pTarget)
		return;

	ZEPlayer* pTargetPlayer = g_playerManager->GetPlayer(pSlot[0]);

	if (pTargetPlayer->IsFakeClient())
		return;

	if (!pTargetPlayer->IsAuthenticated())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s is not yet authenticated, consider kicking instead or please wait a moment and try again.", pTarget->GetPlayerName());
		return;
	}

	CInfractionBase *infraction = new CBanInfraction(iDuration, pTargetPlayer->GetSteamId64());

	g_pAdminSystem->AddInfraction(infraction);
	infraction->ApplyInfraction(pTargetPlayer);
	g_pAdminSystem->SaveInfractions();

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "封禁了 %s 持续时长 %i 分钟.", pszCommandPlayerName, pTarget->GetPlayerName(), iDuration);

	if (iDuration > 0)
	{
		char szAction[64];
		V_snprintf(szAction, sizeof(szAction), " 持续时长 %i 分钟", iDuration);
		PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "封禁了", szAction);
	}
	else
	{
		PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "永久封禁了");
	}
}

CON_COMMAND_CHAT_FLAGS(mute, "mutes a player", ADMFLAG_CHAT)
{
	if (args.ArgC() < 3)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !mute <name> <持续时长/0 (永久)>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlot);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	int iDuration = V_StringToInt32(args[2], -1);

	if (iDuration < 0)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "无效时长.");
		return;
	}

	if (iDuration == 0 && nType >= ETargetType::ALL)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你只能永久禁麦单个目标.");
		return;
	}

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	char szAction[64];
	V_snprintf(szAction, sizeof(szAction), " 持续时长 %i 分钟", iDuration);

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[i]);

		if (!pTarget)
			continue;

		ZEPlayer* pTargetPlayer = g_playerManager->GetPlayer(pSlot[i]);

		if (pTargetPlayer->IsFakeClient())
			continue;

		if (!pTargetPlayer->IsAuthenticated())
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s is not yet authenticated, please wait a moment and try again.", pTarget->GetPlayerName());
			continue;
		}

		CInfractionBase* infraction = new CMuteInfraction(iDuration, pTargetPlayer->GetSteamId64());

		// We're overwriting the infraction, so remove the previous one first
		g_pAdminSystem->FindAndRemoveInfraction(pTargetPlayer, CInfractionBase::Mute);
		g_pAdminSystem->AddInfraction(infraction);
		infraction->ApplyInfraction(pTargetPlayer);
		g_pAdminSystem->SaveInfractions();

		if (iDuration > 0)
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "禁麦了", szAction);
		else
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "永久禁麦了");
	}

	g_pAdminSystem->SaveInfractions();

	PrintMultiAdminAction(nType, pszCommandPlayerName, "禁麦了", szAction);
}

CON_COMMAND_CHAT_FLAGS(unmute, "unmutes a player", ADMFLAG_CHAT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !unmute <name>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlot);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[i]);

		if (!pTarget)
			continue;

		ZEPlayer *pTargetPlayer = g_playerManager->GetPlayer(pSlot[i]);

		if (pTargetPlayer->IsFakeClient())
			continue;

		if (!g_pAdminSystem->FindAndRemoveInfraction(pTargetPlayer, CInfractionBase::Mute) && nType < ETargetType::ALL)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s 未被禁麦.", pTarget->GetPlayerName());
			continue;
		}

		if (nType < ETargetType::ALL)
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "取消禁麦了");
	}

	g_pAdminSystem->SaveInfractions();

	PrintMultiAdminAction(nType, pszCommandPlayerName, "取消禁麦了");
}

CON_COMMAND_CHAT_FLAGS(gag, "gag a player", ADMFLAG_CHAT)
{
	if (args.ArgC() < 3)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !gag <name> <持续时长/0 (永久)>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlot);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	int iDuration = V_StringToInt32(args[2], -1);

	if (iDuration < 0)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "无效时长.");
		return;
	}

	if (iDuration == 0 && nType >= ETargetType::ALL)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你只能禁言单个玩家.");
		return;
	}

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	char szAction[64];
	V_snprintf(szAction, sizeof(szAction), " 持续时长 %i 分钟", iDuration);

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[i]);

		if (!pTarget)
			continue;

		ZEPlayer *pTargetPlayer = g_playerManager->GetPlayer(pSlot[i]);

		if (pTargetPlayer->IsFakeClient())
			continue;

		if (!pTargetPlayer->IsAuthenticated())
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s is not yet authenticated, please wait a moment and try again.", pTarget->GetPlayerName());
			continue;
		}

		CInfractionBase *infraction = new CGagInfraction(iDuration, pTargetPlayer->GetSteamId64());

		// We're overwriting the infraction, so remove the previous one first
		g_pAdminSystem->FindAndRemoveInfraction(pTargetPlayer, CInfractionBase::Gag);
		g_pAdminSystem->AddInfraction(infraction);
		infraction->ApplyInfraction(pTargetPlayer);

		if (nType >= ETargetType::ALL)
			continue;

		if (iDuration > 0)
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "禁言了", szAction);
		else
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "永久禁言了");
	}

	g_pAdminSystem->SaveInfractions();

	PrintMultiAdminAction(nType, pszCommandPlayerName, "禁言了", szAction);
}

CON_COMMAND_CHAT_FLAGS(ungag, "ungags a player", ADMFLAG_CHAT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !ungag <name>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlot);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[i]);

		if (!pTarget)
			continue;

		ZEPlayer *pTargetPlayer = g_playerManager->GetPlayer(pSlot[i]);

		if (pTargetPlayer->IsFakeClient())
			continue;

		if (!g_pAdminSystem->FindAndRemoveInfraction(pTargetPlayer, CInfractionBase::Gag) && nType < ETargetType::ALL)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s 未被禁言.", pTarget->GetPlayerName());
			continue;
		}

		if (nType < ETargetType::ALL)
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "取消禁言了");
	}

	g_pAdminSystem->SaveInfractions();

	PrintMultiAdminAction(nType, pszCommandPlayerName, "取消禁言了");
}

CON_COMMAND_CHAT_FLAGS(kick, "kick a player", ADMFLAG_KICK)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !kick <name>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlot);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"未找到玩家.");
		return;
	}

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[i]);

		if (!pTarget)
			continue;

		ZEPlayer* pTargetPlayer = g_playerManager->GetPlayer(pSlot[i]);
		
		g_pEngineServer2->DisconnectClient(pTargetPlayer->GetPlayerSlot(), NETWORK_DISCONNECT_KICKED);

		PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "踢出了");
	}
}

CON_COMMAND_CHAT_FLAGS(slay, "slay a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !slay <name>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlots[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlots);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"未找到玩家.");
		return;
	}

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlots[i]);

		if (!pTarget)
			continue;

		pTarget->GetPawn()->CommitSuicide(false, true);

		if (nType < ETargetType::ALL)
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "处死了");
	}

	PrintMultiAdminAction(nType, pszCommandPlayerName, "处死了");
}

CON_COMMAND_CHAT_FLAGS(slap, "slap a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !slap <name> <optional damage>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlots[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlots);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	for (int i = 0; i < iNumClients; i++)
	{
		CBasePlayerController *pTarget = (CBasePlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(pSlots[i] + 1));

		if (!pTarget)
			continue;

		CBasePlayerPawn *pPawn = pTarget->m_hPawn();

		if (!pPawn)
			continue;

		// Taken directly from sourcemod
		Vector velocity = pPawn->m_vecAbsVelocity;
		velocity.x += ((rand() % 180) + 50) * (((rand() % 2) == 1) ? -1 : 1);
		velocity.y += ((rand() % 180) + 50) * (((rand() % 2) == 1) ? -1 : 1);
		velocity.z += rand() % 200 + 100;
		pPawn->SetAbsVelocity(velocity);

		int iDamage = V_StringToInt32(args[2], 0);
			
		if (iDamage > 0)
			pPawn->TakeDamage(iDamage);

		if (nType < ETargetType::ALL)
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "打了", "的脸");
	}

	PrintMultiAdminAction(nType, pszCommandPlayerName, "打了", "的脸");
}

CON_COMMAND_CHAT_FLAGS(goto, "teleport to a player", ADMFLAG_SLAY)
{
	// Only players can use this command at all
	if (!player)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你无法在控制台执行该指令.");
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !goto <name>");
		return;
	}

	int iNumClients = 0;
	int pSlots[MAXPLAYERS];

	if (g_playerManager->TargetPlayerString(player->GetPlayerSlot(), args[1], iNumClients, pSlots) != ETargetType::PLAYER || iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "目标玩家不明确.");
		return;
	}

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlots[i]);

		if (!pTarget)
			continue;

		Vector newOrigin = pTarget->GetPawn()->GetAbsOrigin();

		player->GetPawn()->Teleport(&newOrigin, nullptr, nullptr);

		PrintSingleAdminAction(player->GetPlayerName(), pTarget->GetPlayerName(), "传送至");
	}
}

CON_COMMAND_CHAT_FLAGS(bring, "bring a player", ADMFLAG_SLAY)
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你无法在控制台执行该指令.");
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !bring <name>");
		return;
	}

	int iNumClients = 0;
	int pSlots[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(player->GetPlayerSlot(), args[1], iNumClients, pSlots);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlots[i]);

		if (!pTarget)
			continue;

		Vector newOrigin = player->GetPawn()->GetAbsOrigin();

		pTarget->GetPawn()->Teleport(&newOrigin, nullptr, nullptr);

		if (nType < ETargetType::ALL)
			PrintSingleAdminAction(player->GetPlayerName(), pTarget->GetPlayerName(), "将", "传送到身边");
	}

	PrintMultiAdminAction(nType, player->GetPlayerName(), "将", "传送到身边");
}

CON_COMMAND_CHAT_FLAGS(setteam, "set a player's team", ADMFLAG_SLAY)
{
	if (args.ArgC() < 3)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !setteam <name> <team (0-3)>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlots[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlots);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	int iTeam = V_StringToInt32(args[2], -1);

	if (iTeam < CS_TEAM_NONE || iTeam > CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "无效的队伍, 范围在 0-3 之间.");
		return;
	}

	const char *pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	constexpr const char *teams[] = {"无", "观察者", "terrorists", "counter-terrorists"};

	char szAction[64];
	V_snprintf(szAction, sizeof(szAction), " 至队伍 %s", teams[iTeam]);

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlots[i]);

		if (!pTarget)
			continue;

		pTarget->SwitchTeam(iTeam);

		if (nType < ETargetType::ALL)
			PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "移动", szAction);
	}

	PrintMultiAdminAction(nType, pszCommandPlayerName, "移动", szAction);
}

CON_COMMAND_CHAT_FLAGS(noclip, "toggle noclip on yourself", ADMFLAG_SLAY | ADMFLAG_CHEATS)
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你无法在控制台执行该指令.");
		return;
	}

	CBasePlayerPawn *pPawn = player->m_hPawn();

	if (!pPawn)
		return;

	if (pPawn->m_iHealth() <= 0)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你无法在死亡的时候使用!");
		return;
	}

	if (pPawn->m_MoveType() == MOVETYPE_NOCLIP)
	{
		pPawn->m_MoveType = MOVETYPE_WALK;
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "禁用了穿墙模式.", player->GetPlayerName());
	}
	else
	{
		pPawn->m_MoveType = MOVETYPE_NOCLIP;
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "启用了穿墙模式.", player->GetPlayerName());
	}
}

CON_COMMAND_CHAT_FLAGS(reload_discord_bots, "Reload discord bot config", ADMFLAG_ROOT)
{
	g_pDiscordBotManager->LoadDiscordBotsConfig();
	Message("Discord bot config reloaded\n");
}

CON_COMMAND_CHAT_FLAGS(entfire, "fire outputs at entities", ADMFLAG_RCON)
{
	if (args.ArgC() < 3)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !entfire <name> <input> <optional parameter>");
		return;
	}

	variant_string_t value(args[3]);

	int iFoundEnts = 0;

	Z_CBaseEntity *pTarget = nullptr;

	// The idea here is to only use one of the targeting modes at once, prioritizing !picker then targetname/!self then classname
	// Try picker first, FindEntityByName can also take !picker but it always uses player 0 so we have to do this ourselves
	if (player && !V_strcmp("!picker", args[1]))
	{
		pTarget = UTIL_FindPickerEntity(player);

		if (pTarget)
		{
			pTarget->AcceptInput(args[2], player, player, &value);
			iFoundEnts++;
		}
	}

	// !self would resolve to the player controller, so here's a convenient alias to get the pawn instead
	if (player && !V_strcmp("!selfpawn", args[1]))
	{
		pTarget = player->GetPawn();

		if (pTarget)
		{
			pTarget->AcceptInput(args[2], player, player, &value);
			iFoundEnts++;
		}
	}
	
	if (!iFoundEnts)
	{
		while (pTarget = UTIL_FindEntityByName(pTarget, args[1], player))
		{
			pTarget->AcceptInput(args[2], player, player, &value);
			iFoundEnts++;
		}
	}

	if (!iFoundEnts)
	{
		while (pTarget = UTIL_FindEntityByClassname(pTarget, args[1]))
		{
			pTarget->AcceptInput(args[2], player, player, &value);
			iFoundEnts++;
		}
	}

	if (!iFoundEnts)
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到目标实体.");
	else
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "成功在 %i 个实体上执行了输入.", iFoundEnts);
}

CON_COMMAND_CHAT_FLAGS(entfirepawn, "fire outputs at player pawns", ADMFLAG_RCON)
{
	if (args.ArgC() < 3)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !entfirepawn <name> <input> <optional parameter>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlots[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlots);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	variant_string_t value(args[3]);

	int iFoundEnts = 0;

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController *pTarget = CCSPlayerController::FromSlot(pSlots[i]);

		if (!pTarget || !pTarget->GetPawn())
			continue;

		pTarget->GetPawn()->AcceptInput(args[2], player, player, &value);
		iFoundEnts++;
	}

	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "成功在 %i 个玩家上执行了pawn输入.", iFoundEnts);
}

CON_COMMAND_CHAT_FLAGS(entfirecontroller, "fire outputs at player controllers", ADMFLAG_RCON)
{
	if (args.ArgC() < 3)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !entfirecontroller <name> <input> <optional parameter>");
		return;
	}

	int iCommandPlayer = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlots[MAXPLAYERS];

	ETargetType nType = g_playerManager->TargetPlayerString(iCommandPlayer, args[1], iNumClients, pSlots);

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "未找到玩家.");
		return;
	}

	variant_string_t value(args[3]);

	int iFoundEnts = 0;

	for (int i = 0; i < iNumClients; i++)
	{
		CCSPlayerController *pTarget = CCSPlayerController::FromSlot(pSlots[i]);

		if (!pTarget)
			continue;

		pTarget->AcceptInput(args[2], player, player, &value);
		iFoundEnts++;
	}

	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "成功在 %i 个玩家上执行了controller输入.", iFoundEnts);
}

CON_COMMAND_CHAT_FLAGS(map, "change map", ADMFLAG_CHANGEMAP)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Usage: !map <mapname>");
		return;
	}

	if (!g_pEngineServer2->IsMapValid(args[1]))
	{
		// This might be a workshop map, and right now there's no easy way to get the list from a collection
		// So blindly attempt the change for now, as the command does nothing if the map isn't found
		std::string sCommand = "ds_workshop_changelevel " + std::string(args[1]);

		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "5秒后尝试更改地图至创意工坊地图 %s...", args[1]);

		new CTimer(5.0f, false, [sCommand]()
		{
			g_pEngineServer2->ServerCommand(sCommand.c_str());
			return -1.0f;
		});

		return;
	}

	// Copy the string, since we're passing this into a timer
	char szMapName[MAX_PATH];
	V_strncpy(szMapName, args[1], sizeof(szMapName));

	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "更改地图至 %s...", szMapName);

	new CTimer(5.0f, false, [szMapName]()
	{
		g_pEngineServer2->ChangeLevel(szMapName, nullptr);
		return -1.0f;
	});
}

CON_COMMAND_CHAT_FLAGS(hsay, "say something as a hud hint", ADMFLAG_CHAT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !hsay <message>");
		return;
	}

	ClientPrintAll(HUD_PRINTCENTER, "%s", args.ArgS());
}

CON_COMMAND_CHAT_FLAGS(rcon, "send a command to server console", ADMFLAG_RCON)
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你都在用控制台了还用这个干毛线呢.");
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !rcon <command>");
		return;
	}

	g_pEngineServer2->ServerCommand(args.ArgS());
}

CON_COMMAND_CHAT_FLAGS(extend, "extend current map (negative value reduces map duration)", ADMFLAG_CHANGEMAP)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "指令格式: !extend <minutes>");
		return;
	}

	int iExtendTime = V_StringToInt32(args[1], 0);

	ConVar* cvar = g_pCVar->GetConVar(g_pCVar->FindConVar("mp_timelimit"));

	// CONVAR_TODO
	// HACK: values is actually the cvar value itself, hence this ugly cast.
	float flTimelimit = *(float *)&cvar->values;

	if (gpGlobals->curtime - g_pGameRules->m_flGameStartTime > flTimelimit * 60)
		flTimelimit = (gpGlobals->curtime - g_pGameRules->m_flGameStartTime) / 60.0f + iExtendTime;
	else
	{
		if (flTimelimit == 1)
			flTimelimit = 0;
		flTimelimit += iExtendTime;
	}

	if (flTimelimit <= 0)
		flTimelimit = 1;

	// CONVAR_TODO
	char buf[32];
	V_snprintf(buf, sizeof(buf), "mp_timelimit %.6f", flTimelimit);
	g_pEngineServer2->ServerCommand(buf);

	const char* pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	if (iExtendTime < 0)
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "缩减了 %i 分钟的地图时长.", pszCommandPlayerName, iExtendTime * -1);
	else
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "延长了 %i 分钟的地图时长.", pszCommandPlayerName, iExtendTime);
}

bool CAdminSystem::LoadAdmins()
{
	m_vecAdmins.Purge();
	KeyValues* pKV = new KeyValues("admins");
	KeyValues::AutoDelete autoDelete(pKV);

	const char *pszPath = "addons/cs2fixes/configs/admins.cfg";

	if (!pKV->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		Warning("Failed to load %s\n", pszPath);
		return false;
	}
	for (KeyValues* pKey = pKV->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey())
	{
		const char *pszName = pKey->GetName();
		const char *pszSteamID = pKey->GetString("steamid", nullptr);
		const char *pszFlags = pKey->GetString("flags", nullptr);

		if (!pszSteamID)
		{
			Warning("Admin entry %s is missing 'steam' key\n", pszName);
			return false;
		}

		if (!pszFlags)
		{
			Warning("Admin entry %s is missing 'flags' key\n", pszName);
			return false;
		}

		ConMsg("Loaded admin %s\n", pszName);
		ConMsg(" - Steam ID %5s\n", pszSteamID);
		ConMsg(" - Flags %5s\n", pszFlags);

		uint64 iFlags = ParseFlags(pszFlags);

		// Let's just use steamID64 for now
		m_vecAdmins.AddToTail(CAdmin(pszName, atoll(pszSteamID), iFlags));
	}

	return true;
}

bool CAdminSystem::LoadInfractions()
{
	m_vecInfractions.PurgeAndDeleteElements();
	KeyValues* pKV = new KeyValues("infractions");
	KeyValues::AutoDelete autoDelete(pKV);

	const char *pszPath = "addons/cs2fixes/data/infractions.txt";

	if (!pKV->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		Warning("Failed to load %s\n", pszPath);
		return false;
	}

	for (KeyValues* pKey = pKV->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey())
	{
		uint64 iSteamId = pKey->GetUint64("steamid", -1);
		time_t iEndTime = pKey->GetUint64("endtime", -1);
		int iType = pKey->GetInt("type", -1);

		if (iSteamId == -1)
		{
			Warning("Infraction entry is missing 'steam' key\n");
			return false;
		}

		if (iEndTime == -1)
		{
			Warning("Infraction entry is missing 'endtime' key\n");
			return false;
		}

		if (iType == -1)
		{
			Warning("Infraction entry is missing 'type' key\n");
			return false;
		}

		switch (iType)
		{
		case CInfractionBase::Ban:
			AddInfraction(new CBanInfraction(iEndTime, iSteamId, true));
			break;
		case CInfractionBase::Mute:
			AddInfraction(new CMuteInfraction(iEndTime, iSteamId, true));
			break;
		case CInfractionBase::Gag:
			AddInfraction(new CGagInfraction(iEndTime, iSteamId, true));
			break;
		default:
			Warning("Invalid infraction type %d\n", iType);
		}
	}

	return true;
}

void CAdminSystem::SaveInfractions()
{
	KeyValues* pKV = new KeyValues("infractions");
	KeyValues* pSubKey;
	KeyValues::AutoDelete autoDelete(pKV);

	FOR_EACH_VEC(m_vecInfractions, i)
	{
		time_t timestamp = m_vecInfractions[i]->GetTimestamp();
		if (timestamp != 0 && timestamp < std::time(0))
			continue;

		char buf[5];
		V_snprintf(buf, sizeof(buf), "%d", i);
		pSubKey = new KeyValues(buf);
		pSubKey->AddUint64("steamid", m_vecInfractions[i]->GetSteamId64());
		pSubKey->AddUint64("endtime", m_vecInfractions[i]->GetTimestamp());
		pSubKey->AddInt("type", m_vecInfractions[i]->GetType());

		pKV->AddSubKey(pSubKey);
	}

	const char *pszPath = "addons/cs2fixes/data/infractions.txt";

	if (!pKV->SaveToFile(g_pFullFileSystem, pszPath))
		Warning("Failed to save infractions to %s", pszPath);
}

void CAdminSystem::AddInfraction(CInfractionBase* infraction)
{
	m_vecInfractions.AddToTail(infraction);
}


// This function can run at least twice when a player connects: Immediately upon client connection, and also upon getting authenticated by steam.
// It's also run when we're periodically checking for infraction expiry in the case of mutes/gags.
// This returns false only when called from ClientConnect and the player is banned in order to reject them.
bool CAdminSystem::ApplyInfractions(ZEPlayer *player)
{
	FOR_EACH_VEC(m_vecInfractions, i)
	{
		// Because this can run without the player being authenticated, and the fact that we're applying a ban/mute here,
		// we can immediately just use the steamid we got from the connecting player.
		uint64 iSteamID = player->IsAuthenticated() ? player->GetSteamId64() : player->GetUnauthenticatedSteamId64();

		// We're only interested in infractions concerning this player
		if (m_vecInfractions[i]->GetSteamId64() != iSteamID)
			continue;

		// Undo the infraction just briefly while checking if it ran out
		m_vecInfractions[i]->UndoInfraction(player);

		time_t timestamp = m_vecInfractions[i]->GetTimestamp();
		if (timestamp != 0 && timestamp <= std::time(0))
		{
			m_vecInfractions.Remove(i);
			continue;
		}

		// We are called from ClientConnect and the player is banned, immediately reject them
		if (!player->IsConnected() && m_vecInfractions[i]->GetType() == CInfractionBase::EInfractionType::Ban)
			return false;
		
		m_vecInfractions[i]->ApplyInfraction(player);
	}

	return true;
}

bool CAdminSystem::FindAndRemoveInfraction(ZEPlayer *player, CInfractionBase::EInfractionType type)
{
	FOR_EACH_VEC(m_vecInfractions, i)
	{
		if (m_vecInfractions[i]->GetSteamId64() == player->GetSteamId64() && m_vecInfractions[i]->GetType() == type)
		{
			m_vecInfractions[i]->UndoInfraction(player);
			m_vecInfractions.Remove(i);
			
			return true;
		}
	}

	return false;
}

CAdmin *CAdminSystem::FindAdmin(uint64 iSteamID)
{
	FOR_EACH_VEC(m_vecAdmins, i)
	{
		if (m_vecAdmins[i].GetSteamID() == iSteamID)
			return &m_vecAdmins[i];
	}

	return nullptr;
}

uint64 CAdminSystem::ParseFlags(const char* pszFlags)
{
	uint64 flags = 0;
	size_t length = V_strlen(pszFlags);

	for (size_t i = 0; i < length; i++)
	{
		char c = tolower(pszFlags[i]);
		if (c < 'a' || c > 'z')
			continue;

		if (c == 'z')
			return -1; // all flags

		flags |= ((uint64)1 << (c - 'a'));
	}

	return flags;
}

void CBanInfraction::ApplyInfraction(ZEPlayer *player)
{
	g_pEngineServer2->DisconnectClient(player->GetPlayerSlot(), NETWORK_DISCONNECT_KICKBANADDED); // "Kicked and banned"
}

void CMuteInfraction::ApplyInfraction(ZEPlayer* player)
{
	player->SetMuted(true);
}

void CMuteInfraction::UndoInfraction(ZEPlayer *player)
{
	player->SetMuted(false);
}

void CGagInfraction::ApplyInfraction(ZEPlayer *player)
{
	player->SetGagged(true);
}

void CGagInfraction::UndoInfraction(ZEPlayer *player)
{
	player->SetGagged(false);
}