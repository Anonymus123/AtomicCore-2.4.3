/**
 *    Copyright (C) 2010-2013 AtomicCore <http://www.frozen-kingdom.eu/>
 *
 *    AtomicCore is based on TrinityCore, MythCore, ChaosCore and own
 *    WoW emulations. Only authorized persons get access to our
 *    repositories. Sharing of our code is not allowed!
 */

#include "Common.h"
#include "ObjectMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "Config.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "CliRunnable.h"
#include "Language.h"
#include "Log.h"
#include "MapManager.h"
#include "Player.h"
#include "Util.h"

#if PLATFORM != WINDOWS
#include <readline/readline.h>
#include <readline/history.h>

char * command_finder(const char* text, int state)
{
    static int idx,len;
    const char* ret;
    ChatCommand *cmd = ChatHandler::getCommandTable();

    if (!state)
    {
        idx = 0;
        len = strlen(text);
    }

    while((ret = cmd[idx].Name))
    {
        if (!cmd[idx].AllowConsole)
        {
            idx++;
            continue;
        }

        idx++;
        if (strncmp(ret, text, len) == 0)
            return strdup(ret);
        if (cmd[idx].Name == NULL)
            break;
    }

    return ((char*)NULL);
}

char ** cli_completion(const char * text, int start, int /*end*/)
{
    char ** matches;
    matches = (char**)NULL;

    if (start == 0)
        matches = rl_completion_matches((char*)text,&command_finder);
    else
        rl_bind_key('\t',rl_abort);
    return (matches);
}
#endif

void utf8print(void* /*arg*/, const char* str)
{
#if PLATFORM == PLATFORM_WINDOWS
    wchar_t wtemp_buf[6000];
    size_t wtemp_len = 6000-1;
    if (!Utf8toWStr(str,strlen(str),wtemp_buf,wtemp_len))
        return;

    char temp_buf[6000];
    CharToOemBuffW(&wtemp_buf[0],&temp_buf[0],wtemp_len+1);
    printf(temp_buf);
#else
    printf("%s", str);
#endif
}

void commandFinished(void*, bool /*success*/)
{
    printf("Oregon>");
    fflush(stdout);
}

