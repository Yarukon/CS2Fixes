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

#include "votemanager.h"
#include "commands.h"
#include "playermanager.h"
#include "ctimer.h"
#include "entity/cgamerules.h"

#include "tier0/memdbgon.h"

extern CEntitySystem *g_pEntitySystem;
extern IVEngineServer2 *g_pEngineServer2;
extern CGlobalVars *gpGlobals;
extern CCSGameRules *g_pGameRules;

ERTVState g_RTVState = ERTVState::MAP_START;
EExtendState g_ExtendState = EExtendState::MAP_START;
int g_ExtendsLeft = 1;

// CONVAR_TODO
float g_RTVSucceedRatio = 0.6f;
float g_ExtendSucceedRatio = 0.5f;
int g_ExtendTimeToAdd = 20;

int GetCurrentRTVCount()
{
	int iVoteCount = 0;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (pPlayer && pPlayer->GetRTVVote() && !pPlayer->IsFakeClient())
			iVoteCount++;
	}

	return iVoteCount;
}

int GetNeededRTVCount()
{
	int iOnlinePlayers = 0.0f;
	int iVoteCount = 0;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (pPlayer && !pPlayer->IsFakeClient())
		{
			iOnlinePlayers++;
			if (pPlayer->GetRTVVote())
				iVoteCount++;
		}
	}

	return (int)(iOnlinePlayers * g_RTVSucceedRatio) + 1;
}

int GetCurrentExtendCount()
{
	int iVoteCount = 0;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (pPlayer && pPlayer->GetExtendVote() && !pPlayer->IsFakeClient())
			iVoteCount++;
	}

	return iVoteCount;
}

int GetNeededExtendCount()
{
	int iOnlinePlayers = 0.0f;
	int iVoteCount = 0;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (pPlayer && !pPlayer->IsFakeClient())
		{
			iOnlinePlayers++;
			if (pPlayer->GetExtendVote())
				iVoteCount++;
		}
	}

	return (int)(iOnlinePlayers * g_ExtendSucceedRatio) + 1;
}

CON_COMMAND_CHAT(rtv, "Vote to end the current map sooner.")
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你无法从控制台使用该指令.");
		return;
	}

	int iPlayer = player->GetPlayerSlot();

	ZEPlayer* pPlayer = g_playerManager->GetPlayer(iPlayer);

	// Something has to really go wrong for this to happen
	if (!pPlayer)
	{
		Warning("%s Tried to access a null ZEPlayer!!\n", player->GetPlayerName());
		return;
	}

	switch (g_RTVState)
	{
	case ERTVState::MAP_START:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "RTV 还未开放.");
		return;
	case ERTVState::POST_RTV_SUCCESSFULL:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "RTV 投票已经完成.");
		return;
	case ERTVState::POST_LAST_ROUND_END:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "RTV 在选择下一张地图时禁用.");
		return;
	case ERTVState::BLOCKED_BY_ADMIN:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "RTV 已被管理员禁用.");
		return;
	}

	int iCurrentRTVCount = GetCurrentRTVCount();
	int iNeededRTVCount = GetNeededRTVCount();

	if (pPlayer->GetRTVVote())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你已经投过票了 (%i 已投票, %i 需要).", iCurrentRTVCount, iNeededRTVCount);
		return;
	}

	if (pPlayer->GetRTVVoteTime() + 60.0f > gpGlobals->curtime)
	{
		int iRemainingTime = (int)(pPlayer->GetRTVVoteTime() + 60.0f - gpGlobals->curtime);
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Wait %i seconds before you can RTV again.", iRemainingTime);
		return;
	}

	if (iCurrentRTVCount + 1 >= iNeededRTVCount)
	{
		g_RTVState = ERTVState::POST_RTV_SUCCESSFULL;
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "RTV 成功! 这是这张地图的最后一回合!");
		// CONVAR_TODO
		g_pEngineServer2->ServerCommand("mp_timelimit 1");

		for (int i = 0; i < gpGlobals->maxClients; i++)
		{
			ZEPlayer* pPlayer2 = g_playerManager->GetPlayer(i);
			if (pPlayer2)
				pPlayer2->SetRTVVote(false);
		}

		return;
	}

	pPlayer->SetRTVVote(true);
	pPlayer->SetRTVVoteTime(gpGlobals->curtime);
	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "%s 想要投票换图 (%i 已投票, %i 需要).", player->GetPlayerName(), iCurrentRTVCount + 1, iNeededRTVCount);
}

