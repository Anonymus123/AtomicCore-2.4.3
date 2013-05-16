/**
 *    Copyright (C) 2010-2013 AtomicCore <http://www.frozen-kingdom.eu/>
 *
 *    AtomicCore is based on TrinityCore, MythCore, ChaosCore and own
 *    WoW emulations. Only authorized persons get access to our
 *    repositories. Sharing of our code is not allowed!
 */

#ifndef _OREGONSOAP_H
#define _OREGONSOAP_H

#include "Common.h"
#include "World.h"
#include "AccountMgr.h"
#include "Log.h"
#include "soapH.h"
#include "soapStub.h"

#include <ace/Semaphore.h>
#include <ace/Task.h>


class OCSoapRunnable: public ACE_Based::Runnable
{
    public:
        OCSoapRunnable() { }
        void run();
        void setListenArguments(std::string host, uint16 port)
        {
            m_host = host;
            m_port = port;
        }
    private:
        std::string m_host;
        uint16 m_port;
};

class SOAPWorkingThread : public ACE_Task<ACE_MT_SYNCH>
{
    public:
        SOAPWorkingThread ()
        { }

        virtual int svc (void)
        {
            while (1)
            {
                ACE_Message_Block *mb = 0;
                if (this->getq (mb) == -1)
                {
                    ACE_DEBUG ((LM_INFO,
                                ACE_TEXT ("(%t) Shutting down\n")));
                    break;
                }
                process_message (mb);
            }

            return 0;
        }
    private:
        void process_message (ACE_Message_Block *mb);
};


class SOAPCommand
{
    public:
        SOAPCommand():
            pendingCommands(0, USYNC_THREAD, "pendingCommands")
        {

        }
        ~SOAPCommand()
        {
        }

        void appendToPrintBuffer(const char* msg)
        {
            m_printBuffer += msg;
        }

        ACE_Semaphore pendingCommands;

        void setCommandSuccess(bool val)
        {
            m_success = val;
        }
        bool hasCommandSucceeded()
        {
            return m_success;
        }

        static void print(void* callbackArg, const char* msg)
        {
            ((SOAPCommand*)callbackArg)->appendToPrintBuffer(msg);
        }

        static void commandFinished(void* callbackArg, bool success);

        bool m_success;
        std::string m_printBuffer;
};

#endif
