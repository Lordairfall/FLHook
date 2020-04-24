// PvE Controller for Discovery FLHook
// June 2019 by Kazinsal etc.
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.


#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <algorithm>
#include <random>

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

using namespace std;

PLUGIN_RETURNCODE returncode;

#define PLUGIN_DEBUG_NONE 0
#define PLUGIN_DEBUG_CONSOLE 1
#define PLUGIN_DEBUG_VERBOSE 2
#define PLUGIN_DEBUG_VERYVERBOSE 3

struct CLIENT_DATA {
	int bounty_count;
	int bounty_pool;
};

struct stBountyBasePayout {
	int iBasePayout;
};

struct stDropInfo {
	uint uGoodID;
	float fChance;
};

CLIENT_DATA aClientData[250];
map<uint, stBountyBasePayout> mapBountyPayouts;
map<uint, stBountyBasePayout> mapBountyShipPayouts;
map<uint, float> mapBountyGroupScale;
list<uint> lstRecordedBountyObjs;

multimap<uint, stDropInfo> mmapDropInfo;

int set_iPluginDebug = 0;
uint set_uLootCrateID = 0;

bool set_bBountiesEnabled = true;
int set_iPoolPayoutTimer = 0;
int iLoadedNPCBountyClasses = 0;
int iLoadedNPCShipBountyOverrides = 0;
int iLoadedNPCBountyGroupScale = 0;
void LoadSettingsNPCBounties(void);


bool set_bDropsEnabled = true;
int iLoadedNPCDropClasses = 0;
void LoadSettingsNPCDrops(void);

/// Clear client info when a client connects.
void ClearClientInfo(uint iClientID)
{
	aClientData[iClientID] = { 0 };
}

/// Load settings.
void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\pvecontroller.cfg";

	// Load generic settings
	set_iPluginDebug = IniGetI(scPluginCfgFile, "General", "debug", 0);
	set_uLootCrateID = CreateID(IniGetS(scPluginCfgFile, "NPCDrops", "drop_crate", "lootcrate_ast_loot_metal").c_str());

	// Load settings blocks
	LoadSettingsNPCBounties();
	LoadSettingsNPCDrops();
}

void LoadSettingsNPCBounties()
{
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\pvecontroller.cfg";

	// Clear the bounty tables
	mapBountyPayouts.clear();
	iLoadedNPCBountyClasses = 0;
	mapBountyShipPayouts.clear();
	iLoadedNPCShipBountyOverrides = 0;
	mapBountyGroupScale.clear();
	iLoadedNPCBountyGroupScale = 0;

	// Load ratting bounty settings
	set_iPoolPayoutTimer = IniGetI(scPluginCfgFile, "NPCBounties", "pool_payout_timer", 0);

	// Load the big stuff
	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("NPCBounties"))
			{
				while (ini.read_value())
				{
					if (!strcmp(ini.get_name_ptr(), "enabled"))
					{
						if (ini.get_value_int(0) == 0)
							set_bBountiesEnabled = false;
					}

					if (!strcmp(ini.get_name_ptr(), "group_scale"))
					{
						mapBountyGroupScale[ini.get_value_int(0)] = ini.get_value_float(1);
						++iLoadedNPCBountyGroupScale;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded group scale multiplier %u, %f.\n", ini.get_value_int(0), ini.get_value_float(1));
					}

					if (!strcmp(ini.get_name_ptr(), "class"))
					{
						int iClass = ini.get_value_int(0);
						mapBountyPayouts[iClass].iBasePayout = ini.get_value_int(1);
						++iLoadedNPCBountyClasses;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded class base value %u, $%d.\n", iClass, mapBountyPayouts[iClass].iBasePayout);
					}

					if (!strcmp(ini.get_name_ptr(), "ship"))
					{
						uint uShiparchHash = CreateID(ini.get_value_string(0));
						mapBountyShipPayouts[uShiparchHash].iBasePayout = ini.get_value_int(1);
						++iLoadedNPCShipBountyOverrides;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded override for \"%s\" == %u, $%d.\n", stows(ini.get_value_string(0)).c_str(), uShiparchHash, mapBountyShipPayouts[uShiparchHash].iBasePayout);
					}
				}
			}

		}
		ini.close();
	}

	ConPrint(L"PVECONTROLLER: NPC bounties are %s.\n", set_bBountiesEnabled ? L"enabled" : L"disabled");
	ConPrint(L"PVECONTROLLER: Loaded %u NPC bounty group scale values.\n", iLoadedNPCBountyGroupScale);
	ConPrint(L"PVECONTROLLER: Loaded %u NPC bounty classes.\n", iLoadedNPCBountyClasses);
	ConPrint(L"PVECONTROLLER: Loaded %u NPC bounty ship overrides.\n", iLoadedNPCShipBountyOverrides);
}

