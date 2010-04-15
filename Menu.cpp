// win2vnc, adapted from vncviewer by Fredrik Hubinette 2001
//
// Original copyright follows:
//
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

// Implementation of a system tray icon & menu for WinVNC

#include "stdhdrs.h"
#include "vncviewer.h"
#include "omnithread.h"
#include <lmcons.h>

// Header

#include "Menu.h"
#include "AboutBox.h"

const char *MENU_CLASS_NAME = "win2vnc Tray Icon";

// Implementation

vncMenu::vncMenu(VNCviewerApp *app, ClientConnection *c)
{
  m_pApp=app;
  con=c;

  // Create a dummy window to handle tray icon messages
  WNDCLASSEX wndclass;

  wndclass.cbSize		= sizeof(wndclass);
  wndclass.style		= 0;
  wndclass.lpfnWndProc	= vncMenu::WndProc;
  wndclass.cbClsExtra		= 0;
  wndclass.cbWndExtra		= 0;
  wndclass.hInstance		= m_pApp->m_instance;
  wndclass.hIcon		= LoadIcon(NULL, IDI_APPLICATION);
  wndclass.hCursor		= LoadCursor(NULL, IDC_ARROW);
  wndclass.hbrBackground	= (HBRUSH) GetStockObject(WHITE_BRUSH);
  wndclass.lpszMenuName	= (const char *) NULL;
  wndclass.lpszClassName	= MENU_CLASS_NAME;
  wndclass.hIconSm		= LoadIcon(NULL, IDI_APPLICATION);
  
  RegisterClassEx(&wndclass);
  
  m_hwnd = CreateWindow(MENU_CLASS_NAME,
			MENU_CLASS_NAME,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			200, 200,
			NULL,
			NULL,
			m_pApp->m_instance,
			NULL);
  if (m_hwnd == NULL)
  {
    PostMessage(con->m_hwnd, WM_CLOSE, 0, 0);
    return;
  }
  
  // record which client created this window
  SetWindowLong(m_hwnd, GWL_USERDATA, (LONG) this);
  
  // Load the icons for the tray
  m_winvnc_icon = LoadIcon(m_pApp->m_instance, MAKEINTRESOURCE(IDI_MAINICON));
  
  // Load the popup menu
  m_hmenu = LoadMenu(m_pApp->m_instance, MAKEINTRESOURCE(IDR_TRAYMENU));
  
  // Install the tray icon!
  AddTrayIcon();
}

vncMenu::~vncMenu()
{
  // Remove the tray icon
  DelTrayIcon();
  
  // Destroy the loaded menu
  if (m_hmenu != NULL)
  {
    DestroyMenu(m_hmenu);
    m_hmenu=0;
  }

  if(m_hwnd != NULL)
  {
    DestroyWindow(m_hwnd);
    m_hwnd=NULL;
  }
}

void vncMenu::AddTrayIcon()
{
  // If the user name is non-null then we have a user!
  SendTrayMsg(NIM_ADD);
}

void vncMenu::DelTrayIcon()
{
  SendTrayMsg(NIM_DELETE);
}


void vncMenu::SendTrayMsg(DWORD msg)
{
  // Create the tray icon message
#ifdef NOTIFYICONDATA_V1_SIZE
  m_nid.cbSize = NOTIFYICONDATA_V1_SIZE;
#else
  m_nid.cbSize = sizeof(m_nid);
#endif
  m_nid.hWnd = m_hwnd;
  m_nid.uID = 1;
  m_nid.hIcon = m_winvnc_icon;
  m_nid.uFlags = NIF_ICON | NIF_MESSAGE;
  m_nid.uCallbackMessage = WM_TRAYNOTIFY;
  
  // Send the message
  Shell_NotifyIcon(msg, &m_nid);
  /* log failure */
}

// Process window messages
LRESULT CALLBACK vncMenu::WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
  // This is a static method, so we don't know which instantiation we're 
  // dealing with. We use Allen Hadden's (ahadden@taratec.com) suggestion 
  // from a newsgroup to get the pseudo-this.

  vncMenu *_this = (vncMenu *) GetWindowLong(hwnd, GWL_USERDATA);
  if(_this)
  {
    return _this->RealWndProc(hwnd, iMsg, wParam, lParam);
  }else{
    return DefWindowProc(hwnd, iMsg, wParam, lParam);
  }
}

