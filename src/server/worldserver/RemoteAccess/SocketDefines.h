/**
 *    Copyright (C) 2010-2013 AtomicCore <http://www.frozen-kingdom.eu/>
 *
 *    AtomicCore is based on TrinityCore, MythCore, ChaosCore and own
 *    WoW emulations. Only authorized persons get access to our
 *    repositories. Sharing of our code is not allowed!
 */

#ifndef OREGON_SOCKETDEFINES_H
#define OREGON_SOCKETDEFINES_H

#ifdef WIN32

#define FD_SETSIZE 1024
#include <winsock2.h>
#include <Ws2tcpip.h>

typedef SOCKET SocketHandle;
typedef fd_set SelectSet;

#else

#include <sys/socket.h>
#include <netinet/in.h>
#ifdef __APPLE_CC__
#include <sys/select.h>
#endif

typedef int SocketHandle;
typedef fd_set SelectSet;
#endif
#endif