void LoadSettingsNPCDrops()
{
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\pvecontroller.cfg";

	// Clear the drop tables.
	mmapDropInfo.clear();
	iLoadedNPCDropClasses = 0;

	// Load the big stuff
	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("NPCDrops"))
			{
				while (ini.read_value())
				{
					if (!strcmp(ini.get_name_ptr(), "enabled"))
					{
						if (ini.get_value_int(0) == 0)
							set_bDropsEnabled = false;
					}

					if (!strcmp(ini.get_name_ptr(), "class"))
					{
						stDropInfo drop;
						int iClass = ini.get_value_int(0);
						string szGood = ini.get_value_string(1);
						drop.uGoodID = CreateID(szGood.c_str());
						drop.fChance = ini.get_value_float(2);
						mmapDropInfo.insert(make_pair(iClass, drop));
						++iLoadedNPCDropClasses;
						if (set_iPluginDebug)
							ConPrint(L"PVECONTROLLER: Loaded class %u drop %s (0x%08X), %f chance.\n", iClass, stows(szGood).c_str(), CreateID(szGood.c_str()), drop.fChance);
					}
				}
			}

		}
		ini.close();
	}

	ConPrint(L"PVECONTROLLER: NPC drops are %s.\n", set_bDropsEnabled ? L"enabled" : L"disabled");
	ConPrint(L"PVECONTROLLER: Loaded %u NPC drops by class.\n", iLoadedNPCDropClasses);
}


BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	srand(static_cast<uint>(time(nullptr)));
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
			LoadSettings();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NPCBountyAddToPool(uint iClientID, int iBounty, bool bNotify) {
	if (!iClientID)
		return;

	aClientData[iClientID].bounty_count++;
	aClientData[iClientID].bounty_pool += iBounty;
	if (bNotify)
		PrintUserCmdText(iClientID, L"A $%s credit bounty has been added to your reward pool.", ToMoneyStr(iBounty).c_str());
}

void NPCBountyPayout(uint iClientID) {
	if (!iClientID)
		return;

	float fValue;
	pub::Player::GetAssetValue(iClientID, fValue);

	int iCurrMoney;
	pub::Player::InspectCash(iClientID, iCurrMoney);

	long long lNewMoney = iCurrMoney;
	lNewMoney += aClientData[iClientID].bounty_pool;

	if (fValue + aClientData[iClientID].bounty_pool > 2000000000 || lNewMoney > 2000000000)
	{
		PrintUserCmdText(iClientID, L"A bounty pool worth $%s credits was attempted to be paid, but the result would overfill your neural net account.", ToMoneyStr(aClientData[iClientID].bounty_pool).c_str());
		PrintUserCmdText(iClientID, L"Payment of this bounty pool will be retried later.");
		return;
	}

	HkAddCash((const wchar_t*)Players.GetActiveCharacterName(iClientID), aClientData[iClientID].bounty_pool);
	PrintUserCmdText(iClientID, L"A bounty pool worth $%s credits for %d kill%s has been deposited in your account.", ToMoneyStr(aClientData[iClientID].bounty_pool).c_str(), aClientData[iClientID].bounty_count, (aClientData[iClientID].bounty_count == 1 ? L"" : L"s"));

	aClientData[iClientID].bounty_count = 0;
	aClientData[iClientID].bounty_pool = 0;
}

