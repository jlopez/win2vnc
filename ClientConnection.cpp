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


// Many thanks to Randy Brown <rgb@inven.com> for providing the 3-button
// emulation code.

// This is the main source for a ClientConnection object.
// It handles almost everything to do with a connection to a server.
// The decoding of specific rectangle encodings is done in separate files.

// #define USE_SNOOPDLL

#include "stdhdrs.h"
#include "vncviewer.h"
#include "omnithread.h"

#include "ClientConnection.h"
#include "SessionDialog.h"
#include "AuthDialog.h"
#include "Menu.h"


#include "Exception.h"
extern "C" {
	#include "vncauth.h"
}


#define INITIALNETBUFSIZE 4096
#define MAX_ENCODINGS 10
#define VWR_WND_CLASS_NAME _T("win2vnc")

static LRESULT CALLBACK ClientConnection::WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

#ifdef USE_SNOOPDLL
#include "snoopdll.h"
static SnoopDLL *snoopdll;
#endif

// *************************************************************************
//  A Client connection involves two threads - the main one which sets up
//  connections and processes window messages and inputs, and a 
//  client-specific one which receives, decodes and draws output data 
//  from the remote server.
//  This first section contains bits which are generally called by the main
//  program thread.
// *************************************************************************

ClientConnection::ClientConnection(VNCviewerApp *pApp) 
{
	Init(pApp);
}

ClientConnection::ClientConnection(VNCviewerApp *pApp, SOCKET sock) 
{
	Init(pApp);
	m_sock = sock;
	m_serverInitiated = true;
	struct sockaddr_in svraddr;
	int sasize = sizeof(svraddr);
	if (getpeername(sock, (struct sockaddr *) &svraddr, 
		&sasize) != SOCKET_ERROR) {
		_stprintf(m_host, _T("%d.%d.%d.%d"), 
			svraddr.sin_addr.S_un.S_un_b.s_b1, 
			svraddr.sin_addr.S_un.S_un_b.s_b2, 
			svraddr.sin_addr.S_un.S_un_b.s_b3, 
			svraddr.sin_addr.S_un.S_un_b.s_b4);
		m_port = svraddr.sin_port;
	} else {
		_tcscpy(m_host,_T("(unknown)"));
		m_port = 0;
	};
}

ClientConnection::ClientConnection(VNCviewerApp *pApp, LPTSTR host, int port)
{
	Init(pApp);
	_tcsncpy(m_host, host, MAX_HOST_NAME_LEN);
	m_port = port;
}


void ClientConnection::Init(VNCviewerApp *pApp)
{
  m_onedge = 1;
  m_hwnd = 0;
  m_edgewindow =0;
  m_menu = 0;
  m_desktopName = NULL;
  m_port = -1;
  m_serverInitiated = false;
  m_netbuf = NULL;
  m_netbufsize = 0;
  m_hwndNextViewer = NULL;	
  m_pApp = pApp;
  m_dormant = false;
  m_encPasswd[0] = '\0';
  
  // We take the initial conn options from the application defaults
  m_opts = m_pApp->m_options;
  
  m_sock = INVALID_SOCKET;
  m_bKillThread = false;
  m_threadStarted = true;
  m_running = false;
  m_pendingFormatChange = false;
  
  m_waitingOnEmulateTimer = false;
  m_emulatingMiddleButton = false;
  
  // Create a buffer for various network operations
  CheckBufferSize(INITIALNETBUFSIZE);
  
  m_pApp->RegisterConnection(this);
}

// 
// Run() creates the connection if necessary, does the initial negotiations
// and then starts the thread running which does the output (update) processing.
// If Run throws an Exception, the caller must delete the ClientConnection object.
//

void ClientConnection::Run()
{
	// Get the host name and port if we haven't got it
	if (m_port == -1) 
		GetConnectDetails();

	// Connect if we're not already connected
	if (m_sock == INVALID_SOCKET) 
		Connect();

	SetSocketOptions();

	NegotiateProtocolVersion();
	
	Authenticate();

	// Set up windows etc 
	CreateDisplay();

	SendClientInit();
	
	ReadServerInit();
	
	SetupPixelFormat();
	
	SetFormatAndEncodings();

	// This starts the worker thread.
	// The rest of the processing continues in run_undetached.
	start_undetached();
}


void ClientConnection::CreateDisplay() 
{
  m_screenwidth=GetSystemMetrics(SM_CXSCREEN);
  m_screenheight=GetSystemMetrics(SM_CYSCREEN);
  
  // Create the window
  WNDCLASS wndclass;
  
  wndclass.style			= 0;
  wndclass.lpfnWndProc	= ClientConnection::WndProc;
  wndclass.cbClsExtra		= 0;
  wndclass.cbWndExtra		= 0;
  wndclass.hInstance		= m_pApp->m_instance;
  wndclass.hIcon		 = LoadIcon(m_pApp->m_instance, MAKEINTRESOURCE(IDI_MAINICON));
  wndclass.hCursor		= LoadCursor(m_pApp->m_instance, MAKEINTRESOURCE(IDC_NOCURSOR));
  wndclass.hbrBackground	= (HBRUSH) GetStockObject(BLACK_BRUSH);
  wndclass.lpszMenuName	= (const TCHAR *) NULL;
  wndclass.lpszClassName	= VWR_WND_CLASS_NAME;
  
  RegisterClass(&wndclass);
  
  const DWORD winstyle = 0;
  
  m_edgewindow = CreateWindowEx(
    WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
    VWR_WND_CLASS_NAME,
    _T("win2vnc"),
    winstyle,
    (m_opts.m_edge == M_EDGE_EAST) ? m_screenwidth -1: 0,
    (m_opts.m_edge == M_EDGE_SOUTH) ? m_screenheight -1: 0,
    !(m_opts.m_edge & M_EDGE_EW) ? m_screenwidth : 1,    // x-size
    (m_opts.m_edge & M_EDGE_EW) ? m_screenheight: 1,       // y-size
    NULL,                // Parent handle
    NULL,                // Menu handle
    m_pApp->m_instance,
    NULL);
  
  
  m_hwnd = CreateWindowEx(
    WS_EX_TRANSPARENT | WS_EX_TOPMOST,
    VWR_WND_CLASS_NAME,
    _T("win2vnc"),
    winstyle,
    0,
    0,
    m_screenwidth,       // x-size
    m_screenheight,      // y-size
    m_edgewindow,                // Parent handle
    NULL,                // Menu handle
    m_pApp->m_instance,
    NULL);
  
  ShowWindow(m_hwnd, SW_HIDE);
  ShowWindow(m_edgewindow, SW_HIDE);
  
  // record which client created this window
  SetWindowLong(m_hwnd, GWL_USERDATA, (LONG) this);
  SetWindowLong(m_edgewindow, GWL_USERDATA, (LONG) this);
  
  // Create a memory DC which we'll use for drawing to
  // the local framebuffer
  
  
  
  // Set up clipboard watching
#ifndef _WIN32_WCE
  // We want to know when the clipboard changes, so
  // insert ourselves in the viewer chain. But doing
  // this will cause us to be notified immediately of
  // the current state.
  // We don't want to send that.
  m_initialClipboardSeen = false;
  m_hwndNextViewer = SetClipboardViewer(m_hwnd); 	
#endif
  
#ifdef USE_SNOOPDLL
  snoopdll=new SnoopDLL();
  
  log.Print(1, _T("Starting snoop (%d,%d, %d,%d)\n"),
	    m_screenwidth,
	    m_screenheight,
	    (m_opts.m_edge == M_EDGE_WEST) ? 0 :
	    (m_opts.m_edge == M_EDGE_EAST) ? m_screenwidth -1 : 
	    -1,
	    (m_opts.m_edge == M_EDGE_NORTH) ? 0 :
	    (m_opts.m_edge == M_EDGE_SOUTH) ? m_screenheight -1 : 
	    -1);
  snoopdll->start(m_hwnd,
		  m_screenwidth,
		  m_screenheight,
		  (m_opts.m_edge == M_EDGE_WEST) ? 0 :
		  (m_opts.m_edge == M_EDGE_EAST) ? m_screenwidth -1 : 
		  -1,
		  (m_opts.m_edge == M_EDGE_NORTH) ? 0 :
		  (m_opts.m_edge == M_EDGE_SOUTH) ? m_screenheight -1 : 
		  -1);
#endif
  
  m_onedge=0;
  MoveWindowToEdge();


  m_menu = new vncMenu(m_pApp, this);
}