bool ChatHandler::HandleAccountDeleteCommand(const char* args)
{
    if (!*args)
        return false;

    char *account_name_str=strtok ((char*)args," ");
    if (!account_name_str)
        return false;

    std::string account_name = account_name_str;
    if (!AccountMgr::normalizeString(account_name))
    {
        PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 account_id = sAccountMgr->GetId(account_name);
    if (!account_id)
    {
        PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (m_session)
    {
        uint32 targetSecurity = sAccountMgr->GetSecurity(account_id);

        if (targetSecurity >= m_session->GetSecurity())
        {
            SendSysMessage (LANG_YOURS_SECURITY_IS_LOW);
            SetSentErrorMessage (true);
            return false;
        }
    }

    AccountOpResult result = sAccountMgr->DeleteAccount(account_id);
    switch(result)
    {
        case AOR_OK:
            PSendSysMessage(LANG_ACCOUNT_DELETED,account_name.c_str());
            break;
        case AOR_NAME_NOT_EXIST:
            PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        case AOR_DB_INTERNAL_ERROR:
            PSendSysMessage(LANG_ACCOUNT_NOT_DELETED_SQL_ERROR,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        default:
            PSendSysMessage(LANG_ACCOUNT_NOT_DELETED,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
    }

    return true;
}

bool ChatHandler::GetDeletedCharacterInfoList(DeletedInfoList& foundList, std::string searchString)
{
    QueryResult_AutoPtr resultChar;
    if (!searchString.empty())
    {
        if (isNumeric(searchString.c_str()))
            resultChar = CharacterDatabase.PQuery("SELECT guid, deleteInfos_Name, deleteInfos_Account, deleteDate FROM characters WHERE deleteDate IS NOT NULL AND guid = %llu", uint64(atoi(searchString.c_str())));
        else
        {
            if(!normalizePlayerName(searchString))
                return false;

            resultChar = CharacterDatabase.PQuery("SELECT guid, deleteInfos_Name, deleteInfos_Account, deleteDate FROM characters WHERE deleteDate IS NOT NULL AND deleteInfos_Name " _LIKE_ " " _CONCAT3_("'%%'", "'%s'", "'%%'"), searchString.c_str());
        }
    }
    else
        resultChar = CharacterDatabase.Query("SELECT guid, deleteInfos_Name, deleteInfos_Account, deleteDate FROM characters WHERE deleteDate IS NOT NULL");

    if (resultChar)
    {
        do
        {
            Field* fields = resultChar->Fetch();

            DeletedInfo info;

            info.lowguid    = fields[0].GetUInt32();
            info.name       = fields[1].GetCppString();
            info.accountId  = fields[2].GetUInt32();

            sAccountMgr->GetName(info.accountId, info.accountName);
            info.deleteDate = time_t(fields[3].GetUInt64());
            foundList.push_back(info);
        } while (resultChar->NextRow());
    }

    return true;
}

std::string ChatHandler::GenerateDeletedCharacterGUIDsWhereStr(DeletedInfoList::const_iterator& itr, DeletedInfoList::const_iterator const& itr_end)
{
    std::ostringstream wherestr;
    wherestr << "guid IN ('";
    for(; itr != itr_end; ++itr)
    {
        wherestr << itr->lowguid;

        if (wherestr.str().size() > MAX_QUERY_LEN - 50)
        {
            ++itr;
            break;
        }

        DeletedInfoList::const_iterator itr2 = itr;
        if(++itr2 != itr_end)
            wherestr << "','";
    }
    wherestr << "')";
    return wherestr.str();
}

void ChatHandler::HandleCharacterDeletedListHelper(DeletedInfoList const& foundList)
{
    if (!m_session)
    {
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_HEADER);
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
    }

    for (DeletedInfoList::const_iterator itr = foundList.begin(); itr != foundList.end(); ++itr)
    {
        std::string dateStr = TimeToTimestampStr(itr->deleteDate);

        if (!m_session)
            PSendSysMessage(LANG_CHARACTER_DELETED_LIST_LINE_CONSOLE,
                itr->lowguid, itr->name.c_str(), itr->accountName.empty() ? "<Not existed>" : itr->accountName.c_str(),
                itr->accountId, dateStr.c_str());
        else
            PSendSysMessage(LANG_CHARACTER_DELETED_LIST_LINE_CHAT,
                itr->lowguid, itr->name.c_str(), itr->accountName.empty() ? "<Not existed>" : itr->accountName.c_str(),
                itr->accountId, dateStr.c_str());
    }

    if (!m_session)
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
}

bool ChatHandler::HandleCharacterDeletedListCommand(const char* args)
{
    DeletedInfoList foundList;
    if (!GetDeletedCharacterInfoList(foundList, args))
        return false;

    if (foundList.empty())
    {
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_EMPTY);
        return false;
    }

    HandleCharacterDeletedListHelper(foundList);
    return true;
}

void ChatHandler::HandleCharacterDeletedRestoreHelper(DeletedInfo const& delInfo)
{
    if (delInfo.accountName.empty())
    {
        PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_ACCOUNT, delInfo.name.c_str(), delInfo.lowguid, delInfo.accountId);
        return;
    }

    uint32 charcount = sAccountMgr->GetCharactersCount(delInfo.accountId);
    if (charcount >= 10)
    {
        PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_FULL, delInfo.name.c_str(), delInfo.lowguid, delInfo.accountId);
        return;
    }

    if (objmgr.GetPlayerGUIDByName(delInfo.name))
    {
        PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_NAME, delInfo.name.c_str(), delInfo.lowguid, delInfo.accountId);
        return;
    }

    CharacterDatabase.PExecute("UPDATE characters SET name='%s', account='%u', deleteDate=NULL, deleteInfos_Name=NULL, deleteInfos_Account=NULL WHERE deleteDate IS NOT NULL AND guid = %u",
        delInfo.name.c_str(), delInfo.accountId, delInfo.lowguid);
}

