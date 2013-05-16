/**
 *    Copyright (C) 2010-2013 AtomicCore <http://www.frozen-kingdom.eu/>
 *
 *    AtomicCore is based on TrinityCore, MythCore, ChaosCore and own
 *    WoW emulations. Only authorized persons get access to our
 *    repositories. Sharing of our code is not allowed!
 */

#ifndef __CLIRUNNABLE_H
#define __CLIRUNNABLE_H

class CliRunnable : public ACE_Based::Runnable
{
    public:
        void run();
};
#endif