void ClientConnection::GetConnectDetails()
{
  if (m_opts.m_configSpecified) {
    LoadConnection(m_opts.m_configFilename);
  } else {
    SessionDialog sessdlg(&m_opts);
    if (!sessdlg.DoDialog()) {
      throw QuietException("User Cancelled");
    }
    _tcsncpy(m_host, sessdlg.m_host, MAX_HOST_NAME_LEN);
    m_port = sessdlg.m_port;
  }
  // This is a bit of a hack: 
  // The config file may set various things in the app-level defaults which 
  // we don't want to be used except for the first connection. So we clear them
  // in the app defaults here.
  m_pApp->m_options.m_host[0] = '\0';
  m_pApp->m_options.m_port = -1;
  m_pApp->m_options.m_connectionSpecified = false;
  m_pApp->m_options.m_configSpecified = false;
}

void ClientConnection::Connect()
{
  struct sockaddr_in thataddr;
  int res;
  
  m_sock = socket(PF_INET, SOCK_STREAM, 0);
  if (m_sock == INVALID_SOCKET)
    throw WarningException(_T("Error creating socket"));
  int one = 1;
  
  // The host may be specified as a dotted address "a.b.c.d"
  // Try that first
  thataddr.sin_addr.s_addr = inet_addr(m_host);
  
  // If it wasn't one of those, do gethostbyname
  if (thataddr.sin_addr.s_addr == INADDR_NONE) {
    LPHOSTENT lphost;
    lphost = gethostbyname(m_host);
    
    if (lphost == NULL) { 
      throw WarningException("Failed to get server address.\n\r"
			     "Did you type the host name correctly?"); 
    };
    thataddr.sin_addr.s_addr = ((LPIN_ADDR) lphost->h_addr)->s_addr;
  };
  
  thataddr.sin_family = AF_INET;
  thataddr.sin_port = htons(m_port);
  res = connect(m_sock, (LPSOCKADDR) &thataddr, sizeof(thataddr));
  if (res == SOCKET_ERROR)
    throw WarningException("Failed to connect to server");
  log.Print(0, _T("Connected to %s port %d\n"), m_host, m_port);
  
}

void ClientConnection::SetSocketOptions() {
	// Disable Nagle's algorithm
	BOOL nodelayval = TRUE;
	if (setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &nodelayval, sizeof(BOOL)))
		throw WarningException("Error disabling Nagle's algorithm");
}


void ClientConnection::NegotiateProtocolVersion()
{
	rfbProtocolVersionMsg pv;

   /* if the connection is immediately closed, don't report anything, so
       that pmw's monitor can make test connections */

    try {
		ReadExact(pv, sz_rfbProtocolVersionMsg);
	} catch (Exception &c) {
		log.Print(0, _T("Error reading protocol version: %s\n"), c);
		throw QuietException(c.m_info);
	}

    pv[sz_rfbProtocolVersionMsg] = 0;

	// XXX This is a hack.  Under CE we just return to the server the
	// version number it gives us without parsing it.  
	// Too much hassle replacing sscanf for now. Fix this!
#ifdef UNDER_CE
	m_majorVersion = rfbProtocolMajorVersion;
	m_minorVersion = rfbProtocolMinorVersion;
#else
    if (sscanf(pv,rfbProtocolVersionFormat,&m_majorVersion,&m_minorVersion) != 2) {
		throw WarningException(_T("Invalid protocol"));
    }
    log.Print(0, _T("RFB server supports protocol version %d.%d\n"),
	    m_majorVersion,m_minorVersion);

    if ((m_majorVersion == 3) && (m_minorVersion < 3)) {
		
        /* if server is 3.2 we can't use the new authentication */
		log.Print(0, _T("Can't use IDEA authentication\n"));
        /* This will be reported later if authentication is requested*/

    } else {
		
        /* any other server version, just tell the server what we want */
		m_majorVersion = rfbProtocolMajorVersion;
		m_minorVersion = rfbProtocolMinorVersion;

    }

    sprintf(pv,rfbProtocolVersionFormat,m_majorVersion,m_minorVersion);
#endif

    WriteExact(pv, sz_rfbProtocolVersionMsg);

	log.Print(0, _T("Connected to RFB server, using protocol version %d.%d\n"),
		rfbProtocolMajorVersion, rfbProtocolMinorVersion);
}

