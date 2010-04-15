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


// VNCOptions.cpp: implementation of the VNCOptions class.

#include "stdhdrs.h"
#include "vncviewer.h"
#include "VNCOptions.h"
#include "Exception.h"

VNCOptions::VNCOptions()
{
  for (int i = rfbEncodingRaw; i<= LASTENCODING; i++)
    m_UseEnc[i] = true;
	
  m_PreferredEncoding = rfbEncodingHextile;
  m_SwapMouse = false;
  m_Emul3Buttons = true;
  m_Emul3Timeout = 100; // milliseconds
  m_Emul3Fuzz = 4;      // pixels away before emulation is cancelled
  m_Shared = false;
  m_DisableClipboard = false;
	
  m_host[0] = '\0';
  m_port = -1;
	
  m_kbdname[0] = '\0';
  m_kbdSpecified = false;
	
  m_logLevel = 0;
  m_logToConsole = false;
  m_logToFile = false;
  m_logFilename[0] = '\0';
	
  m_delay=0;
  m_connectionSpecified = false;
  m_configSpecified = false;
  m_configFilename[0] = '\0';
  m_listening = false;
  m_edge = M_EDGE_EAST;
}

VNCOptions& VNCOptions::operator=(VNCOptions& s)
{
  for (int i = rfbEncodingRaw; i<= LASTENCODING; i++)
    m_UseEnc[i] = s.m_UseEnc[i];

  m_PreferredEncoding = s.m_PreferredEncoding;
  m_SwapMouse			= s.m_SwapMouse;
  m_Emul3Buttons		= s.m_Emul3Buttons;
  m_Emul3Timeout		= s.m_Emul3Timeout;
  m_Emul3Fuzz			= s.m_Emul3Fuzz;      // pixels away before emulation is cancelled
  m_Shared			= s.m_Shared;
  m_DisableClipboard  = s.m_DisableClipboard;
	
  strcpy(m_host, s.m_host);
  m_port				= s.m_port;
  m_edge  = s.m_edge;
	
  strcpy(m_kbdname, s.m_kbdname);
  m_kbdSpecified		= s.m_kbdSpecified;
	
  m_logLevel			= s.m_logLevel;
  m_logToConsole		= s.m_logToConsole;
  m_logToFile			= s.m_logToFile;
  strcpy(m_logFilename, s.m_logFilename);

  m_delay				= s.m_delay;
  m_connectionSpecified = s.m_connectionSpecified;
  m_configSpecified   = s.m_configSpecified;
  strcpy(m_configFilename, s.m_configFilename);

  m_listening			= s.m_listening;

  return *this;
}

VNCOptions::~VNCOptions()
{
	
}

inline bool SwitchMatch(LPCTSTR arg, LPCTSTR swtch)
{
  return (arg[0] == '-' || arg[0] == '/') &&
    (_tcsicmp(&arg[1], swtch) == 0);
}

static void ArgError(LPTSTR msg)
{
  MessageBox(NULL,  msg, _T("Argument error"),MB_OK | MB_TOPMOST | MB_ICONSTOP);
}