CON_COMMAND_CHAT(unrtv, "Remove your vote to end the current map sooner.")
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你无法从控制台使用该指令.");
		return;
	}

	int iPlayer = player->GetPlayerSlot();

	ZEPlayer* pPlayer = g_playerManager->GetPlayer(iPlayer);

	// Something has to really go wrong for this to happen
	if (!pPlayer)
	{
		Warning("%s Tried to access a null ZEPlayer!!\n", player->GetPlayerName());
		return;
	}

	if (!pPlayer->GetRTVVote())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你还没有投票.");
		return;
	}

	pPlayer->SetRTVVote(false);
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你取消了换图投票.");
}


CON_COMMAND_CHAT(ve, "Vote to extend the current map.")
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你无法从控制台使用该指令.");
		return;
	}

	int iPlayer = player->GetPlayerSlot();

	ZEPlayer* pPlayer = g_playerManager->GetPlayer(iPlayer);

	// Something has to really go wrong for this to happen
	if (!pPlayer)
	{
		Warning("%s Tried to access a null ZEPlayer!!\n", player->GetPlayerName());
		return;
	}

	switch (g_ExtendState)
	{
	case EExtendState::MAP_START:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "投票延长还未开放.");
		return;
	case EExtendState::POST_EXTEND_COOLDOWN:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "投票延长还未开放.");
		return;
	case EExtendState::POST_EXTEND_NO_EXTENDS_LEFT:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "该地图已经没有延长次数.");
		return;
	case EExtendState::POST_LAST_ROUND_END:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "投票延长在选择下一张地图时禁用.");
		return;
	case EExtendState::NO_EXTENDS:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "当前地图不支持投票延长.");
		return;
	}

	int iCurrentExtendCount = GetCurrentExtendCount();
	int iNeededExtendCount = GetNeededExtendCount();

	if (pPlayer->GetExtendVote())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你已经投过票了 (%i 已投票, %i 需要).", iCurrentExtendCount, iNeededExtendCount);
		return;
	}

	if (pPlayer->GetExtendVoteTime() + 60.0f > gpGlobals->curtime)
	{
		int iRemainingTime = (int)(pPlayer->GetExtendVoteTime() + 60.0f - gpGlobals->curtime);
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Wait %i seconds before you can vote extend again.", iRemainingTime);
		return;
	}

	if (iCurrentExtendCount + 1 >= iNeededExtendCount)
	{
		// mimic behaviour of !extend
		// CONVAR_TODO
		ConVar* cvar = g_pCVar->GetConVar(g_pCVar->FindConVar("mp_timelimit"));

		float flTimelimit;
		// type punning
		memcpy(&flTimelimit, &cvar->values, sizeof(flTimelimit));

		if (gpGlobals->curtime - g_pGameRules->m_flGameStartTime > flTimelimit * 60)
			flTimelimit = (gpGlobals->curtime - g_pGameRules->m_flGameStartTime) / 60.0f + g_ExtendTimeToAdd;
		else
		{
			if (flTimelimit == 1)
				flTimelimit = 0;
			flTimelimit += g_ExtendTimeToAdd;
		}

		if (flTimelimit <= 0)
			flTimelimit = 1;
		
		char buf[32];
		V_snprintf(buf, sizeof(buf), "mp_timelimit %.6f", flTimelimit + g_ExtendTimeToAdd);

		// CONVAR_TODO
		g_pEngineServer2->ServerCommand(buf);

		if (g_ExtendsLeft == 1)
			// there are no extends left after a successfull extend vote
			g_ExtendState = EExtendState::POST_EXTEND_NO_EXTENDS_LEFT;
		else
		{
			// there's an extend left after a successfull extend vote
			g_ExtendState = EExtendState::POST_EXTEND_COOLDOWN;

			// Allow another extend vote after 2 minutes
			new CTimer(120.0f, false, []()
			{
				if (g_ExtendState == EExtendState::POST_EXTEND_COOLDOWN)
					g_ExtendState = EExtendState::EXTEND_ALLOWED;
				return -1.0f;
			});
		}

		for (int i = 0; i < gpGlobals->maxClients; i++)
		{
			ZEPlayer* pPlayer2 = g_playerManager->GetPlayer(i);
			if (pPlayer2)
				pPlayer2->SetExtendVote(false);
		}

		g_ExtendsLeft--;
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "延长投票完成! 当前地图已延长 %i 分钟.", g_ExtendTimeToAdd);

		return;
	}

	pPlayer->SetExtendVote(true);
	pPlayer->SetExtendVoteTime(gpGlobals->curtime);
	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "%s 想要延长地图 (%i 已投票, %i 需要).", player->GetPlayerName(), iCurrentExtendCount+1, iNeededExtendCount);
}