void ClientConnection::Authenticate()
{
	CARD32 authScheme, reasonLen, authResult;
    CARD8 challenge[CHALLENGESIZE];
	
	ReadExact((char *)&authScheme, 4);
    authScheme = Swap32IfLE(authScheme);
	
    switch (authScheme) {
		
    case rfbConnFailed:
		ReadExact((char *)&reasonLen, 4);
		reasonLen = Swap32IfLE(reasonLen);
		
		CheckBufferSize(reasonLen+1);
		ReadString(m_netbuf, reasonLen);
		
		log.Print(0, _T("RFB connection failed, reason: %s\n"), m_netbuf);
		throw WarningException(m_netbuf);
        break;
		
    case rfbNoAuth:
		log.Print(0, _T("No authentication needed\n"));
		break;
		
    case rfbVncAuth:
		{
            if ((m_majorVersion == 3) && (m_minorVersion < 3)) {
                /* if server is 3.2 we can't use the new authentication */
                log.Print(0, _T("Can't use IDEA authentication\n"));

                MessageBox(NULL, 
                    _T("Sorry - this server uses an older authentication scheme\n\r")
                    _T("which is no longer supported."), 
                    _T("Protocol Version error"), 
                    MB_OK | MB_ICONSTOP | MB_SETFOREGROUND | MB_TOPMOST);

                throw WarningException("Can't use IDEA authentication any more!");
            }

			ReadExact((char *)challenge, CHALLENGESIZE);
			
			char passwd[256];
			// Was the password already specified in a config file?
			if (strlen((const char *) m_encPasswd)>0) {
				char *pw = vncDecryptPasswd(m_encPasswd);
				strcpy(passwd, pw);
				free(pw);
			} else {
				AuthDialog ad;
				ad.DoDialog();	
#ifndef UNDER_CE
				strcpy(passwd, ad.m_passwd);
#else
				int origlen = _tcslen(ad.m_passwd);
				int newlen = WideCharToMultiByte(
					CP_ACP,    // code page
					0,         // performance and mapping flags
					ad.m_passwd, // address of wide-character string
					origlen,   // number of characters in string
					passwd,    // address of buffer for new string
					255,       // size of buffer
					NULL, NULL );
				
				passwd[newlen]= '\0';
#endif
				if (strlen(passwd) == 0) {
					log.Print(0, _T("Password had zero length\n"));
					throw WarningException("Empty password");
				}
				if (strlen(passwd) > 8) {
					passwd[8] = '\0';
				}
				vncEncryptPasswd(m_encPasswd, passwd);
			}				
	
			vncEncryptBytes(challenge, passwd);

			/* Lose the plain-text password from memory */
			for (int i=0; i< (int) strlen(passwd); i++) {
				passwd[i] = '\0';
			}
			
			WriteExact((char *) challenge, CHALLENGESIZE);
			ReadExact((char *) &authResult, 4);
			
			authResult = Swap32IfLE(authResult);
			
			switch (authResult) {
			case rfbVncAuthOK:
				log.Print(0, _T("VNC authentication succeeded\n"));
				break;
			case rfbVncAuthFailed:
				log.Print(0, _T("VNC authentication failed!"));
				throw WarningException("VNC authentication failed!");
			case rfbVncAuthTooMany:
				throw WarningException(
					"VNC authentication failed - too many tries!");
			default:
				log.Print(0, _T("Unknown VNC authentication result: %d\n"),
					(int)authResult);
				throw ErrorException("Unknown VNC authentication result!");
			}
			break;
		}
		
	default:
		log.Print(0, _T("Unknown authentication scheme from RFB server: %d\n"),
			(int)authScheme);
		throw ErrorException("Unknown authentication scheme!");
    }
}

void ClientConnection::SendClientInit()
{
    rfbClientInitMsg ci;
	ci.shared = m_opts.m_Shared;

    WriteExact((char *)&ci, sz_rfbClientInitMsg);
}

void ClientConnection::ReadServerInit()
{
    ReadExact((char *)&m_si, sz_rfbServerInitMsg);
	
    m_si.framebufferWidth = Swap16IfLE(m_si.framebufferWidth);
    m_si.framebufferHeight = Swap16IfLE(m_si.framebufferHeight);
    m_si.format.redMax = Swap16IfLE(m_si.format.redMax);
    m_si.format.greenMax = Swap16IfLE(m_si.format.greenMax);
    m_si.format.blueMax = Swap16IfLE(m_si.format.blueMax);
    m_si.nameLength = Swap32IfLE(m_si.nameLength);
	
    m_desktopName = new TCHAR[m_si.nameLength + 2];

    ReadString(m_desktopName, m_si.nameLength);
    
//	SetWindowText(m_edgewindow, m_desktopName);	

	log.Print(0, _T("Desktop name \"%s\"\n"),m_desktopName);
	log.Print(1, _T("Geometry %d x %d depth %d\n"),
		m_si.framebufferWidth, m_si.framebufferHeight, m_si.format.depth );
	SetWindowText(m_edgewindow, m_desktopName);	

}


void ClientConnection::SetupPixelFormat() {
  // Normally we just use the sever's format suggestion
  m_myFormat = m_si.format;
}

void ClientConnection::SetFormatAndEncodings()
{
	// Set pixel format to myFormat
    
	rfbSetPixelFormatMsg spf;

    spf.type = rfbSetPixelFormat;
    spf.format = m_myFormat;
    spf.format.redMax = Swap16IfLE(spf.format.redMax);
    spf.format.greenMax = Swap16IfLE(spf.format.greenMax);
    spf.format.blueMax = Swap16IfLE(spf.format.blueMax);
	spf.format.bigEndian = 0;

    WriteExact((char *)&spf, sz_rfbSetPixelFormatMsg);

    // The number of bytes required to hold at least one pixel.
	m_minPixelBytes = (m_myFormat.bitsPerPixel + 7) >> 3;

	// Set encodings
    char buf[sz_rfbSetEncodingsMsg + MAX_ENCODINGS * 4];
    rfbSetEncodingsMsg *se = (rfbSetEncodingsMsg *)buf;
    CARD32 *encs = (CARD32 *)(&buf[sz_rfbSetEncodingsMsg]);
    int len = 0;
	
    se->type = rfbSetEncodings;
    se->nEncodings = 0;

	// Put the preferred encoding first, and change it if the
	// preferred encoding is not actually usable.
	for (int i = LASTENCODING; i >= rfbEncodingRaw; i--)
	{
		if (m_opts.m_PreferredEncoding == i) {
			if (m_opts.m_UseEnc[i]) {
				encs[se->nEncodings++] = Swap32IfLE(i);
			} else {
				m_opts.m_PreferredEncoding--;
			}
		}
	}

	// Now we go through and put in all the other encodings in order.
	// We do rather assume that the most recent encoding is the most
	// desirable!
	for (i = LASTENCODING; i >= rfbEncodingRaw; i--)
	{
		if ( (m_opts.m_PreferredEncoding != i) &&
			 (m_opts.m_UseEnc[i]))
		{
			encs[se->nEncodings++] = Swap32IfLE(i);
		}
	}

    len = sz_rfbSetEncodingsMsg + se->nEncodings * 4;
	
    se->nEncodings = Swap16IfLE(se->nEncodings);
	
    WriteExact((char *) buf, len);
}

// Closing down the connection.
// Close the socket, kill the thread.
void ClientConnection::KillThread()
{
  m_bKillThread = true;
  m_running = false;
  
  if (m_sock != INVALID_SOCKET) {
    shutdown(m_sock, SD_BOTH);
    closesocket(m_sock);
    m_sock = INVALID_SOCKET;
  }
}


