/**
 *    Copyright (C) 2010-2013 AtomicCore <http://www.frozen-kingdom.eu/>
 *
 *    AtomicCore is based on TrinityCore, MythCore, ChaosCore and own
 *    WoW emulations. Only authorized persons get access to our
 *    repositories. Sharing of our code is not allowed!
 */

#ifndef _MASTER_H
#define _MASTER_H

#include "Common.h"
#include "Singleton.h"

class Master
{
    public:
        Master();
        ~Master();
        int Run();
        static volatile uint32 m_masterLoopCounter;

    private:
        bool _StartDB();

        void _HookSignals();
        void _UnhookSignals();
        static void _OnSignal(int s);

        void clearOnlineAccounts();
};

#define sMaster Oregon::Singleton<Master>::Instance()
#endif

