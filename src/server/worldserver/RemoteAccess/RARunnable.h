/**
 *    Copyright (C) 2010-2013 AtomicCore <http://www.frozen-kingdom.eu/>
 *
 *    AtomicCore is based on TrinityCore, MythCore, ChaosCore and own
 *    WoW emulations. Only authorized persons get access to our
 *    repositories. Sharing of our code is not allowed!
 */

#ifndef _OREGON_RARUNNABLE_H_
#define _OREGON_RARUNNABLE_H_

#include "Common.h"

#include <ace/Reactor.h>

class RARunnable : public ACE_Based::Runnable
{
public:
    RARunnable();
    virtual ~RARunnable();
    void run();

private:
    ACE_Reactor* m_Reactor;

};

#endif