void VNCOptions::SetFromCommandLine(LPTSTR szCmdLine)
{
  // We assume no quoting here.
  // Copy the command line - we don't know what might happen to the original
  int cmdlinelen = _tcslen(szCmdLine);
  if (cmdlinelen == 0) return;
	
  TCHAR *cmd = new TCHAR[cmdlinelen + 1];
  _tcscpy(cmd, szCmdLine);
  
  // Count the number of spaces
  // This may be more than the number of arguments, but that doesn't matter.
  int nspaces = 0;
  TCHAR *p = cmd;
  TCHAR *pos = cmd;
  while ( ( pos = _tcschr(p, ' ') ) != NULL ) {
    nspaces ++;
    p = pos + 1;
  }
  
  // Create the array to hold pointers to each bit of string
  TCHAR **args = new LPTSTR[nspaces + 1];
  
  // replace spaces with nulls and
  // create an array of TCHAR*'s which points to start of each bit.
  pos = cmd;
  int i = 0;
  args[i] = cmd;
  bool inquote=false;
  for (pos = cmd; *pos != 0; pos++) {
    // Arguments are normally separated by spaces, unless there's quoting
    if ((*pos == ' ') && !inquote) {
      *pos = '\0';
      p = pos + 1;
      args[++i] = p;
    }
    if (*pos == '"') {  
      if (!inquote) {      // Are we starting a quoted argument?
	args[i] = ++pos; // It starts just after the quote
      } else {
	*pos = '\0';     // Finish a quoted argument?
      }
      inquote = !inquote;
    }
  }
  i++;

  bool hostGiven = false, portGiven = false;
  // take in order.
  for (int j = 0; j < i; j++) {
    if ( SwitchMatch(args[j], _T("help")) ||
	 SwitchMatch(args[j], _T("?")) ||
	 SwitchMatch(args[j], _T("h"))) {
      ShowUsage();
      PostQuitMessage(1);
    } else if ( SwitchMatch(args[j], _T("east"))) {
      m_edge = M_EDGE_EAST;
    } else if ( SwitchMatch(args[j], _T("west"))) {
      m_edge = M_EDGE_WEST;
    } else if ( SwitchMatch(args[j], _T("south"))) {
      m_edge = M_EDGE_SOUTH;
    } else if ( SwitchMatch(args[j], _T("north"))) {
      m_edge = M_EDGE_NORTH;
    } else if ( SwitchMatch(args[j], _T("shared"))) {
      m_Shared = true;
    } else if ( SwitchMatch(args[j], _T("swapmouse"))) {
      m_SwapMouse = true;
    } else if ( SwitchMatch(args[j], _T("emulate3") )) {
      m_Emul3Buttons = true;
    } else if ( SwitchMatch(args[j], _T("noemulate3") )) {
      m_Emul3Buttons = false;
    } else if ( SwitchMatch(args[j], _T("emulate3timeout") )) {
      if (++j == i) {
	ArgError(_T("No timeout specified"));
	continue;
      }
      if (_stscanf(args[j], _T("%d"), &m_Emul3Timeout) != 1) {
	ArgError(_T("Invalid timeout specified"));
	continue;
      }
			
    } else if ( SwitchMatch(args[j], _T("emulate3fuzz") )) {
      if (++j == i) {
	ArgError(_T("No fuzz specified"));
	continue;
      }
      if (_stscanf(args[j], _T("%d"), &m_Emul3Fuzz) != 1) {
	ArgError(_T("Invalid fuzz specified"));
	continue;
      }
			
    } else if ( SwitchMatch(args[j], _T("disableclipboard") )) {
      m_DisableClipboard = true;
    }
    else if ( SwitchMatch(args[j], _T("delay") )) {
      if (++j == i) {
	ArgError(_T("No delay specified"));
	continue;
      }
      if (_stscanf(args[j], _T("%d"), &m_delay) != 1) {
	ArgError(_T("Invalid delay specified"));
	continue;
      }
			
    } else if ( SwitchMatch(args[j], _T("loglevel") )) {
      if (++j == i) {
	ArgError(_T("No loglevel specified"));
	continue;
      }
      if (_stscanf(args[j], _T("%d"), &m_logLevel) != 1) {
	ArgError(_T("Invalid loglevel specified"));
	continue;
      }
			
    } else if ( SwitchMatch(args[j], _T("console") )) {
      m_logToConsole = true;
    } else if ( SwitchMatch(args[j], _T("logfile") )) {
      if (++j == i) {
	ArgError(_T("No logfile specified"));
	continue;
      }
      if (_stscanf(args[j], _T("%s"), &m_logFilename) != 1) {
	ArgError(_T("Invalid logfile specified"));
	continue;
      } else {
	m_logToFile = true;
      }
    } else if ( SwitchMatch(args[j], _T("config") )) {
      if (++j == i) {
	ArgError(_T("No config file specified"));
	continue;
      }
      // The GetPrivateProfile* stuff seems not to like some relative paths
      _fullpath(m_configFilename, args[j], _MAX_PATH);
      if (_access(m_configFilename, 04)) {
	ArgError(_T("Can't open specified config file for reading."));
	PostQuitMessage(1);
	continue;
      } else {
	Load(m_configFilename);
	m_configSpecified = true;
      }
    } else if ( SwitchMatch(args[j], _T("register") )) {
      Register();
      PostQuitMessage(0);
    } else {
      TCHAR phost[256];
      if (!ParseDisplay(args[j], phost, 255, &m_port)) {
	ShowUsage(_T("Invalid VNC server specified."));
	PostQuitMessage(1);
      } else {
	_tcscpy(m_host, phost);
	m_connectionSpecified = true;
      }
    }
  }       
	

  // tidy up
  delete [] cmd;
  delete [] args;
}