void ClientConnection::Exit()
{
  if (m_hwnd != 0)
  {
    DestroyWindow(m_hwnd);
    m_hwnd=0;
  }
  
  if (m_edgewindow != 0)
  {
    DestroyWindow(m_edgewindow);
    m_edgewindow=0;
  }
  
  if (m_sock != INVALID_SOCKET) {
    shutdown(m_sock, SD_BOTH);
    closesocket(m_sock);
    m_sock = INVALID_SOCKET;
  }
  
  if (m_desktopName != NULL)
  {
    delete [] m_desktopName;
    m_desktopName=NULL;
  }

  if(m_netbuf)
  {
    delete [] m_netbuf;
    m_netbuf=0;
  }

  if(m_menu)
  {
    delete m_menu;
    m_menu=0;
  }
}


ClientConnection::~ClientConnection()
{
  Exit();
  m_pApp->DeregisterConnection(this);
}


void ClientConnection::MoveWindowToEdge(void)
{
  if(m_onedge) return;
  log.Print(1,_T("Moving Window to EDGE (%d,%d,%d,%d)\n"),
	       (m_opts.m_edge == M_EDGE_EAST) ? m_screenwidth -1: 0,
	       (m_opts.m_edge == M_EDGE_SOUTH) ? m_screenheight -1: 0,
	       !(m_opts.m_edge & M_EDGE_EW) ? m_screenwidth : 1,    // x-size
	       (m_opts.m_edge & M_EDGE_EW) ? m_screenheight: 1   // y-size
	    );
  SetWindowPos(m_hwnd, HWND_BOTTOM,
	       0, 0,
	       m_screenwidth, m_screenheight,
	       SWP_HIDEWINDOW | SWP_NOREDRAW);

  SetWindowPos(m_edgewindow, HWND_TOPMOST,
	       (m_opts.m_edge == M_EDGE_EAST) ? m_screenwidth -1: 0,
	       (m_opts.m_edge == M_EDGE_SOUTH) ? m_screenheight -1: 0,
	       !(m_opts.m_edge & M_EDGE_EW) ? m_screenwidth : 1,    // x-size
	       (m_opts.m_edge & M_EDGE_EW) ? m_screenheight: 1,   // y-size
	        SWP_SHOWWINDOW | SWP_NOREDRAW);

  m_onedge=1;
}

void ClientConnection::Deactivate(int x, int y)
{
  if(m_onedge) return;
//  ShowCursor(1);
  log.Print(2,_T("Warp Cursor: %d,%d -> %d, %d\n"),
	    x,y,
	    !(m_opts.m_edge & M_EDGE_EW) ? x : x ? 1 : m_screenwidth -2,
	    (m_opts.m_edge & M_EDGE_EW) ? y : y ? 1 : m_screenheight -2 );
  SetCursorPos(
    !(m_opts.m_edge & M_EDGE_EW) ? x : x ? 1 : m_screenwidth -2,
    (m_opts.m_edge & M_EDGE_EW) ? y : y ? 1 : m_screenheight -2 );

  MoveWindowToEdge();

  // try to hide slave mouse pointer
  if (m_running)
    SubProcessPointerEvent(m_si.framebufferWidth-2, 
			   m_si.framebufferHeight-2,
			   0);
}

void ClientConnection::MoveWindowToScreen(void)
{
  if(!m_onedge) return;
  log.Print(1,_T("Moving Window to SCREEN (%d,%d)\n"),
	    m_screenwidth,
	    m_screenheight);
  SetWindowPos(m_hwnd, HWND_TOPMOST,
	       0, 0,
	       m_screenwidth, m_screenheight,
	       SWP_SHOWWINDOW | SWP_NOREDRAW);

  /* Really needed ? */
  SetForegroundWindow(m_hwnd);

  m_onedge=0;
}

void ClientConnection::Activate(int x, int y)
{
  if(!m_onedge) return;
//  ShowCursor(0);
  MoveWindowToScreen();
  log.Print(5,_T("Warp Cursor: %d,%d -> %d, %d\n"),
	    x,y,
	    !(m_opts.m_edge & M_EDGE_EW) ? x : x ? 1 : m_screenwidth -2,
	    (m_opts.m_edge & M_EDGE_EW) ? y : y ? 1 : m_screenheight -2 );
  SetCursorPos(
    !(m_opts.m_edge & M_EDGE_EW) ? x : x ? 1 : m_screenwidth -2,
    (m_opts.m_edge & M_EDGE_EW) ? y : y ? 1 : m_screenheight -2 );
}


// Process windows messages

class got_wm_destroy {};

LRESULT CALLBACK ClientConnection::WndProc(HWND hwnd, UINT iMsg, 
					   WPARAM wParam, LPARAM lParam) {
	
	// This is a static method, so we don't know which instantiation we're 
	// dealing with.  But we've stored a 'pseudo-this' in the window data.
	ClientConnection *_this = (ClientConnection *) GetWindowLong(hwnd, GWL_USERDATA);
#if 1
	switch(iMsg)
	{
	  case WM_PAINT: break;
	  default:
	    log.Print(10, _T("MESSAGE: 0x%04x\n"), iMsg);
	}
#endif
	if(_this)
	{
	  try {
	    return _this->RealWndProc(hwnd, iMsg, wParam, lParam);
	  } catch (got_wm_destroy &e) {
	    // We are currently in the main thread.
	    // The worker thread should be about to finish if
	    // it hasn't already. Wait for it.
	    try {
	      void *p;
	      _this->join(&p);  // After joining, this is no longer valid
	    } catch (omni_thread_invalid& e) {
	      // The thread probably hasn't been started yet,
	    }
	    return 0;
	  }
	    
	}else{
	  return DefWindowProc(hwnd, iMsg, wParam, lParam);
	}
}

