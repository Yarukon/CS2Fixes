/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2024 Source2ZE
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
#include "icvar.h"
#include "entity/cgamerules.h"

#include "tier0/memdbgon.h"

extern CGameEntitySystem *g_pEntitySystem;
extern IVEngineServer2 *g_pEngineServer2;
extern CGlobalVars *gpGlobals;
extern CCSGameRules *g_pGameRules;

ERTVState g_RTVState = ERTVState::MAP_START;
EExtendState g_ExtendState = EExtendState::MAP_START;

bool g_bVoteManagerEnable = false;
int g_iExtendsLeft = 1;
float g_flExtendSucceedRatio = 0.5f;
int g_iExtendTimeToAdd = 20;
float g_flRTVSucceedRatio = 0.6f;
bool g_bRTVEndRound = false;

FAKE_BOOL_CVAR(cs2f_votemanager_enable, "Whether to enable votemanager features such as RTV and extends", g_bVoteManagerEnable, false, false)
FAKE_INT_CVAR(cs2f_extends, "Maximum extends per map", g_iExtendsLeft, 1, false)
FAKE_FLOAT_CVAR(cs2f_extend_success_ratio, "Ratio needed to pass an extend", g_flExtendSucceedRatio, 0.5f, false)
FAKE_INT_CVAR(cs2f_extend_time, "Time to add per extend", g_iExtendTimeToAdd, 20, false)
FAKE_FLOAT_CVAR(cs2f_rtv_success_ratio, "Ratio needed to pass RTV", g_flRTVSucceedRatio, 0.6f, false)
FAKE_BOOL_CVAR(cs2f_rtv_endround, "Whether to immediately end the round when RTV succeeds", g_bRTVEndRound, false, false)

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

	return (int)(iOnlinePlayers * g_flRTVSucceedRatio) + 1;
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

	return (int)(iOnlinePlayers * g_flExtendSucceedRatio) + 1;
}

CON_COMMAND_CHAT(rtv, "- Vote to end the current map sooner")
{
	if (!g_bVoteManagerEnable)
		return;

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
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "你已经投过票了 (%i 已投票, 需要 %i 票).", iCurrentRTVCount, iNeededRTVCount);
		return;
	}

	if (pPlayer->GetRTVVoteTime() + 60.0f > gpGlobals->curtime)
	{
		int iRemainingTime = (int)(pPlayer->GetRTVVoteTime() + 60.0f - gpGlobals->curtime);
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "再次投票换图前请等待 %i 秒.", iRemainingTime);
		return;
	}

	if (iCurrentRTVCount + 1 >= iNeededRTVCount)
	{
		g_RTVState = ERTVState::POST_RTV_SUCCESSFULL;
		g_ExtendState = EExtendState::POST_RTV;
		// CONVAR_TODO
		g_pEngineServer2->ServerCommand("mp_timelimit 1");

		if (g_bRTVEndRound)
		{
			ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "RTV 成功! 正在结束当前地图...");

			new CTimer(3.0f, false, true, []()
			{
				g_pGameRules->TerminateRound(5.0f, CSRoundEndReason::Draw);

				return -1.0f;
			});
		}
		else
		{
			ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "RTV 成功! 这是当前地图的最后一回合!");
		}

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
	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "%s 想要投票换图 (%i 已投票, 需要 %i 票).", player->GetPlayerName(), iCurrentRTVCount + 1, iNeededRTVCount);
}