void saveInt(char *name, int value, char *fname) 
{
  char buf[4];
  sprintf(buf, "%d", value); 
  WritePrivateProfileString("options", name, buf, fname);
}

int readInt(char *name, int defval, char *fname)
{
  return GetPrivateProfileInt("options", name, defval, fname);
}

void VNCOptions::Save(char *fname)
{
  for (int i = rfbEncodingRaw; i<= LASTENCODING; i++) {
    char buf[128];
    sprintf(buf, "use_encoding_%d", i);
    saveInt(buf, m_UseEnc[i], fname);
  }
  saveInt("preferred_encoding",	m_PreferredEncoding,fname);
  saveInt("shared",			m_Shared,		fname);
  saveInt("swapmouse",			m_SwapMouse,		fname);
  saveInt("emulate3",			m_Emul3Buttons,		fname);
  saveInt("emulate3timeout",		m_Emul3Timeout,		fname);
  saveInt("emulate3fuzz",		m_Emul3Fuzz,		fname);
  saveInt("disableclipboard",		m_DisableClipboard, fname);
  saveInt("edge",			m_edge,		fname);
}

void VNCOptions::Load(char *fname)
{
  for (int i = rfbEncodingRaw; i<= LASTENCODING; i++)
  {
    char buf[128];
    sprintf(buf, "use_encoding_%d", i);
    m_UseEnc[i] =   readInt(buf, m_UseEnc[i], fname) != 0;
  }
  m_PreferredEncoding =	readInt("preferred_encoding", m_PreferredEncoding,	fname);
  m_Shared =		readInt("shared",	m_Shared,		fname) != 0;
  m_SwapMouse =		readInt("swapmouse",	m_SwapMouse,	fname) != 0;
  m_Emul3Buttons =	readInt("emulate3",	m_Emul3Buttons, fname) != 0;
  m_Emul3Timeout =	readInt("emulate3timeout",m_Emul3Timeout, fname);
  m_Emul3Fuzz =		readInt("emulate3fuzz",	m_Emul3Fuzz,    fname);
  m_DisableClipboard =	readInt("disableclipboard", m_DisableClipboard, fname) != 0;
  m_edge =		readInt("edge",		m_edge,	fname);
}

// Record the path to the VNC viewer and the type
// of the .vnc files in the registry
void VNCOptions::Register()
{
  char keybuf[_MAX_PATH * 2 + 20];
  HKEY hKey, hKey2;
  if ( RegCreateKey(HKEY_CLASSES_ROOT, ".w2v", &hKey)  == ERROR_SUCCESS ) {
    RegSetValue(hKey, NULL, REG_SZ, "Win2VNC.Config", 0);
    RegCloseKey(hKey);
  } else {
    log.Print(0, "Failed to register .w2v extension\n");
  }
  
  char filename[_MAX_PATH];
  if (GetModuleFileName(NULL, filename, _MAX_PATH) == 0) {
    log.Print(0, "Error getting win2vnc filename\n");
    return;
  }
  log.Print(2, "Viewer is %s\n", filename);
  
  if ( RegCreateKey(HKEY_CLASSES_ROOT, "Win2VNC.Config", &hKey)  == ERROR_SUCCESS ) {
    RegSetValue(hKey, NULL, REG_SZ, "Win2VNC Config File", 0);
    
    if ( RegCreateKey(hKey, "DefaultIcon", &hKey2)  == ERROR_SUCCESS ) {
      sprintf(keybuf, "%s,0", filename);
      RegSetValue(hKey2, NULL, REG_SZ, keybuf, 0);
      RegCloseKey(hKey2);
    }
    if ( RegCreateKey(hKey, "Shell\\open\\command", &hKey2)  == ERROR_SUCCESS ) {
      sprintf(keybuf, "\"%s\" -config \"%%1\"", filename);
      RegSetValue(hKey2, NULL, REG_SZ, keybuf, 0);
      RegCloseKey(hKey2);
    }
    
    RegCloseKey(hKey);
  }
  
  if ( RegCreateKey(HKEY_LOCAL_MACHINE, 
		    "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\win2vnc.exe", 
		    &hKey)  == ERROR_SUCCESS ) {
    RegSetValue(hKey, NULL, REG_SZ, filename, 0);
    RegCloseKey(hKey);
  }
}

