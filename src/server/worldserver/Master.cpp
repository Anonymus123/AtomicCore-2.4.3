/*
 *    Copyright (C) 2010-2013 AtomicCore <http://www.frozen-kingdom.eu/>
 *
 *    AtomicCore is based on TrinityCore, MythCore, ChaosCore and other
 *    WoW emulations. Only authorized persons get access to our repositories.
 *    Sharing of our code is not allowed!
 */

#include <ace/OS_NS_signal.h>
#include "WorldSocketMgr.h"
#include "Common.h"
#include "Master.h"
#include "WorldSocket.h"
#include "WorldRunnable.h"
#include "World.h"
#include "Log.h"
#include "Timer.h"
#include "SingletonImp.h"
#include "SystemConfig.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "CliRunnable.h"
#include "RARunnable.h"
#include "Util.h"
#include "TCSoap.h"

#ifdef _WIN32
#include "ServiceWin32.h"
extern int m_ServiceStatus;
#endif

INSTANTIATE_SINGLETON_1(Master);

volatile uint32 Master::m_masterLoopCounter = 0;

class FreezeDetectorRunnable : public ACE_Based::Runnable
{
public:
    FreezeDetectorRunnable() { _delaytime = 0; }
    uint32 m_loops, m_lastchange;
    uint32 w_loops, w_lastchange;
    uint32 _delaytime;
    void SetDelayTime(uint32 t) { _delaytime = t; }
    void run(void)
    {
        if (!_delaytime)
            return;
        sLog.outString("Starting up anti-freeze thread (%u seconds max stuck time)...",_delaytime/1000);
        m_loops = 0;
        w_loops = 0;
        m_lastchange = 0;
        w_lastchange = 0;
        while (!World::IsStopped())
        {
            ACE_Based::Thread::Sleep(1000);
            uint32 curtime = getMSTime();
            if (w_loops != World::m_worldLoopCounter)
            {
                w_lastchange = curtime;
                w_loops = World::m_worldLoopCounter;
            }
            else if (getMSTimeDiff(w_lastchange,curtime) > _delaytime)
            {
                sLog.outError("World Thread is stuck.  Terminating server!");
                *((uint32 volatile*)NULL) = 0;
            }
        }
        sLog.outString("Anti-freeze thread exiting without problems.");
    }
};

Master::Master()
{
}

Master::~Master()
{
}