int ClientConnection::RealWndProc(HWND hwnd, UINT iMsg, 
			      WPARAM wParam, LPARAM lParam)
{
  switch (iMsg)
  {

    case WM_CREATE:
      return 0;

    case WM_TIMER:
      if (wParam == m_emulate3ButtonsTimer)
      {
	SubProcessPointerEvent(m_emulateButtonPressedX,
			       m_emulateButtonPressedY,
			       m_emulateKeyFlags);
	KillTimer(m_hwnd, m_emulate3ButtonsTimer);
	m_waitingOnEmulateTimer = false;
      }
      return 0;


    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
    {
#ifndef USE_SNOOPDLL
      POINT pt;
      if (GetFocus() != hwnd) return 0;
      if (!m_running) return 0;

      pt.x = GET_X_LPARAM(lParam);
      pt.y = GET_Y_LPARAM(lParam);

      ClientToScreen(hwnd, &pt);
      int x=pt.x;
      int y=pt.y;

      if(x<0 || x>32768) x=0;
      if(y<0 || y>32768) y=0;
      if(x>=m_screenwidth) x=m_screenwidth-1;
      if(y>=m_screenheight) y=m_screenheight-1;
	  
      if(m_onedge)
      {
	if(hwnd == m_edgewindow)
	{
	  log.Print(4,"Activated by action: %d\n",iMsg);
	  Activate(x,y);
	}
	return 0;
      }
	  
      if(hwnd == m_hwnd)
      {
	int back=0;
	switch(m_opts.m_edge)
	{
	  case M_EDGE_WEST: back=x==m_screenwidth-1; break;
	  case M_EDGE_EAST: back=x==0; break;
	  case M_EDGE_NORTH: back=y==m_screenheight-1; break;
	  case M_EDGE_SOUTH: back=y==0; break;
	}
	    
	if(back)
	{
	  Deactivate(x,y);
	  return 0;
	}
      }

      ProcessPointerEvent(x,y, wParam, iMsg);
#endif
      return 0;
    }

#ifdef USE_SNOOPDLL
    case SNOOPDLL_MOUSE_UPDATE:
    {
      if (!m_running) return 0;
	  
      int x = LOWORD(lParam);
      int y = HIWORD(lParam);
      ProcessPointerEvent(x,y, wParam, iMsg);
      return 0;
    }
	    
    case SNOOPDLL_ACTIVE_UPDATE:
    {
      log.Print(5,_T("ACTIVE=%d , %d\n"), wParam, lParam);
      if(!wParam)
      {
	MoveWindowToEdge();
	SubProcessPointerEvent(m_si.framebufferWidth - 2, 
			       m_si.framebufferHeight - 2,
			       0);
      }else{
	MoveWindowToScreen();
      }
      return 0;
    }

    case SNOOPDLL_KB_UPDATE:
#endif


    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    {
      if (!m_running) return 0;
      ProcessKeyEvent((int) wParam, (DWORD) lParam);
      return 0;
    }
	
    case WM_CHAR:
    case WM_SYSCHAR:
#ifdef UNDER_CE
    {
      int key = wParam;
      log.Print(4,_T("CHAR msg : %02x\n"), key);
      // Control keys which are in the Keymap table will already
      // have been handled.
      if (key == 0x0D  ||  // return
	  key == 0x20 ||   // space
	  key == 0x08)     // backspace
	return 0;
      
      if (key < 32) key += 64;  // map ctrl-keys onto alphabet
      if (key > 32 && key < 127) {
	SendKeyEvent(wParam & 0xff, true);
	SendKeyEvent(wParam & 0xff, false);
      }
      return 0;
    }
#endif
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
      return 0;
      
      // Cacnel modifiers when we lose focus
    case WM_KILLFOCUS:
    {
      if (!m_running) return 0;
      log.Print(6, _T("Losing focus - cancelling modifiers\n"));
      SendKeyEvent(XK_Alt_L,     false);
      SendKeyEvent(XK_Control_L, false);
      SendKeyEvent(XK_Shift_L,   false);
      SendKeyEvent(XK_Alt_R,     false);
      SendKeyEvent(XK_Control_R, false);
      SendKeyEvent(XK_Shift_R,   false);
      return 0;
    }

    case WM_CLOSE:
    {
      // Close the worker thread as well
      if(m_menu)
      {
	delete m_menu;
	m_menu=0;
      }
      KillThread();
      DestroyWindow(hwnd);
      return 0;
    }
    
    case WM_DESTROY:
    {
#ifdef USE_SNOOPDLL
      delete snoopdll;
#endif
      if(m_menu)
      {
	delete m_menu;
	m_menu=0;
      }
      
#ifndef UNDER_CE
      // Remove us from the clipboard viewer chain
      BOOL res = ChangeClipboardChain( m_hwnd, m_hwndNextViewer);
#endif
      if (m_waitingOnEmulateTimer)
      {
	KillTimer(m_hwnd, m_emulate3ButtonsTimer);
	m_waitingOnEmulateTimer = false;
      }

      SetWindowLong(m_hwnd, GWL_USERDATA, (LONG) 0);
      SetWindowLong(m_edgewindow, GWL_USERDATA, (LONG) 0);
      
      if(hwnd == m_hwnd) m_hwnd=0;
      if(hwnd == m_edgewindow) m_edgewindow=0;
      throw got_wm_destroy();
    }
    
    
#ifndef UNDER_CE
    case WM_SETCURSOR:
    {
      POINT pt;
      if(m_onedge && hwnd == m_edgewindow)
      {
	GetCursorPos(&pt);
	log.Print(2,"Activated by action: WM_SETCURSOR\n");
	Activate(pt.x, pt.y);
      }
      return DefWindowProc(hwnd, iMsg, wParam, lParam);
    }
    
    case WM_DRAWCLIPBOARD:
      ProcessLocalClipboardChange();
      return 0;
      
    case WM_CHANGECBCHAIN:
    {
      // The clipboard chain is changing
      HWND hWndRemove = (HWND) wParam;     // handle of window being removed 
      HWND hWndNext = (HWND) lParam;       // handle of next window in chain 
      // If next window is closing, update our pointer.
      if (hWndRemove == m_hwndNextViewer)  
	m_hwndNextViewer = hWndNext;  
      // Otherwise, pass the message to the next link.  
      else if (m_hwndNextViewer != NULL) 
	::SendMessage(m_hwndNextViewer, WM_CHANGECBCHAIN, 
		      (WPARAM) hWndRemove,  (LPARAM) hWndNext );  
      return 0;
      
    }
#endif

    case WM_SYNCPAINT:
    case WM_PAINT:
      return DefWindowProc(hwnd, iMsg, wParam, lParam);

    case WM_NCPAINT:
      return 0;

    case WM_REGIONUPDATED:
    case WM_SETFOCUS:
    case WM_ACTIVATE:
    case WM_NCHITTEST:
    case WM_NCACTIVATE:
    case WM_WINDOWPOSCHANGED:
    case WM_WINDOWPOSCHANGING:
    case WM_SIZE:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_GETMINMAXINFO:
    case WM_QUERYNEWPALETTE:
    case WM_PALETTECHANGED:
      return 1;
  }

//  if(hwnd != m_hwnd && hwnd != m_edgewindow)

//  if(!m_onedge)
  log.Print(9, _T("Unused message: 0x%04x\n"), iMsg);

//  return DefWindowProc(hwnd, iMsg, wParam, lParam);

  return 1;
	
  // We know about an unused variable here.
#pragma warning(disable : 4101)
}

#pragma warning(default : 4101)

