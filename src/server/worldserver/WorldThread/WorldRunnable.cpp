/**
 *    Copyright (C) 2010-2013 AtomicCore <http://www.frozen-kingdom.eu/>
 *
 *    AtomicCore is based on TrinityCore, MythCore, ChaosCore and own
 *    WoW emulations. Only authorized persons get access to our
 *    repositories. Sharing of our code is not allowed!
 */
#include "WorldSocketMgr.h"
#include "Common.h"
#include "World.h"
#include "WorldRunnable.h"
#include "Timer.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "BattleGroundMgr.h"
#include "DatabaseEnv.h"

#define WORLD_SLEEP_CONST 50

void WorldRunnable::run()
{
    WorldDatabase.ThreadStart();
    sWorld.InitResultQueue();
    uint32 realCurrTime = 0;
    uint32 realPrevTime = getMSTime();
    uint32 prevSleepTime = 0;

    while (!World::IsStopped())
    {
        ++World::m_worldLoopCounter;
        realCurrTime = getMSTime();
        uint32 diff = getMSTimeDiff(realPrevTime,realCurrTime);
        sWorld.Update(diff);
        realPrevTime = realCurrTime;

        if (diff <= WORLD_SLEEP_CONST+prevSleepTime)
        {
            prevSleepTime = WORLD_SLEEP_CONST+prevSleepTime-diff;
            ACE_Based::Thread::Sleep(prevSleepTime);
        }
        else
            prevSleepTime = 0;
    }
    sWorld.KickAll();
    sWorld.UpdateSessions( 1 );
    sBattleGroundMgr.DeleteAlllBattleGrounds();
    sWorldSocketMgr->StopNetwork();
    MapManager::Instance().UnloadAll();
    WorldDatabase.ThreadEnd();
}