void VNCOptions::ShowUsage(LPTSTR info)
{
  TCHAR msg[1024];
  TCHAR *tmpinf = _T("");
  if (info != NULL) 
    tmpinf = info;
  _stprintf(msg, 
	    _T("%s\n\rUsage includes:\n\r"
	       "  vncviewer [/swapmouse] [/shared] [/emulate3] \n\r"
	       "      [/north] [/south] [/east] [/west]\n\r"
	       "      [/config configfile] [server:display]\n\r"
	       "For full details see documentation."), 
	    tmpinf);
  MessageBox(NULL,  msg, _T("VNC error"), MB_OK | MB_ICONSTOP | MB_TOPMOST);
}

// The dialog box allows you to change the session-specific parameters
int VNCOptions::DoDialog(bool running)
{
  m_running = running;
  return DialogBoxParam(pApp->m_instance,
			DIALOG_MAKEINTRESOURCE(IDD_OPTIONDIALOG), 
			NULL,
			(DLGPROC) OptDlgProc,
			(LONG) this);
}

BOOL CALLBACK VNCOptions::OptDlgProc( HWND hwnd,
				      UINT uMsg,  
				      WPARAM wParam,
				      LPARAM lParam )
{
  // This is a static method, so we don't know which instantiation we're 
  // dealing with. But we can get a pseudo-this from the parameter to 
  // WM_INITDIALOG, which we therafter store with the window and retrieve
  // as follows:

  VNCOptions *_this;
  if(uMsg == WM_INITDIALOG)
  {
    SetWindowLong(hwnd, GWL_USERDATA, lParam);
    _this = (VNCOptions *) lParam;
  }else{
    _this = (VNCOptions *) GetWindowLong(hwnd, GWL_USERDATA);
  }
  if(_this)
  {
    return _this->RealOptDlgProc(hwnd,uMsg,wParam,lParam);
  }else{
    return 0;
  }
}
	