bool ChatHandler::HandleCharacterDeletedRestoreCommand(const char* args)
{
    if (!*args)
        return false;

    std::string searchString;
    std::string newCharName;
    uint32 newAccount = 0;

    std::istringstream params(args);
    params >> searchString >> newCharName >> newAccount;

    DeletedInfoList foundList;
    if (!GetDeletedCharacterInfoList(foundList, searchString))
        return false;

    if (foundList.empty())
    {
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_EMPTY);
        return false;
    }

    SendSysMessage(LANG_CHARACTER_DELETED_RESTORE);
    HandleCharacterDeletedListHelper(foundList);

    if (newCharName.empty())
	{
        for (DeletedInfoList::iterator itr = foundList.begin(); itr != foundList.end(); ++itr)
            HandleCharacterDeletedRestoreHelper(*itr);
    }
    else if (foundList.size() == 1 && normalizePlayerName(newCharName))
    {
        DeletedInfo delInfo = foundList.front();

        delInfo.name = newCharName;

        if (newAccount && newAccount != delInfo.accountId)
        {
            delInfo.accountId = newAccount;
            sAccountMgr->GetName(newAccount, delInfo.accountName);
        }

        HandleCharacterDeletedRestoreHelper(delInfo);
    }
    else
        SendSysMessage(LANG_CHARACTER_DELETED_ERR_RENAME);

    return true;
}

bool ChatHandler::HandleCharacterDeletedDeleteCommand(const char* args)
{
    if (!*args)
        return false;

    DeletedInfoList foundList;
    if (!GetDeletedCharacterInfoList(foundList, args))
        return false;

    if (foundList.empty())
    {
        SendSysMessage(LANG_CHARACTER_DELETED_LIST_EMPTY);
        return false;
    }

    SendSysMessage(LANG_CHARACTER_DELETED_DELETE);
    HandleCharacterDeletedListHelper(foundList);

    for(DeletedInfoList::const_iterator itr = foundList.begin(); itr != foundList.end(); ++itr)
        Player::DeleteFromDB(itr->lowguid, 0, false, true);

    return true;
}

bool ChatHandler::HandleCharacterDeletedOldCommand(const char* args)
{
    int32 keepDays = sWorld.getConfig(CONFIG_CHARDELETE_KEEP_DAYS);

    char* px = strtok((char*)args, " ");
    if (px)
    {
        if (!isNumeric(px))
            return false;

        keepDays = atoi(px);
        if (keepDays < 0)
            return false;
    }
    else if (keepDays <= 0)
        return false;

    Player::DeleteOldCharacters((uint32)keepDays);
    return true;
}

bool ChatHandler::HandleCharacterEraseCommand(const char* args)
{
    if (!*args)
        return false;

    char *character_name_str = strtok((char*)args," ");
    if (!character_name_str)
        return false;

    std::string character_name = character_name_str;
    if (!normalizePlayerName(character_name))
        return false;

    uint64 character_guid;
    uint32 account_id;

    Player *player = objmgr.GetPlayer(character_name.c_str());
    if (player)
    {
        character_guid = player->GetGUID();
        account_id = player->GetSession()->GetAccountId();
        player->GetSession()->KickPlayer();
    }
    else
    {
        character_guid = objmgr.GetPlayerGUIDByName(character_name);
        if (!character_guid)
        {
            PSendSysMessage(LANG_NO_PLAYER,character_name.c_str());
            SetSentErrorMessage(true);
            return false;
        }

        account_id = objmgr.GetPlayerAccountIdByGUID(character_guid);
    }

    std::string account_name;
    sAccountMgr->GetName (account_id,account_name);

    Player::DeleteFromDB(character_guid, account_id, true);
    PSendSysMessage(LANG_CHARACTER_DELETED,character_name.c_str(),GUID_LOPART(character_guid),account_name.c_str(), account_id);
    return true;
}

bool ChatHandler::HandleServerExitCommand(const char* /*args*/)
{
    SendSysMessage(LANG_COMMAND_EXIT);
    World::StopNow(SHUTDOWN_EXIT_CODE);
    return true;
}

bool ChatHandler::HandleAccountOnlineListCommand(const char* /*args*/)
{
    QueryResult_AutoPtr resultDB = CharacterDatabase.Query("SELECT name,account FROM characters WHERE online > 0");
    if (!resultDB)
        return true;

    SendSysMessage("=====================================================================");
    SendSysMessage(LANG_ACCOUNT_LIST_HEADER);
    SendSysMessage("=====================================================================");

    do
    {
        Field *fieldsDB = resultDB->Fetch();
        std::string name = fieldsDB[0].GetCppString();
        uint32 account = fieldsDB[1].GetUInt32();

        QueryResult_AutoPtr resultLogin = 
            LoginDatabase.PQuery("SELECT a.username, a.last_ip, aa.gmlevel, a.expansion "
                                 "FROM account a "
                                 "LEFT JOIN account_access aa "
                                 "ON (a.id = aa.id) "
                                 "WHERE a.id = '%u'", account);
        if (resultLogin)
        {
            Field *fieldsLogin = resultLogin->Fetch();
            PSendSysMessage("|%15s| %20s | %15s |%4d|%5d|",
                fieldsLogin[0].GetString(),name.c_str(),fieldsLogin[1].GetString(),fieldsLogin[2].GetUInt32(),fieldsLogin[3].GetUInt32());
        }
        else
            PSendSysMessage(LANG_ACCOUNT_LIST_ERROR,name.c_str());

    }while(resultDB->NextRow());

    SendSysMessage("=====================================================================");
    return true;
}