int Master::Run()
{
	sLog.outString("   ___     _                   _         ___                   ");
    sLog.outString("  / _ \\   | |_   ___   _ __   (_)  __   / __|  ___   _ _   ___ ");
    sLog.outString(" / /_\\ \\  |  _| / _ \\ | '  \\  | | / _| | (__  / _ \\ | '_| / -_)");
    sLog.outString("/_/   \\_\\  \\__| \\___/ |_|_|_| |_| \\__|  \\___| \\___/ |_|   \\___|");
    sLog.outString("");

    std::string pidfile = sConfig.GetStringDefault("PidFile", "");
    if (!pidfile.empty())
    {
        uint32 pid = CreatePIDFile(pidfile);
        if (!pid)
        {
            sLog.outError("Cannot create PID file %s.\n", pidfile.c_str());
            return 1;
        }

        sLog.outString("Daemon PID: %u\n", pid);
    }

    if (!_StartDB())
        return 1;

    sWorld.SetInitialWorldSettings();
    _HookSignals();
    ACE_Based::Thread world_thread(new WorldRunnable);
    world_thread.setPriority(ACE_Based::Highest);
    std::string builds = AcceptableClientBuildsListStr();
    LoginDatabase.escape_string(builds);
    LoginDatabase.PExecute("UPDATE realmlist SET flag = flag & ~(%u), population = 0, gamebuild = '%s'  WHERE id = '%d'", REALM_FLAG_OFFLINE, builds.c_str(), realmID);

    ACE_Based::Thread* cliThread = NULL;

#ifdef _WIN32
    if (sConfig.GetBoolDefault("Console.Enable", true) && (m_ServiceStatus == -1))
#else
    if (sConfig.GetBoolDefault("Console.Enable", true))
#endif
    {
        cliThread = new ACE_Based::Thread(new CliRunnable);
    }

    ACE_Based::Thread rar_thread(new RARunnable);

    #ifdef _WIN32
    {
        HANDLE hProcess = GetCurrentProcess();

        uint32 Aff = sConfig.GetIntDefault("UseProcessors", 0);
        if (Aff > 0)
        {
            ULONG_PTR appAff;
            ULONG_PTR sysAff;

            if (GetProcessAffinityMask(hProcess,&appAff,&sysAff))
            {
                ULONG_PTR curAff = Aff & appAff;

                if (!curAff)
                {
                    sLog.outError("Processors marked in UseProcessors bitmask (hex) %x not accessible for OregonCore. Accessible processors bitmask (hex): %x",Aff,appAff);
                }
                else
                {
                    if (SetProcessAffinityMask(hProcess,curAff))
                        sLog.outString("Using processors (bitmask, hex): %x", curAff);
                    else
                        sLog.outError("Can't set used processors (hex): %x",curAff);
                }
            }
            sLog.outString();
        }

        bool Prio = sConfig.GetBoolDefault("ProcessPriority", false);

        if (Prio)
        {
            if (SetPriorityClass(hProcess,HIGH_PRIORITY_CLASS))
                sLog.outString("OregonCore process priority class set to HIGH");
            else
                sLog.outError("ERROR: Can't set OregonCore process priority class.");
            sLog.outString();
        }
    }
    #endif

    ACE_Based::Thread* soap_thread = NULL;

    if (sConfig.GetBoolDefault("SOAP.Enabled", false))
    {
        OCSoapRunnable* soapconnectector = new OCSoapRunnable();
        soapconnectector->setListenArguments(sConfig.GetStringDefault("SOAP.IP", "127.0.0.1"), uint16(sConfig.GetIntDefault("SOAP.Port", 7878)));
        soap_thread = new ACE_Based::Thread(soapconnectector);
    }
    ACE_Based::Thread* freeze_thread = NULL;
    if (uint32 freeze_delay = sConfig.GetIntDefault("MaxCoreStuckTime", 0))
    {
        FreezeDetectorRunnable *fdr = new FreezeDetectorRunnable();
        fdr->SetDelayTime(freeze_delay*1000);
        freeze_thread = new ACE_Based::Thread(fdr);
        freeze_thread->setPriority(ACE_Based::Highest);
    }
	
    uint16 wsport = sWorld.getConfig(CONFIG_PORT_WORLD);
    std::string bind_ip = sConfig.GetStringDefault ("BindIP", "0.0.0.0");

    if (sWorldSocketMgr->StartNetwork (wsport, bind_ip.c_str ()) == -1)
    {
        sLog.outError("Failed to start network");
        World::StopNow(ERROR_EXIT_CODE);
    }

    sWorldSocketMgr->Wait();

    if (freeze_thread)
    {
        freeze_thread->destroy();
        delete freeze_thread;
    }

    if (soap_thread)
    {
        soap_thread->wait();
        soap_thread->destroy();
        delete soap_thread;
    }
    LoginDatabase.PExecute("UPDATE realmlist SET flag = flag | %u WHERE id = '%d'", REALM_FLAG_OFFLINE, realmID);
    _UnhookSignals();
    world_thread.wait();
    rar_thread.wait ();
    clearOnlineAccounts();
    CharacterDatabase.HaltDelayThread();
    WorldDatabase.HaltDelayThread();
    LoginDatabase.HaltDelayThread();

    sLog.outString("Halting process...");

    if (cliThread)
    {
        #ifdef _WIN32

        INPUT_RECORD b[5];
        HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
        b[0].EventType = KEY_EVENT;
        b[0].Event.KeyEvent.bKeyDown = TRUE;
        b[0].Event.KeyEvent.uChar.AsciiChar = 'X';
        b[0].Event.KeyEvent.wVirtualKeyCode = 'X';
        b[0].Event.KeyEvent.wRepeatCount = 1;

        b[1].EventType = KEY_EVENT;
        b[1].Event.KeyEvent.bKeyDown = FALSE;
        b[1].Event.KeyEvent.uChar.AsciiChar = 'X';
        b[1].Event.KeyEvent.wVirtualKeyCode = 'X';
        b[1].Event.KeyEvent.wRepeatCount = 1;

        b[2].EventType = KEY_EVENT;
        b[2].Event.KeyEvent.bKeyDown = TRUE;
        b[2].Event.KeyEvent.dwControlKeyState = 0;
        b[2].Event.KeyEvent.uChar.AsciiChar = '\r';
        b[2].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        b[2].Event.KeyEvent.wRepeatCount = 1;
        b[2].Event.KeyEvent.wVirtualScanCode = 0x1c;

        b[3].EventType = KEY_EVENT;
        b[3].Event.KeyEvent.bKeyDown = FALSE;
        b[3].Event.KeyEvent.dwControlKeyState = 0;
        b[3].Event.KeyEvent.uChar.AsciiChar = '\r';
        b[3].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        b[3].Event.KeyEvent.wVirtualScanCode = 0x1c;
        b[3].Event.KeyEvent.wRepeatCount = 1;
        DWORD numb;
        WriteConsoleInput(hStdIn, b, 4, &numb);

        cliThread->wait();

        #else

        cliThread->destroy();

        #endif

        delete cliThread;
    }
    return World::GetExitCode();
}