CON_COMMAND_CHAT(unve, "Remove your vote to extend current map.")
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你无法从控制台使用该指令.");
		return;
	}

	int iPlayer = player->GetPlayerSlot();

	ZEPlayer* pPlayer = g_playerManager->GetPlayer(iPlayer);

	// Something has to really go wrong for this to happen
	if (!pPlayer)
	{
		Warning("%s Tried to access a null ZEPlayer!!\n", player->GetPlayerName());
		return;
	}

	if (!pPlayer->GetExtendVote())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你还未投票.");
		return;
	}

	pPlayer->SetExtendVote(false);
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你取消了延长投票.");
}

CON_COMMAND_CHAT_FLAGS(blockrtv, "Block the ability for players to vote to end current map sooner.", ADMFLAG_CHANGEMAP)
{
	if (g_RTVState == ERTVState::BLOCKED_BY_ADMIN)
	{
		if (player)
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "RTV 已经被禁用.");
		else
			ConMsg("RTV 已经被禁用.");
		return;
	}

	const char* pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	g_RTVState = ERTVState::BLOCKED_BY_ADMIN;

	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "禁用了RTV.", pszCommandPlayerName);
}

CON_COMMAND_CHAT_FLAGS(unblockrtv, "Restore the ability for players to vote to end current map sooner.", ADMFLAG_CHANGEMAP)
{
	if (g_RTVState == ERTVState::RTV_ALLOWED)
	{
		if (player)
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "RTV 未被禁用.");
		else
			ConMsg("RTV 未被禁用.");
		return;
	}

	const char* pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	g_RTVState = ERTVState::RTV_ALLOWED;

	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "启用了RTV.", pszCommandPlayerName);
}

CON_COMMAND_CHAT_FLAGS(addextend, "Add another extend to the current map for players to vote.", ADMFLAG_CHANGEMAP)
{
	const char* pszCommandPlayerName = player ? player->GetPlayerName() : "量子套";

	if (g_ExtendState == EExtendState::POST_EXTEND_NO_EXTENDS_LEFT || g_ExtendState == EExtendState::NO_EXTENDS)
		g_ExtendState = EExtendState::EXTEND_ALLOWED;
	
	g_ExtendsLeft += 1;

	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "增加了一次延长投票的机会.", pszCommandPlayerName);
}

CON_COMMAND_CHAT(extendsleft, "Display amount of extends left for the current map")
{
	char message[64];

	switch (g_ExtendsLeft)
	{
	case 0:
		strcpy(message, "没有延长机会了.");
		break;
	case 1:
		strcpy(message, "剩余 1 次延长机会.");
		break;
	default:
		V_snprintf(message, sizeof(message), "剩余 %i 次延长机会.", g_ExtendsLeft);
		break;
	}

	if (player)
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s", message);
	else
		ConMsg("%s", message);
}

CON_COMMAND_CHAT(timeleft, "Display time left to end of current map.")
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你无法从控制台使用该指令.");
		return;
	}

	// CONVAR_TODO
	ConVar* cvar = g_pCVar->GetConVar(g_pCVar->FindConVar("mp_timelimit"));

	float flTimelimit;
	memcpy(&flTimelimit, &cvar->values, sizeof(flTimelimit));

	if (flTimelimit == 0)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "当前地图没有时间限制.");
		return;
	}

	int iTimeleft = (int) ((g_pGameRules->m_flGameStartTime + flTimelimit * 60.0f) - gpGlobals->curtime);

	if (iTimeleft < 0)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "已经是最后一回合了!");
		return;
	}

	div_t div = std::div(iTimeleft, 60);
	int iMinutesLeft = div.quot;
	int iSecondsLeft = div.rem;
	
	if (iMinutesLeft > 0)
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "剩余时间: %i 分 %i 秒", iMinutesLeft, iSecondsLeft);
	else
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "剩余时间: %i 秒", iSecondsLeft);
}