/*void NPCSendPM(int iNPCIndex, int toClientID, const wstring &wscMessage) {
	// todo
}

void NPCSendChat(int iNPCIndex, const wstring &wscMessage) {
	// incomplete
	wstring wscSender = (const wchar_t*)Players.GetActiveCharacterName(iFromClientID);

	// Get the player's current system and location in the system.
	uint iSystemID;
	pub::Player::GetSystem(iFromClientID, iSystemID);

	uint iFromShip;
	pub::Player::GetShip(iFromClientID, iFromShip);

	Vector vFromShipLoc;
	Matrix mFromShipDir;
	pub::SpaceObj::GetLocation(iFromShip, vFromShipLoc, mFromShipDir);

	// For all players in system...
	struct PlayerData *pPD = 0;
	while (pPD = Players.traverse_active(pPD)) {
		// Get the this player's current system and location in the system.
		uint iClientID = HkGetClientIdFromPD(pPD);
		uint iClientSystemID = 0;
		pub::Player::GetSystem(iClientID, iClientSystemID);
		if (iSystemID != iClientSystemID)
			continue;

		uint iShip;
		pub::Player::GetShip(iClientID, iShip);

		Vector vShipLoc;
		Matrix mShipDir;
		pub::SpaceObj::GetLocation(iShip, vShipLoc, mShipDir);

		if (HkDistance3D(vFromShipLoc, vShipLoc) <= 15000.0f)
			FormatSendChat(iClientID, wscSender, wscMessage, L"DDDDDD");
	}
}

void NPCSendGroupChat(int iNPCIndex, const wstring &wscMessage) {
	// todo
}*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*bool UserCmd_PvEController(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	PrintUserCmdText(iClientID, L"PvE Controller is enabled.");
	return true;
}

bool UserCmd_DialogTest(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	HkChangeIDSString(iClientID, 500000, L"Dialog Test");
	HkChangeIDSString(iClientID, 526999, L"Wow, this is a dialog test!");

	FmtStr caption(0, 0);
	caption.begin_mad_lib(500000);
	caption.end_mad_lib();

	FmtStr message(0, 0);
	message.begin_mad_lib(526999);
	message.end_mad_lib();

	pub::Player::PopUpDialog(iClientID, caption, message, POPUPDIALOG_BUTTONS_CENTER_NO | POPUPDIALOG_BUTTONS_LEFT_YES);
	return true;
}

bool UserCmd_BaseTest(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	CUSTOM_BASE_IS_DOCKED_STRUCT isDockedInfo;
	isDockedInfo.iClientID = iClientID;
	isDockedInfo.iDockedBaseID = 0;
	Plugin_Communication(CUSTOM_BASE_IS_DOCKED, &isDockedInfo);

	if (isDockedInfo.iDockedBaseID)
	{
		CUSTOM_BASE_QUERY_MODULE_STRUCT moduleInfo;
		moduleInfo.iClientID = iClientID;
		moduleInfo.iModuleType = 12;	// TYPE_FABRICATOR
		moduleInfo.bExists = false;
		Plugin_Communication(CUSTOM_BASE_QUERY_MODULE, &moduleInfo);
		
		if (moduleInfo.bExists)
			PrintUserCmdText(iClientID, L"Success! Docked on a POB that has an Equipment Fabricator Bay.");
		else
			PrintUserCmdText(iClientID, L"No Equipment Fabricator Bay found.");
	}
	else
		PrintUserCmdText(iClientID, L"Not docked on POB.");

	return true;
}*/

bool UserCmd_Pool(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	if (set_iPoolPayoutTimer == 0) {
		PrintUserCmdText(iClientID, L"Bounty pool has been disabled; all bounties are paid out at time of kill.");
		return true;
	}

	uint next_tick = set_iPoolPayoutTimer - ((uint)time(0) % set_iPoolPayoutTimer);
	if (aClientData[iClientID].bounty_pool == 0)
		PrintUserCmdText(iClientID, L"You do not currently have any outstanding bounty payments.");
	else
		PrintUserCmdText(iClientID, L"You will be paid out $%s credits for %d kill%s in %dm%ds.", ToMoneyStr(aClientData[iClientID].bounty_pool).c_str(), aClientData[iClientID].bounty_count, (aClientData[iClientID].bounty_count == 1 ? L"" : L"s"), next_tick / 60, next_tick % 60);

	return true;
}

bool UserCmd_Value(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	float fShipValue = 0;
	HKGetShipValue((const wchar_t*)Players.GetActiveCharacterName(iClientID), fShipValue);
	PrintUserCmdText(iClientID, L"Ship value: $%s credits.", ToMoneyStr(fShipValue).c_str());

	return true;
}




///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Client command processing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct USERCMD
{
	wchar_t *wszCmd;
	_UserCmdProc proc;
	wchar_t *usage;
};