int CALLBACK VNCOptions::RealOptDlgProc( HWND hwnd,
					 UINT uMsg,  
					 WPARAM wParam,
					 LPARAM lParam )
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
    {
      // Initialise the controls
      for (int i = rfbEncodingRaw; i <= LASTENCODING; i++) {
	HWND hPref = GetDlgItem(hwnd, IDC_RAWRADIO + (i-rfbEncodingRaw));
	SendMessage(hPref, BM_SETCHECK, 
		    (i== m_PreferredEncoding), 0);
	EnableWindow(hPref, m_UseEnc[i]);
      }
			
      HWND hCopyRect = GetDlgItem(hwnd, ID_SESSION_SET_CRECT);
      SendMessage(hCopyRect, BM_SETCHECK, m_UseEnc[rfbEncodingCopyRect], 0);
			
      HWND hSwap = GetDlgItem(hwnd, ID_SESSION_SWAPMOUSE);
      SendMessage(hSwap, BM_SETCHECK, m_SwapMouse, 0);

#ifndef UNDER_CE
      HWND hDisableClip = GetDlgItem(hwnd, IDC_DISABLECLIPBOARD);
      SendMessage(hDisableClip, BM_SETCHECK, m_DisableClipboard, 0);
#endif			
			
      HWND hShared = GetDlgItem(hwnd, IDC_SHARED);
      SendMessage(hShared, BM_SETCHECK, m_Shared, 0);
      EnableWindow(hShared, !m_running);
			 

#ifndef UNDER_CE
      HWND hEmulate = GetDlgItem(hwnd, IDC_EMULATECHECK);
      SendMessage(hEmulate, BM_SETCHECK, m_Emul3Buttons, 0);
#endif

      HWND hE = GetDlgItem(hwnd, IDC_EDGE_EAST);
      SendMessage(hE, BM_SETCHECK, m_edge == M_EDGE_EAST, 0);
			 
      HWND hW = GetDlgItem(hwnd, IDC_EDGE_WEST);
      SendMessage(hW, BM_SETCHECK, m_edge == M_EDGE_WEST, 0);
			 
      HWND hN = GetDlgItem(hwnd, IDC_EDGE_NORTH);
      SendMessage(hW, BM_SETCHECK, m_edge == M_EDGE_NORTH, 0);
			 
      HWND hS = GetDlgItem(hwnd, IDC_EDGE_SOUTH);
      SendMessage(hW, BM_SETCHECK, m_edge == M_EDGE_SOUTH, 0);

      CentreWindow(hwnd);
			
      return TRUE;
    }

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
	case IDOK:
	{
	  for (int i = rfbEncodingRaw; i <= LASTENCODING; i++)
	  {
	    HWND hPref = GetDlgItem(hwnd, IDC_RAWRADIO+i-rfbEncodingRaw);
	    if (SendMessage(hPref, BM_GETCHECK, 0, 0) == BST_CHECKED)
	      m_PreferredEncoding = i;
	  }
				
	  HWND hCopyRect = GetDlgItem(hwnd, ID_SESSION_SET_CRECT);
	  m_UseEnc[rfbEncodingCopyRect] =
	    (SendMessage(hCopyRect, BM_GETCHECK, 0, 0) == BST_CHECKED);
				
	  HWND hSwap = GetDlgItem(hwnd, ID_SESSION_SWAPMOUSE);
	  m_SwapMouse =
	    (SendMessage(hSwap, BM_GETCHECK, 0, 0) == BST_CHECKED);
				
#ifndef UNDER_CE				
	  HWND hDisableClip = GetDlgItem(hwnd, IDC_DISABLECLIPBOARD);
	  m_DisableClipboard =
	    (SendMessage(hDisableClip, BM_GETCHECK, 0, 0) == BST_CHECKED);
#endif
				
	  HWND hShared = GetDlgItem(hwnd, IDC_SHARED);
	  m_Shared =
	    (SendMessage(hShared, BM_GETCHECK, 0, 0) == BST_CHECKED);
				
#ifndef UNDER_CE
	  HWND hEmulate = GetDlgItem(hwnd, IDC_EMULATECHECK);
	  m_Emul3Buttons =
	    (SendMessage(hEmulate, BM_GETCHECK, 0, 0) == BST_CHECKED);
#endif

	  HWND hN = GetDlgItem(hwnd, IDC_EDGE_NORTH);
	  if(SendMessage(hN, BM_GETCHECK, 0, 0) == BST_CHECKED)
	    m_edge = M_EDGE_NORTH;

	  HWND hS = GetDlgItem(hwnd, IDC_EDGE_SOUTH);
	  if(SendMessage(hS, BM_GETCHECK, 0, 0) == BST_CHECKED)
	    m_edge = M_EDGE_SOUTH;

	  HWND hE = GetDlgItem(hwnd, IDC_EDGE_EAST);
	  if(SendMessage(hE, BM_GETCHECK, 0, 0) == BST_CHECKED)
	    m_edge = M_EDGE_EAST;

	  HWND hW = GetDlgItem(hwnd, IDC_EDGE_WEST);
	  if(SendMessage(hW, BM_GETCHECK, 0, 0) == BST_CHECKED)
	    m_edge = M_EDGE_WEST;

	  EndDialog(hwnd, TRUE);
				
	  return TRUE;
	}
	case IDCANCEL:
	  EndDialog(hwnd, FALSE);
	  return TRUE;
      }
      break;
    case WM_DESTROY:
      EndDialog(hwnd, FALSE);
      return TRUE;
  }
  return 0;
}