// ProcessPointerEvent handles the delicate case of emulating 3 buttons
// on a two button mouse, then passes events off to SubProcessPointerEvent.
void
ClientConnection::ProcessPointerEvent(int x, int y, DWORD keyflags, UINT msg) 
{
  int xs=m_screenwidth-1;
  int ys=m_screenheight-1;
  if(m_opts.m_edge & M_EDGE_EW)
  {
    if(m_opts.m_edge == M_EDGE_EAST) x--;
    xs--;
  }else{
    if(m_opts.m_edge == M_EDGE_SOUTH) y--;
    ys--;
  }

  log.Print(8,"Mouse: [%d/%d, %d/%d]  -> [%d/%d, %d/%d]\n",
	    x,xs,y,ys,
	    x * (m_si.framebufferWidth-1) / xs,
	    m_si.framebufferWidth,
	    y * (m_si.framebufferHeight-1) / ys,
	    m_si.framebufferHeight);

  x = x * (m_si.framebufferWidth -1)  / xs;
  y = y * (m_si.framebufferHeight -1) / ys;

  if (m_opts.m_Emul3Buttons) {
    // XXX To be done:
    // If this is a left or right press, the user may be 
    // about to press the other button to emulate a middle press.
    // We need to start a timer, and if it expires without any
    // further presses, then we send the button press. 
    // If a press of the other button, or any release, comes in
    // before timer has expired, we cancel timer & take different action.
    if (m_waitingOnEmulateTimer)
    {
      if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP ||
	  abs(x - m_emulateButtonPressedX) > m_opts.m_Emul3Fuzz ||
	  abs(y - m_emulateButtonPressedY) > m_opts.m_Emul3Fuzz)
      {
	// if button released or we moved too far then cancel.
	// First let the remote know where the button was down
	SubProcessPointerEvent(
	  m_emulateButtonPressedX, 
	  m_emulateButtonPressedY, 
	  m_emulateKeyFlags);
	// Then tell it where we are now
	SubProcessPointerEvent(x, y, keyflags);
      }
      else if (
	(msg == WM_LBUTTONDOWN && (m_emulateKeyFlags & MK_RBUTTON))
	|| (msg == WM_RBUTTONDOWN && (m_emulateKeyFlags & MK_LBUTTON)))
      {
	// Triggered an emulate; remove left and right buttons, put
	// in middle one.
	DWORD emulatekeys = keyflags & ~(MK_LBUTTON|MK_RBUTTON);
	emulatekeys |= MK_MBUTTON;
	SubProcessPointerEvent(x, y, emulatekeys);
		  
	m_emulatingMiddleButton = true;
      }
      else
      {
	// handle movement normally & don't kill timer.
	// just remove the pressed button from the mask.
	DWORD keymask = m_emulateKeyFlags & (MK_LBUTTON|MK_RBUTTON);
	DWORD emulatekeys = keyflags & ~keymask;
	SubProcessPointerEvent(x, y, emulatekeys);
	return;
      }
	      
      // if we reached here, we don't need the timer anymore.
      KillTimer(m_hwnd, m_emulate3ButtonsTimer);
      m_waitingOnEmulateTimer = false;
    }
    else if (m_emulatingMiddleButton)
    {
      if ((keyflags & MK_LBUTTON) == 0 && (keyflags & MK_RBUTTON) == 0)
      {
	// We finish emulation only when both buttons come back up.
	m_emulatingMiddleButton = false;
	SubProcessPointerEvent(x, y, keyflags);
      }
      else
      {
	// keep emulating.
	DWORD emulatekeys = keyflags & ~(MK_LBUTTON|MK_RBUTTON);
	emulatekeys |= MK_MBUTTON;
	SubProcessPointerEvent(x, y, emulatekeys);
      }
    }
    else
    {
      // Start considering emulation if we've pressed a button
      // and the other isn't pressed.
      if ( (msg == WM_LBUTTONDOWN && !(keyflags & MK_RBUTTON))
	   || (msg == WM_RBUTTONDOWN && !(keyflags & MK_LBUTTON)))
      {
	// Start timer for emulation.
	m_emulate3ButtonsTimer = 
	  SetTimer(m_hwnd, 
		   IDT_EMULATE3BUTTONSTIMER, 
		   m_opts.m_Emul3Timeout, 
		   NULL);
		  
	if (!m_emulate3ButtonsTimer)
	{
	  log.Print(0, _T("Failed to create timer for emulating 3 buttons"));
	  PostMessage(m_hwnd, WM_CLOSE, 0, 0);
	  return;
	}
		  
	m_waitingOnEmulateTimer = true;
		  
	// Note that we don't send the event here; we're batching it for
	// later.
	m_emulateKeyFlags = keyflags;
	m_emulateButtonPressedX = x;
	m_emulateButtonPressedY = y;
      }
      else
      {
	// just send event noramlly
	SubProcessPointerEvent(x, y, keyflags);
      }
    }
  }
  else
  {
    SubProcessPointerEvent(x, y, keyflags);
  }
}

// SubProcessPointerEvent takes windows positions and flags and converts 
// them into VNC ones.

inline void 
ClientConnection::SubProcessPointerEvent(int x, int y, DWORD keyflags) 
{
  int mask;
  
  if (m_opts.m_SwapMouse) {
    mask = ( ((keyflags & MK_LBUTTON) ? rfbButton1Mask : 0) |
	     ((keyflags & MK_MBUTTON) ? rfbButton3Mask : 0) |
	     ((keyflags & MK_RBUTTON) ? rfbButton2Mask : 0)  );
  } else {
    mask = ( ((keyflags & MK_LBUTTON) ? rfbButton1Mask : 0) |
	     ((keyflags & MK_MBUTTON) ? rfbButton2Mask : 0) |
	     ((keyflags & MK_RBUTTON) ? rfbButton3Mask : 0)  );
  }
	
  try {
    SendPointerEvent((x), 
		     (y), mask);
  } catch (Exception &e) {
    e.Report();
    PostMessage(m_hwnd, WM_CLOSE, 0, 0);
  }
}

//
// SendPointerEvent.
//

inline void
ClientConnection::SendPointerEvent(int x, int y, int buttonMask)
{
  rfbPointerEventMsg pe;

  pe.type = rfbPointerEvent;
  pe.buttonMask = buttonMask;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  pe.x = Swap16IfLE(x);
  pe.y = Swap16IfLE(y);
  WriteExact((char *)&pe, sz_rfbPointerEventMsg);
}

//
// ProcessKeyEvent
//
// Normally a single Windows key event will map onto a single RFB
// key message, but this is not always the case.  Much of the stuff
// here is to handle AltGr (=Ctrl-Alt) on international keyboards.
// Example cases:
//
//    We want Ctrl-F to be sent as:
//      Ctrl-Down, F-Down, F-Up, Ctrl-Up.
//    because there is no keysym for ctrl-f, and because the ctrl
//    will already have been sent by the time we get the F.
//
//    On German keyboards, @ is produced using AltGr-Q, which is
//    Ctrl-Alt-Q.  But @ is a valid keysym in its own right, and when
//    a German user types this combination, he doesn't mean Ctrl-@.
//    So for this we will send, in total:
//
//      Ctrl-Down, Alt-Down,   
//                 (when we get the AltGr pressed)
//
//      Alt-Up, Ctrl-Up, @-Down, Ctrl-Down, Alt-Down 
//                 (when we discover that this is @ being pressed)
//
//      Alt-Up, Ctrl-Up, @-Up, Ctrl-Down, Alt-Down
//                 (when we discover that this is @ being released)
//
//      Alt-Up, Ctrl-Up
//                 (when the AltGr is released)

