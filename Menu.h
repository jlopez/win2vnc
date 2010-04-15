//  Copyright (C) 1999 AT&T Laboratories Cambridge. All Rights Reserved.
//
//  This file is part of the VNC system.
//
//  The VNC system is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
// If the source code for the VNC system is not available from the place 
// whence you received this file, check http://www.uk.research.att.com/vnc or contact
// the authors on vnc@uk.research.att.com for information on obtaining it.


// vncMenu
// This class handles creation of a system-tray icon & menu

#pragma once

class vncMenu;

#if (!defined(_WINVNC_VNCMENU))
#define _WINVNC_VNCMENU

#include "stdhdrs.h"
#include <lmcons.h>
#include "vncviewer.h"
#include "ClientConnection.h"

// Constants
extern const char *MENU_CLASS_NAME;

// The tray menu class itself
class vncMenu
{
public:
  vncMenu(VNCviewerApp *,ClientConnection *);
  ~vncMenu();
 protected:
  
  // Tray icon handling
  void AddTrayIcon();
  void DelTrayIcon();
  void SendTrayMsg(DWORD);
  
  // Message handler for the tray window
  int RealWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
  
  // Fields
 protected:
  // Check that the password has been set
  void CheckPassword();
  
  ClientConnection      *con;
  VNCviewerApp *m_pApp;
  
  HWND			m_hwnd;
  HMENU			m_hmenu;
  NOTIFYICONDATA	m_nid;
  
  char			m_username[UNLEN+1];
  
  // The icon handles
  HICON			m_winvnc_icon;
};


#endif
