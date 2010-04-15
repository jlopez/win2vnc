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

// Hextile Encoding
//
// The bits of the ClientConnection object to do with Hextile.

#include "stdhdrs.h"
#include "vncviewer.h"
#include "ClientConnection.h"

void ClientConnection::ReadHextileRect(rfbFramebufferUpdateRectHeader *pfburh)
{
	switch (m_myFormat.bitsPerPixel) {
	case 8:
		HandleHextileEncoding8(pfburh->r.x, pfburh->r.y, pfburh->r.w, pfburh->r.h);
		break;
	case 16:
		HandleHextileEncoding16(pfburh->r.x, pfburh->r.y, pfburh->r.w, pfburh->r.h);
		break;
	case 32:
		HandleHextileEncoding32(pfburh->r.x, pfburh->r.y, pfburh->r.w, pfburh->r.h);
		break;
	}
}


#define DEFINE_HEXTILE(bpp)                                                   \
void ClientConnection::HandleHextileEncoding##bpp(int rx, int ry, int rw, int rh)                    \
{                                                                             \
    CARD##bpp bg, fg;                                                         \
    CARD8 *ptr;                                                               \
    int x, y, w, h;                                                           \
    CARD8 subencoding;                                                        \
    CARD8 nSubrects;                                                          \
																			  \
    CheckBufferSize( 16 * 16 * bpp );										  \
                                                                              \
    for (y = ry; y < ry+rh; y += 16) {                                        \
		for (x = rx; x < rx+rw; x += 16) {                                    \
            w = h = 16;                                                       \
            if (rx+rw - x < 16)                                               \
                w = rx+rw - x;                                                \
            if (ry+rh - y < 16)                                               \
                h = ry+rh - y;                                                \
                                                                              \
            ReadExact((char *)&subencoding, 1);                               \
                                                                              \
            if (subencoding & rfbHextileRaw) {                                \
                ReadExact(m_netbuf, w * h * (bpp / 8));                       \
                continue;                                                     \
            }                                                                 \
                                                                              \
		    if (subencoding & rfbHextileBackgroundSpecified) {                \
                ReadExact((char *)&bg, (bpp/8));                              \
			}																  \
                                                                              \
            if (subencoding & rfbHextileForegroundSpecified)  {               \
                ReadExact((char *)&fg, (bpp/8));                              \
			}                                                                 \
                                                                              \
            if (!(subencoding & rfbHextileAnySubrects)) {                     \
                continue;                                                     \
            }                                                                 \
                                                                              \
            ReadExact( (char *)&nSubrects, 1) ;                               \
                                                                              \
            ptr = (CARD8 *)m_netbuf;                                          \
                                                                              \
            if (subencoding & rfbHextileSubrectsColoured) {                   \
				                                                              \
                ReadExact( m_netbuf, nSubrects * (2 + (bpp / 8)));            \
                                                                              \
            } else {                                                          \
                ReadExact(m_netbuf, nSubrects * 2);                    \
            }                                                                 \
        }                                                                     \
    }                                                                         \
                                                                              \
}

DEFINE_HEXTILE(8)
DEFINE_HEXTILE(16)
DEFINE_HEXTILE(32)