inline void ClientConnection::ProcessKeyEvent(int virtkey, DWORD keyData)
{
  bool down = ((keyData & 0x80000000l) == 0);

  // if virtkey found in mapping table, send X equivalent
  // else
  //   try to convert directly to ascii
  //   if result is in range supported by X keysyms,
  //      raise any modifiers, send it, then restore mods
  //   else
  //      calculate what the ascii would be without mods
  //      send that

#ifdef _DEBUG
#ifdef UNDER_CE
  char *keyname="";
#else
  char keyname[32];
  if (GetKeyNameText(  keyData,keyname, 31)) {
    log.Print(4, _T("Process key: %s (keyData %04x): "), keyname, keyData);
  };
#endif
#endif

  try {
    KeyActionSpec kas = m_keymap.PCtoX(virtkey, keyData);    
		
    if (kas.releaseModifiers & KEYMAP_LCONTROL) {
      SendKeyEvent(XK_Control_L, false );
      log.Print(5, _T("fake L Ctrl raised\n"));
    }
    if (kas.releaseModifiers & KEYMAP_LALT) {
      SendKeyEvent(XK_Alt_L, false );
      log.Print(5, _T("fake L Alt raised\n"));
    }
    if (kas.releaseModifiers & KEYMAP_RCONTROL) {
      SendKeyEvent(XK_Control_R, false );
      log.Print(5, _T("fake R Ctrl raised\n"));
    }
    if (kas.releaseModifiers & KEYMAP_RALT) {
      SendKeyEvent(XK_Alt_R, false );
      log.Print(5, _T("fake R Alt raised\n"));
    }
		
    for (int i = 0; kas.keycodes[i] != XK_VoidSymbol && i < MaxKeysPerKey; i++) {
      SendKeyEvent(kas.keycodes[i], down );
      log.Print(4, _T("Sent keysym %04x (%s)\n"), 
		kas.keycodes[i], down ? _T("press") : _T("release"));
    }
		
    if (kas.releaseModifiers & KEYMAP_RALT) {
      SendKeyEvent(XK_Alt_R, true );
      log.Print(5, _T("fake R Alt pressed\n"));
    }
    if (kas.releaseModifiers & KEYMAP_RCONTROL) {
      SendKeyEvent(XK_Control_R, true );
      log.Print(5, _T("fake R Ctrl pressed\n"));
    }
    if (kas.releaseModifiers & KEYMAP_LALT) {
      SendKeyEvent(XK_Alt_L, false );
      log.Print(5, _T("fake L Alt pressed\n"));
    }
    if (kas.releaseModifiers & KEYMAP_LCONTROL) {
      SendKeyEvent(XK_Control_L, false );
      log.Print(5, _T("fake L Ctrl pressed\n"));
    }
  } catch (Exception &e) {
    e.Report();
    PostMessage(m_hwnd, WM_CLOSE, 0, 0);
  }

}

//
// SendKeyEvent
//

void
ClientConnection::SendKeyEvent(CARD32 key, bool down)
{
  rfbKeyEventMsg ke;

  ke.type = rfbKeyEvent;
  ke.down = down ? 1 : 0;
  ke.key = Swap32IfLE(key);
  WriteExact((char *)&ke, sz_rfbKeyEventMsg);
  log.Print(2, _T("SendKeyEvent: key = x%04x status = %s\n"), key, 
	    down ? _T("down") : _T("up"));
}

#ifndef UNDER_CE
//
// SendClientCutText
//

void ClientConnection::SendClientCutText(char *str, int len)
{
  rfbClientCutTextMsg cct;

  cct.type = rfbClientCutText;
  cct.length = Swap32IfLE(len);
  WriteExact((char *)&cct, sz_rfbClientCutTextMsg);
  WriteExact(str, len);
  log.Print(6, _T("Sent %d bytes of clipboard\n"), len);
}
#endif


void ClientConnection::ShowConnInfo()
{
  TCHAR buf[2048];
#ifndef UNDER_CE
  char kbdname[9];
  GetKeyboardLayoutName(kbdname);
#else
  TCHAR *kbdname = _T("(n/a)");
#endif
  _stprintf(
    buf,
    _T("Connected to: %s:\n\r")
    _T("Host: %s port: %d\n\r\n\r")
    _T("Desktop geometry: %d x %d x %d\n\r")
    _T("Direction: %s\n\r")
    _T("Current protocol version: %d.%d\n\r\n\r")
    _T("Current keyboard name: %s\n\r"),
    m_desktopName, m_host, m_port,
    m_si.framebufferWidth, m_si.framebufferHeight, m_si.format.depth,
    m_opts.m_edge == M_EDGE_EAST ? "east" : 
    m_opts.m_edge == M_EDGE_WEST ? "west" : 
    m_opts.m_edge == M_EDGE_NORTH ? "north" : 
    m_opts.m_edge == M_EDGE_SOUTH ? "south" :
    "unknown",
    m_majorVersion, m_minorVersion,
    kbdname);
  MessageBox(NULL, buf, _T("VNC connection info"), MB_ICONINFORMATION | MB_OK);
}

// ********************************************************************
//  Methods after this point are generally called by the worker thread.
//  They finish the initialisation, then chiefly read data from the server.
// ********************************************************************


void* ClientConnection::run_undetached(void* arg)
{
  log.Print(9,_T("Update-processing thread started\n"));

  m_threadStarted = true;

  try {
    m_running = true;
    UpdateWindow(m_hwnd);
		
    while (!m_bKillThread) {
			
      // Look at the type of the message, but leave it in the buffer 
      CARD8 msgType;
      {
	omni_mutex_lock l(m_readMutex);  // we need this if we're not using ReadExact
	int bytes = recv(m_sock, (char *) &msgType, 1, MSG_PEEK);
	if (bytes == 0) {
	  log.Print(0, _T("Socket closed\n") );
	  throw QuietException(_T("SocketClosed"));
	}
	if (bytes < 0) {
	  log.Print(3, _T("Socket error reading message: %d\n"), WSAGetLastError() );
	  throw WarningException("Error while waiting for server message");
	}
      }
				
      switch (msgType) {
	case rfbFramebufferUpdate:
	  ReadScreenUpdate();
	  break;
	case rfbSetColourMapEntries:
	  log.Print(3, _T("rfbSetColourMapEntries read but not supported\n") );
	  throw WarningException("Unhandled SetColormap message type received!\n");
	  break;
	case rfbBell:
	  ReadBell();
	  break;
	case rfbServerCutText:
	  ReadServerCutText();
	  break;

	  // - VERSION 3.5 and above messages
	case rfbEnableExtensionRequest:
	  ReadEnableExtension();
	  break;
	default:
	  if (msgType >= rfbExtensionData) {
	    ReadExtensionData();
	  } else {
	    log.Print(3, _T("Unknown message type x%02x\n"), msgType );
	    throw WarningException("Unhandled message type received!\n");
	  }
	  break;

	  /*
	    default:
	    log.Print(3, _T("Unknown message type x%02x\n"), msgType );
	    throw WarningException("Unhandled message type received!\n");
	  */
      }

    }
        
    log.Print(4, _T("Update-processing thread finishing\n") );

  } catch (WarningException) {
    PostMessage(m_hwnd, WM_CLOSE, 0, 0);
  } catch (QuietException &e) {
    e.Report();
    PostMessage(m_hwnd, WM_CLOSE, 0, 0);
  } 
  return this;
}