CON_COMMAND_CHAT(unrtv, "- Remove your vote to end the current map sooner")
{
	if (!g_bVoteManagerEnable)
		return;

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

CON_COMMAND_CHAT(ve, "- Vote to extend current map")
{
	if (!g_bVoteManagerEnable)
		return;

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
	case EExtendState::POST_RTV:
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Extend vote is closed because RTV vote has passed.");
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
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "再次投票延长前请先等待 %i 秒.", iRemainingTime);
		return;
	}

	if (iCurrentExtendCount + 1 >= iNeededExtendCount)
	{
		// mimic behaviour of !extend
		// CONVAR_TODO
		ConVar* cvar = g_pCVar->GetConVar(g_pCVar->FindConVar("mp_timelimit"));

		// CONVAR_TODO
		// HACK: values is actually the cvar value itself, hence this ugly cast.
		float flTimelimit = *(float *)&cvar->values;

		if (gpGlobals->curtime - g_pGameRules->m_flGameStartTime > flTimelimit * 60)
			flTimelimit = (gpGlobals->curtime - g_pGameRules->m_flGameStartTime) / 60.0f + g_iExtendTimeToAdd;
		else
		{
			if (flTimelimit == 1)
				flTimelimit = 0;
			flTimelimit += g_iExtendTimeToAdd;
		}

		if (flTimelimit <= 0)
			flTimelimit = 1;
		
		char buf[32];
		V_snprintf(buf, sizeof(buf), "mp_timelimit %.6f", flTimelimit);

		// CONVAR_TODO
		g_pEngineServer2->ServerCommand(buf);

		if (g_iExtendsLeft == 1)
			// there are no extends left after a successfull extend vote
			g_ExtendState = EExtendState::POST_EXTEND_NO_EXTENDS_LEFT;
		else
		{
			// there's an extend left after a successfull extend vote
			g_ExtendState = EExtendState::POST_EXTEND_COOLDOWN;

			// Allow another extend vote after added time lapses
			new CTimer(g_iExtendTimeToAdd * 60.0f, false, true, []()
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

		g_iExtendsLeft--;
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "延长投票完成! 当前地图已延长 %i 分钟.", g_iExtendTimeToAdd);

		return;
	}

	pPlayer->SetExtendVote(true);
	pPlayer->SetExtendVoteTime(gpGlobals->curtime);
	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "%s 想要延长地图 (%i 已投票, %i 需要).", player->GetPlayerName(), iCurrentExtendCount+1, iNeededExtendCount);
}

CON_COMMAND_CHAT(unve, "- Remove your vote to extend current map")
{
	if (!g_bVoteManagerEnable)
		return;

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

CON_COMMAND_CHAT_FLAGS(disablertv, "- Disable the ability for players to vote to end current map sooner", ADMFLAG_CHANGEMAP)
{
	if (!g_bVoteManagerEnable)
		return;

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

CON_COMMAND_CHAT_FLAGS(enablertv, "- Restore the ability for players to vote to end current map sooner", ADMFLAG_CHANGEMAP)
{
	if (!g_bVoteManagerEnable)
		return;

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

CON_COMMAND_CHAT_FLAGS(addextend, "- Add another extend to the current map for players to vote", ADMFLAG_CHANGEMAP)
{
	if (!g_bVoteManagerEnable)
		return;

	const char* pszCommandPlayerName = player ? player->GetPlayerName() : "控制台";

	if (g_ExtendState == EExtendState::POST_EXTEND_NO_EXTENDS_LEFT || g_ExtendState == EExtendState::NO_EXTENDS)
		g_ExtendState = EExtendState::EXTEND_ALLOWED;
	
	g_iExtendsLeft += 1;

	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX ADMIN_PREFIX "增加了一次延长投票的机会.", pszCommandPlayerName);
}

CON_COMMAND_CHAT(extendsleft, "- Display amount of extends left for the current map")
{
	if (!g_bVoteManagerEnable)
		return;

	char message[64];

	switch (g_iExtendsLeft)
	{
	case 0:
		strcpy(message, "没有延长机会了.");
		break;
	case 1:
		strcpy(message, "剩余 1 次延长机会.");
		break;
	default:
		V_snprintf(message, sizeof(message), "剩余 %i 次延长机会.", g_iExtendsLeft);
		break;
	}

	if (player)
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s", message);
	else
		ConMsg("%s", message);
}

CON_COMMAND_CHAT(timeleft, "- Display time left to end of current map.")
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "你无法从控制台使用该指令.");
		return;
	}

	ConVar* cvar = g_pCVar->GetConVar(g_pCVar->FindConVar("mp_timelimit"));

	// CONVAR_TODO
	// HACK: values is actually the cvar value itself, hence this ugly cast.
	float flTimelimit = *(float *)&cvar->values;

	if (flTimelimit == 0.0f)
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