int vncMenu::RealWndProc(HWND hwnd, UINT iMsg, 
			 WPARAM wParam, LPARAM lParam)
{
  switch (iMsg)
  {
      // STANDARD MESSAGE HANDLING
    case WM_CREATE:
      return 0;
      
    case WM_COMMAND:
      // User has clicked an item on the tray menu
    {
      log.Print(0, _T("TRAY MENU ITEM %d activated\n"),LOWORD(wParam));
      switch (LOWORD(wParam)) {
	case SC_MINIMIZE:
	  con->SetDormant(true);
	  break;
	case SC_RESTORE:
	  con->SetDormant(false);
	  break;
	case ID_NEWCONN:
	  m_pApp->NewConnection();
	  return 0;
	case ID_CONN_SAVE_AS:
	  con->SaveConnection();
	  return 0;
	case IDC_OPTIONBUTTON: 
	  if (con->m_opts.DoDialog(true)) {
	    con->m_pendingFormatChange = true;
	    con->m_onedge=0;
	    con->MoveWindowToEdge();
	  };	
	  return 0;

	case IDD_APP_ABOUT:
	  ShowAboutBox();
	  return 0;
	case ID_CONN_ABOUT:
	  con->ShowConnInfo();
	  return 0;
	  
	case ID_CONN_CTLALTDEL:
	  log.Print(0, _T("Sending Ctrl-Alt-Del\n"));
	  con->SendKeyEvent(XK_Control_L, true);
	  con->SendKeyEvent(XK_Alt_L,     true);
	  con->SendKeyEvent(XK_Delete,    true);
	  con->SendKeyEvent(XK_Delete,    false);
	  con->SendKeyEvent(XK_Alt_L,     false);
	  con->SendKeyEvent(XK_Control_L, false);
	  return 0;
	case ID_CONN_CTLDOWN:
	  con->SendKeyEvent(XK_Control_L, true);
	  return 0;
	case ID_CONN_CTLUP:
	  con->SendKeyEvent(XK_Control_L, false);
	  return 0;
	case ID_CONN_ALTDOWN:
	  con->SendKeyEvent(XK_Alt_L, true);
	  return 0;
	case ID_CONN_ALTUP:
	  con->SendKeyEvent(XK_Alt_L, false);
	  return 0;

	case IDD_APP_EXIT:
	  if (MessageBox(NULL, _T("Are you sure you want to exit?"), 
			 _T("Closing win2vnc"), 
			 MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
	  {
	    DelTrayIcon();
	    PostMessage(con->m_hwnd, WM_CLOSE, 0, 0);
	  }
	  return 0;
      }
      break;
    }
    
    return 0;
      
    case WM_TRAYNOTIFY:
      // User has clicked on the tray icon or the menu
    {
      // Get the submenu to use as a pop-up menu
      HMENU submenu = GetSubMenu(m_hmenu, 0);
      
      // What event are we responding to, RMB click?
      if (lParam==WM_RBUTTONUP)
      {
	if (submenu == NULL)
	{ 
	  log.Print(1,_T("no submenu available\n"));
	  return 0;
	}
	
	// Make the first menu item the default (bold font)
	SetMenuDefaultItem(submenu, 0, TRUE);
	
      	// Get the current cursor position, to display the menu at
	POINT mouse;
	GetCursorPos(&mouse);
	
        // There's a "bug"
        // (Microsoft calls it a feature) in Windows 95 that requires calling
        // SetForegroundWindow. To find out more, search for Q135788 in MSDN.
        //
	SetForegroundWindow(m_nid.hWnd);

        // Display the menu at the desired position
	TrackPopupMenu(submenu,
		       0, mouse.x, mouse.y, 0,
		       m_nid.hWnd, NULL);
	
	return 0;
      }
      
      // Or was there a LMB double click?
      if (lParam==WM_LBUTTONDBLCLK)
      {
				// double click: execute first menu item
	SendMessage(m_nid.hWnd,
		    WM_COMMAND, 
		    GetMenuItemID(submenu, 0),
		    0);
      }
      
      return 0;
    }
    
    case WM_CLOSE:
      break;
      
    case WM_DESTROY:
      PostMessage(con->m_hwnd, WM_CLOSE, 0, 0);
      return 0;
      
  }
  
  // Message not recognised
  return DefWindowProc(hwnd, iMsg, wParam, lParam);
}