// A ScreenUpdate message has been received

void ClientConnection::ReadScreenUpdate()
{
  rfbFramebufferUpdateMsg sut;
  ReadExact((char *) &sut, sz_rfbFramebufferUpdateMsg);
  sut.nRects = Swap16IfLE(sut.nRects);
  if (sut.nRects == 0) return;
  
  for (UINT i=0; i < sut.nRects; i++) {
    
    rfbFramebufferUpdateRectHeader surh;
    ReadExact((char *) &surh, sz_rfbFramebufferUpdateRectHeader);
    surh.r.x = Swap16IfLE(surh.r.x);
    surh.r.y = Swap16IfLE(surh.r.y);
    surh.r.w = Swap16IfLE(surh.r.w);
    surh.r.h = Swap16IfLE(surh.r.h);
    surh.encoding = Swap32IfLE(surh.encoding);
    
    switch (surh.encoding) {
      case rfbEncodingRaw:
	ReadRawRect(&surh);
	break;
      case rfbEncodingCopyRect:
	ReadCopyRect(&surh);
	break;
      case rfbEncodingRRE:
	ReadRRERect(&surh);
	break;
      case rfbEncodingCoRRE:
	ReadCoRRERect(&surh);
	break;
      case rfbEncodingHextile:
	ReadHextileRect(&surh);
	break;
      default:
	log.Print(0, _T("Unknown encoding %d - not supported!\n"), surh.encoding);
	break;
    }
    
    
  }	
  // Inform the other thread that an update is needed.
//  PostMessage(m_hwnd, WM_REGIONUPDATED, NULL, NULL);
}	

void ClientConnection::SetDormant(bool newstate)
{
  log.Print(5, _T("%s dormant mode\n"), newstate ? _T("Entering") : _T("Leaving"));
  m_dormant = newstate;
}

// The server has copied some text to the clipboard - put it 
// in the local clipboard too.

void ClientConnection::ReadServerCutText()
{
  rfbServerCutTextMsg sctm;
  log.Print(6, _T("Read remote clipboard change\n"));
  ReadExact((char *) &sctm, sz_rfbServerCutTextMsg);
  int len = Swap32IfLE(sctm.length);
	
  CheckBufferSize(len);
  if (len == 0) {
    m_netbuf[0] = '\0';
  } else {
    ReadString(m_netbuf, len);
  }
  UpdateLocalClipboard(m_netbuf, len);
}


void ClientConnection::ReadBell()
{
  /* Let the other machine do the bell */
}

void ClientConnection::ReadEnableExtension()
{
  rfbEnableExtensionRequestMsg eer;
  ReadExact((char *) &eer, sz_rfbEnableExtensionRequestMsg);

  const CARD32 length = Swap32IfLE(eer.length);
  CheckBufferSize(length);
  if (length != 0) {
    ReadExact(m_netbuf, length);
  }

  /* *** Enable the extension here */
}

void ClientConnection::ReadExtensionData()
{
  rfbExtensionDataMsg ed;
  ReadExact((char *) &ed, sz_rfbExtensionDataMsg);

  const CARD32 length = Swap32IfLE(ed.length);
  CheckBufferSize(length);
  if (length != 0) {
    ReadExact(m_netbuf, length);
  }

  /* *** Pass the data to the extension here */
}


// General utilities -------------------------------------------------

// Reads the number of bytes specified into the buffer given

inline void ClientConnection::ReadExact(char *inbuf, int wanted)
{
  omni_mutex_lock l(m_readMutex);

  int offset = 0;
  log.Print(10, _T("  reading %d bytes\n"), wanted);
	
  while (wanted > 0) {

    int bytes = recv(m_sock, inbuf+offset, wanted, 0);
    if (bytes == 0) throw WarningException("Connection closed.");
    if (bytes == SOCKET_ERROR) {
      int err = ::GetLastError();
      log.Print(1, _T("Socket error while reading %d\n"), err);
      m_running = false;
      throw WarningException("ReadExact: Socket error while reading.");
    }
    wanted -= bytes;
    offset += bytes;

  }
}

// Read the number of bytes and return them zero terminated in the buffer 
inline void ClientConnection::ReadString(char *buf, int length)
{
  if (length > 0)
    ReadExact(buf, length);
  buf[length] = '\0';
  log.Print(10, _T("Read a %d-byte string\n"), length);
}


// Sends the number of bytes specified from the buffer
inline void ClientConnection::WriteExact(char *buf, int bytes)
{
  if (bytes == 0) return;
  omni_mutex_lock l(m_writeMutex);
  log.Print(10, _T("  writing %d bytes\n"), bytes);
  
  int i = 0;
  int j;
  
  while (i < bytes) {
    
    j = send(m_sock, buf+i, bytes-i, 0);
    if (j == SOCKET_ERROR || j==0) {
      LPVOID lpMsgBuf;
      int err = ::GetLastError();
      FormatMessage(     
	FORMAT_MESSAGE_ALLOCATE_BUFFER | 
	FORMAT_MESSAGE_FROM_SYSTEM |     
	FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
	err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
	(LPTSTR) &lpMsgBuf, 0, NULL ); // Process any inserts in lpMsgBuf.
      log.Print(1, _T("Socket error %d: %s\n"), err, lpMsgBuf);
      LocalFree( lpMsgBuf );
      m_running = false;
      
      throw WarningException("WriteExact: Socket error while writing.");
    }
    i += j;
  }
}

// Makes sure netbuf is at least as big as the specified size.
// Note that netbuf itself may change as a result of this call.
// Throws an exception on failure.
inline void ClientConnection::CheckBufferSize(int bufsize)
{
  if (m_netbufsize > bufsize) return;

  omni_mutex_lock l(m_bufferMutex);

  char *newbuf = new char[bufsize+256];;
  if (newbuf == NULL) {
    throw ErrorException("Insufficient memory to allocate network buffer.");
  }

  // Only if we're successful...

  if (m_netbuf != NULL)
    delete [] m_netbuf;
  m_netbuf = newbuf;
  m_netbufsize=bufsize + 256;
  log.Print(4, _T("bufsize expanded to %d\n"), m_netbufsize);
}