bool ChatHandler::HandleAccountCreateCommand(const char* args)
{
    if (!*args)
        return false;

    char *szAcc = strtok((char*)args, " ");
    char *szPassword = strtok(NULL, " ");
    if (!szAcc || !szPassword)
        return false;

    std::string account_name = szAcc;
    std::string password = szPassword;

    AccountOpResult result = sAccountMgr->CreateAccount(account_name, password);
    switch(result)
    {
        case AOR_OK:
            PSendSysMessage(LANG_ACCOUNT_CREATED,account_name.c_str());
            break;
        case AOR_NAME_TOO_LONG:
            SendSysMessage(LANG_ACCOUNT_TOO_LONG);
            SetSentErrorMessage(true);
            return false;
        case AOR_NAME_ALREDY_EXIST:
            SendSysMessage(LANG_ACCOUNT_ALREADY_EXIST);
            SetSentErrorMessage(true);
            return false;
        case AOR_DB_INTERNAL_ERROR:
            PSendSysMessage(LANG_ACCOUNT_NOT_CREATED_SQL_ERROR,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        default:
            PSendSysMessage(LANG_ACCOUNT_NOT_CREATED,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
    }

    return true;
}

bool ChatHandler::HandleServerSetLogLevelCommand(const char *args)
{
    if (!*args)
        return false;

    char *NewLevel = strtok((char*)args, " ");
    if (!NewLevel)
        return false;

    sLog.SetLogLevel(NewLevel);
    return true;
}

bool ChatHandler::HandleServerSetDiffTimeCommand(const char *args)
{
    if (!*args)
        return false;

    char *NewTimeStr = strtok((char*)args, " ");
    if (!NewTimeStr)
        return false;

    int32 NewTime =atoi(NewTimeStr);
    if (NewTime < 0)
        return false;

    sWorld.SetRecordDiffInterval(NewTime);
    printf( "Record diff every %u ms\n", NewTime);
    return true;
}

#ifdef linux
int kb_hit_return()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}
#endif

void CliRunnable::run()
{
    WorldDatabase.ThreadStart();
    sLog.outString();
    #if PLATFORM != WINDOWS
    rl_attempted_completion_function = cli_completion;
    #endif
    if (sConfig.GetBoolDefault("BeepAtStart", true))
        printf("\a");

    #if PLATFORM == PLATFORM_WINDOWS
    char commandbuf[256];
    #endif
    printf("AC>");

    while (!World::IsStopped())
    {
        fflush(stdout);

        char *command_str;

        #if PLATFORM == WINDOWS
        command_str = fgets(commandbuf,sizeof(commandbuf),stdin);
        #else
        command_str = readline("AC>");
        rl_bind_key('\t',rl_complete);
        #endif
        if (command_str != NULL)
        {
            for (int x=0; command_str[x]; x++)
                if (command_str[x]=='\r'||command_str[x]=='\n')
                {
                    command_str[x]=0;
                    break;
                }

            if (!*command_str)
            {
                #if PLATFORM == WINDOWS
                printf("AC>");
                #endif
                continue;
            }

            std::string command;
            if (!consoleToUtf8(command_str,command))
            {
                #if PLATFORM == WINDOWS
                printf("AC>");
                #endif
                continue;
            }
            fflush(stdout);
            sWorld.QueueCliCommand(new CliCommandHolder(NULL, command.c_str(), &utf8print, &commandFinished));
            #if PLATFORM != WINDOWS
            add_history(command.c_str());
            #endif

        }
        else if (feof(stdin))
        {
            World::StopNow(SHUTDOWN_EXIT_CODE);
        }
    }
    WorldDatabase.ThreadEnd();
}