bool Master::_StartDB()
{
    sLog.SetLogDB(false);
    std::string dbstring = sConfig.GetStringDefault("WorldDatabaseInfo", "");
    if (dbstring.empty())
    {
        sLog.outError("World database not specified in configuration file");
        return false;
    }

    if (!WorldDatabase.Initialize(dbstring.c_str()))
    {
        sLog.outError("Cannot connect to world database %s",dbstring.c_str());
        return false;
    }

    dbstring = sConfig.GetStringDefault("CharacterDatabaseInfo", "");
    if (dbstring.empty())
    {
        sLog.outError("Character database not specified in configuration file");
        return false;
    }

    if (!CharacterDatabase.Initialize(dbstring.c_str()))
    {
        sLog.outError("Cannot connect to Character database %s",dbstring.c_str());
        return false;
    }

    dbstring = sConfig.GetStringDefault("LoginDatabaseInfo", "");
    if (dbstring.empty())
    {
        sLog.outError("Login database not specified in configuration file");
        return false;
    }

    if (!LoginDatabase.Initialize(dbstring.c_str()))
    {
        sLog.outError("Cannot connect to login database %s",dbstring.c_str());
        return false;
    }

    realmID = sConfig.GetIntDefault("RealmID", 0);
    if (!realmID)
    {
        sLog.outError("Realm ID not defined in configuration file");
        return false;
    }
    sLog.outString("Realm running as realm ID %d", realmID);
    sLog.SetLogDBLater(sConfig.GetBoolDefault("EnableLogDB", false));
    sLog.SetLogDB(false);
    sLog.SetRealmID(realmID);
    clearOnlineAccounts();
    WorldDatabase.PExecute("UPDATE version SET core_version = '%s', core_revision = '%s'", _FULLVERSION, _REVISION);
    sWorld.LoadDBVersion();
    sLog.outString("Using World DB: %s", sWorld.GetDBVersion());
    return true;
}

void Master::clearOnlineAccounts()
{
    //LoginDatabase.PExecute("UPDATE account SET active_realm_id = 0 WHERE active_realm_id = '%d'", realmID);
    CharacterDatabase.Execute("UPDATE characters SET online = 0 WHERE online<>0");
}

void Master::_OnSignal(int s)
{
    switch (s)
    {
        case SIGINT:
            World::StopNow(RESTART_EXIT_CODE);
            break;
        case SIGTERM:
        #ifdef _WIN32
        case SIGBREAK:
        #endif
            World::StopNow(SHUTDOWN_EXIT_CODE);
            break;
    }

    signal(s, _OnSignal);
}

void Master::_HookSignals()
{
    signal(SIGINT, _OnSignal);
    signal(SIGTERM, _OnSignal);
    #ifdef _WIN32
    signal(SIGBREAK, _OnSignal);
    #endif
}

void Master::_UnhookSignals()
{
    signal(SIGINT, 0);
    signal(SIGTERM, 0);
    #ifdef _WIN32
    signal(SIGBREAK, 0);
    #endif
}