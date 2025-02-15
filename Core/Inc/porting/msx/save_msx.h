/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Utils/SaveState.h,v $
**
** $Revision: 1.7 $
**
** $Date: 2009-07-18 14:10:27 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
******************************************************************************
*/
#ifndef SAVE_STATE_H
#define SAVE_STATE_H
 
#include "MsxTypes.h"

typedef void SaveState;

UInt32 saveMsxState(char *pathName);
UInt32 saveGnwMsxData(char *pathName);
UInt32 loadMsxStateV0(char *pathName);
UInt32 loadMsxState(char *pathName);
UInt32 loadGnwMsxData(char *pathName);

void saveStateCreateForRead(const char* fileName);
void saveStateCreateForWrite(const char* fileName);
void saveStateDestroy(void);

SaveState* saveStateOpenForRead(const char* fileName);
SaveState* saveStateOpenForWrite(const char* fileName);
void saveStateClose(SaveState* state);

UInt32 saveStateGet(SaveState* state, const char* tagName, UInt32 defValue);
void saveStateSet(SaveState* state, const char* tagName, UInt32 value);

void saveStateGetBuffer(SaveState* state, const char* tagName, void* buffer, UInt32 length);
void saveStateSetBuffer(SaveState* state, const char* tagName, void* buffer, UInt32 length);

#endif /* SAVE_STATE_H */