USERCMD UserCmds[] =
{
	//{ L"/pvecontroller", UserCmd_PvEController, L"" },
	//{ L"/dialogtest", UserCmd_DialogTest, L"" },
	//{ L"/basetest", UserCmd_BaseTest, L"" },
	{ L"/pool", UserCmd_Pool, L"" },
	{ L"/value", UserCmd_Value, L"" },
};

/**
This function is called by FLHook when a user types a chat string. We look at the
string they've typed and see if it starts with one of the above commands. If it
does we try to process it.
*/
bool UserCmd_Process(uint iClientID, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	// If the chat string does not match the USER_CMD then we do not handle the
	// command, so let other plugins or FLHook kick in. We require an exact match
	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			// Extract the parameters string from the chat string. It should
			// be immediately after the command and a space.
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			// Dispatch the command to the appropriate processing function.
			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				returncode = SKIPPLUGINS_NOFUNCTIONCALL;
				return true;
			}
		}
	}
	return false;
}

bool ExecuteCommandString_Callback(CCmds* cmds, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	if (wscCmd.compare(L"pvecontroller"))
		return false;

	if (!cmds->ArgStrToEnd(1).compare(L"status"))
	{
		cmds->Print(L"PVECONTROLLER: PvE controller is active.\n");
		if (set_iPoolPayoutTimer)
		{
			uint next_tick = set_iPoolPayoutTimer - ((uint)time(0) % set_iPoolPayoutTimer);
			uint pools = 0, poolvalue = 0, poolkills = 0;
			for (int i = 0; i < 250; i++) {
				if (aClientData[i].bounty_pool) {
					pools++;
					poolvalue += aClientData[i].bounty_pool;
					poolkills += aClientData[i].bounty_count;
				}
			}
			cmds->Print(L"  There are %d outstanding bounty pools worth $%s credits for %d kill%s to be paid out in %dm%ds.\n", pools, ToMoneyStr(poolvalue).c_str(), poolkills, (poolkills != 1 ? L"s" : L""), next_tick / 60, next_tick % 60);
		}
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (!cmds->ArgStrToEnd(1).compare(L"payout"))
	{
		if (set_iPoolPayoutTimer)
		{
			uint pools = 0, poolvalue = 0, poolkills = 0;
			for (int i = 0; i < 250; i++) {
				if (aClientData[i].bounty_pool) {
					pools++;
					poolvalue += aClientData[i].bounty_pool;
					poolkills += aClientData[i].bounty_count;
					NPCBountyPayout(i);
				}
			}
			cmds->Print(L"PVECONTROLLER: Paid out %d outstanding bounty pools worth $%s credits for %d kill%s.\n", pools, ToMoneyStr(poolvalue).c_str(), poolkills, (poolkills != 1 ? L"s" : L""));
		}
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	else if (!cmds->ArgStrToEnd(1).compare(L"reloadall"))
	{
		cmds->Print(L"PVECONTROLLER: COMPLETE LIVE RELOAD requested by %s.\n", cmds->GetAdminName());
		LoadSettings();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live reload completed.\n");
		return true;
	}
	else if (!cmds->ArgStrToEnd(1).compare(L"reloadnpcbounties"))
	{
		cmds->Print(L"PVECONTROLLER: Live NPC bounties reload requested by %s.\n", cmds->GetAdminName());
		LoadSettingsNPCBounties();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live NPC bounties reload completed.\n");
		return true;
	}
	else if (!cmds->ArgStrToEnd(1).compare(L"reloadnpcdrops"))
	{
		cmds->Print(L"PVECONTROLLER: Live NPC drops reload requested by %s.\n", cmds->GetAdminName());
		LoadSettingsNPCDrops();
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		cmds->Print(L"PVECONTROLLER: Live NPC drops reload completed.\n");
		return true;
	}
	else
	{
		cmds->Print(L"Usage:\n");
		cmds->Print(L"  .pvecontroller status    -- Displays PvE controller status information.\n");
		cmds->Print(L"  .pvecontroller payout    -- Pays out all outstanding NPC bounties.\n");
		cmds->Print(L"  .pvecontroller reloadall -- Reloads ALL settings on the fly.\n");
		cmds->Print(L"  .pvecontroller reloadnpcbounties -- Reloads NPC bounty settings on the fly.\n");
		cmds->Print(L"  .pvecontroller reloadnpcdrops -- Reloads NPC drop settings on the fly.\n");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void __stdcall HkCb_AddDmgEntry(DamageList *dmg, unsigned short p1, float damage, enum DamageEntry::SubObjFate fate)
{
	returncode = DEFAULT_RETURNCODE;
	uint iDmgFrom = HkGetClientIDByShip(dmg->get_inflictor_id());
	if (iDmgToSpaceID && iDmgFrom) {
		if (HkGetClientIDByShip(iDmgToSpaceID))
			return;

		if (p1 != 1)
			return;

		if (damage == 0.0f) {
			// Prevent paying out the same kill twice
			if (find(lstRecordedBountyObjs.begin(), lstRecordedBountyObjs.end(), iDmgToSpaceID) != lstRecordedBountyObjs.end())
				return;

			unsigned int uArchID = 0;
			pub::SpaceObj::GetSolarArchetypeID(iDmgToSpaceID, uArchID);
			Archetype::Ship* victimShiparch = Archetype::GetShip(uArchID);
			if (set_iPluginDebug >= PLUGIN_DEBUG_VERYVERBOSE)
				PrintUserCmdText(iDmgFrom, L"PVECONTROLLER: You killed an NPC uArchID == %u",uArchID);

			// Grab some info we'll need later.
			uint uKillerSystem = 0;
			pub::Player::GetSystem(iDmgFrom, uKillerSystem);

			int iBountyPayout = 0;

			// Determine bounty payout.
			map<uint, stBountyBasePayout>::iterator iter = mapBountyShipPayouts.find(uArchID);
			if (iter != mapBountyShipPayouts.end()) {
				if (set_iPluginDebug >= PLUGIN_DEBUG_VERBOSE)
					PrintUserCmdText(iDmgFrom, L"Overriding payout for uarch %u to be $%d.", uArchID, iter->second.iBasePayout);
				iBountyPayout = iter->second.iBasePayout;
			}
			else {
				map<uint, stBountyBasePayout>::iterator iter = mapBountyPayouts.find(victimShiparch->iShipClass);
				if (iter != mapBountyPayouts.end()) {
					iBountyPayout = iter->second.iBasePayout;
					if (victimShiparch->iShipClass < 5) {
						unsigned int iDunno = 0;
						IObjInspectImpl *obj = NULL;
						if (GetShipInspect(iDmgToSpaceID, obj, iDunno)) {
							if (obj) {
								CShip* cship = (CShip*)HkGetEqObjFromObjRW((IObjRW*)obj);
								CEquipManager* eqmanager = (CEquipManager*)((char*)cship + 0xE4);
								CEArmor *cearmor = (CEArmor*)eqmanager->FindFirst(0x1000000);
								if (cearmor) {
									switch (cearmor->archetype->iArchID) {
										case 0x8F502A4A:	// armor_scale_2 - rank 10-13
											iBountyPayout = iBountyPayout * 2;
											break;
										case 0x8F50BA4A:	// armor_scale_6 - rank 14-16
											iBountyPayout = (int)((float)iBountyPayout * 3.5);
											break;
										case 0x8F51624A:	// armor_scale_8 - rank 17-18
											iBountyPayout = iBountyPayout * 7;
											break;
										case 0xB047EE8A:	// armor_scale_10 - rank 19
											iBountyPayout = iBountyPayout * 10;
											break;
									}
								}
							}
						}
					}
				}
			}

			// If we've turned bounties off, don't pay it.
			if (set_bBountiesEnabled == false)
				iBountyPayout = 0;

			if (iBountyPayout) {
				list<GROUP_MEMBER> lstMembers;
				HkGetGroupMembers((const wchar_t*)Players.GetActiveCharacterName(iDmgFrom), lstMembers);

				if (set_iPluginDebug >= PLUGIN_DEBUG_VERYVERBOSE)
					PrintUserCmdText(iDmgFrom, L"PVECONTROLLER: There are %u players in your group.", lstMembers.size());

				foreach (lstMembers, GROUP_MEMBER, gm) {
					uint uGroupMemberSystem = 0;
					pub::Player::GetSystem(gm->iClientID, uGroupMemberSystem);
					if (uKillerSystem != uGroupMemberSystem)
						lstMembers.erase(gm);
				}

				if (mapBountyGroupScale[lstMembers.size()])
					iBountyPayout = (int)((float)iBountyPayout * mapBountyGroupScale[lstMembers.size()]);
				else
					iBountyPayout = (int)((float)iBountyPayout / lstMembers.size());

				if (set_iPluginDebug >= PLUGIN_DEBUG_VERYVERBOSE)
					PrintUserCmdText(iDmgFrom, L"PVECONTROLLER: Paying out $%d to %u eligible group members in your system.", iBountyPayout, lstMembers.size());

				foreach(lstMembers, GROUP_MEMBER, gm) {
					NPCBountyAddToPool(gm->iClientID, iBountyPayout, set_iPoolPayoutTimer);
					if (!set_iPoolPayoutTimer)
						NPCBountyPayout(gm->iClientID);
				}

				lstRecordedBountyObjs.push_back(iDmgToSpaceID);
			}

			// Process drops if enabled
			if (set_bDropsEnabled) {
				for (auto it = mmapDropInfo.begin(); it != mmapDropInfo.end(); it++) {
					if (it->first == victimShiparch->iShipClass) {
						if (set_iPluginDebug >= PLUGIN_DEBUG_VERYVERBOSE)
							PrintUserCmdText(iDmgFrom, L"PVECONTROLLER: class %d drop entry found, %f chance to drop 0x%08X.\n", it->first, it->second.fChance, it->second.uGoodID);
						float roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
						if (roll < it->second.fChance) {
							if (set_iPluginDebug >= PLUGIN_DEBUG_VERYVERBOSE)
								PrintUserCmdText(iDmgFrom, L"PVECONTROLLER: Rolled %f, won a drop!\n", roll);

							Vector vLoc = { 0.0f, 0.0f, 0.0f };
							Matrix mRot = { 0.0f, 0.0f, 0.0f };
							pub::SpaceObj::GetLocation(iDmgToSpaceID, vLoc, mRot);
							vLoc.x += 30.0;
							Server.MineAsteroid(uKillerSystem, vLoc, set_uLootCrateID, it->second.uGoodID, 1, iDmgFrom);
						}
						else
							if (set_iPluginDebug >= PLUGIN_DEBUG_VERYVERBOSE)
								PrintUserCmdText(iDmgFrom, L"PVECONTROLLER: Rolled %f, no drop for you.\n", roll);
					}
				}
			}
		}
	}
}

void HkTimerCheckKick()
{
	returncode = DEFAULT_RETURNCODE;
	uint curr_time = (uint)time(0);

	// Pay bounty pools as required.
	if (set_iPoolPayoutTimer) {
		if (curr_time % set_iPoolPayoutTimer == 0) {
			for (int i = 0; i < 250; i++) {
				if (aClientData[i].bounty_pool)
					NPCBountyPayout(i);
			}
		}
	}

	// Clear our list of recorded bounty objects every 5 seconds.
	if (curr_time % 5 == 0) {
		lstRecordedBountyObjs.clear();
	}
}

void __stdcall DisConnect(uint iClientID, enum EFLConnection p2)
{
	returncode = DEFAULT_RETURNCODE;

	//ConPrint(L"PVE: DisConnect for id=%d char=%s\n", iClientID, Players.GetActiveCharacterName(iClientID));

	if (ClientInfo[iClientID].bCharSelected)
		NPCBountyPayout(iClientID);

	ClearClientInfo(iClientID);
}

void __stdcall CharacterInfoReq(uint iClientID, bool p2)
{
	returncode = DEFAULT_RETURNCODE;

	//ConPrint(L"PVE: CharacterInfoReq for id=%d char=%s, p2=%s\n", iClientID, Players.GetActiveCharacterName(iClientID), p2 ? L"true" : L"false");

	if (ClientInfo[iClientID].bCharSelected)
		NPCBountyPayout(iClientID);

	ClearClientInfo(iClientID);
}

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "PvE Controller by Kazinsal et al.";
	p_PI->sShortName = "pvecontroller";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ClearClientInfo, PLUGIN_ClearClientInfo, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkCb_AddDmgEntry, PLUGIN_HkCb_AddDmgEntry, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkTimerCheckKick, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DisConnect, PLUGIN_HkIServerImpl_DisConnect, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterInfoReq, PLUGIN_HkIServerImpl_CharacterInfoReq, 0));

	return p_PI;
}
