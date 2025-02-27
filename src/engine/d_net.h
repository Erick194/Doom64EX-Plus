// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1997 Id Software, Inc.
// Copyright(C) 2005 Simon Howard
// Copyright(C) 2007-2012 Samuel Villarreal
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
//-----------------------------------------------------------------------------

#ifndef __D_NET__
#define __D_NET__

#include "d_player.h"

#include "net_client.h"
#include "net_io.h"
#include "net_query.h"
#include "net_server.h"
#include "net_loop.h"

#ifdef __GNUG__
#pragma interface
#endif

#define MAXNETNODES        8    // Max computers/players in a game.
#define BACKUPTICS        128    // Networking and tick handling related.


// Create any new ticcmds and broadcast to other players.
void NetUpdate(void);

// Broadcasts special packets to other players
//  to notify of game exit
void D_QuitNetGame(void);

extern dboolean ShowGun;
extern dboolean drone;
extern dboolean    net_cl_new_sync;

#endif

