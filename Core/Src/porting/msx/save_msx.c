#include <odroid_system.h>

#include "main.h"
#include "appid.h"

#include "common.h"
#include "gw_lcd.h"
#include "main_msx.h"
#include "save_msx.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "Board.h"

#include "gw_core_bridge.h"

static char *headerString  = "bMSX0000";

extern BoardInfo boardInfo;

struct SaveStateSection {
    UInt32 tag;
    UInt32 offset;
};

// We have 31*8 Bytes available for sections info
// Do not increase this value without reserving
// another 256 bytes block for header
#define MAX_SECTIONS 31

typedef struct {
    struct SaveStateSection sections[MAX_SECTIONS];
    UInt16 section;
} Savestate;

static Savestate msxSaveState;

extern BoardInfo boardInfo;

static FILE *msxSaveFile;

static UInt32 tagFromName(const char* tagName)
{
    UInt32 tag = 0;
    UInt32 mod = 1;

    while (*tagName) {
        mod *= 19219;
        tag += mod * *tagName++;
    }

    return tag;
}

/* Savestate functions */
UInt32 saveMsxState(char *savePath) {
    UInt32 size;
    // Fill context data
    msxSaveState.section = 0;

    msxSaveFile = fopen(savePath, "wb");
    fwrite((unsigned char *)headerString, 1, 8, msxSaveFile);
    fseek(msxSaveFile, 256, SEEK_SET); // Keep 256 Bytes free for offset header
    boardSaveState("mem0",0);
    save_gnw_msx_data();
    // Copy header data at the start of file (after header)
    fseek(msxSaveFile, 8, SEEK_SET);
    fwrite((UInt8 *)msxSaveState.sections, 1, sizeof(msxSaveState.sections[0])*MAX_SECTIONS, msxSaveFile);

    size = ftell(msxSaveFile);
    fclose(msxSaveFile);

    return size;
}

void saveStateCreateForWrite(const char* fileName)
{
    // Nothing to do
}

void saveStateSet(SaveState* state, const char* tagName, UInt32 value)
{
    wdog_refresh();
    fwrite((unsigned char *)&value, 1, 4, msxSaveFile);
}

void saveStateSetBuffer(SaveState* state, const char* tagName, void* buffer, UInt32 length)
{
    wdog_refresh();
    fwrite(buffer, 1, length, msxSaveFile);
}

SaveState* saveStateOpenForWrite(const char* fileName)
{
    // Update section
    msxSaveState.sections[msxSaveState.section].tag = tagFromName(fileName);
    msxSaveState.sections[msxSaveState.section].offset = ftell(msxSaveFile);
    msxSaveState.section++;
    return &msxSaveState;
}

void saveStateDestroy(void)
{
}

void saveStateClose(SaveState* state)
{
}

/* Loadstate functions */

UInt32 loadMsxState(char *savePath) {
    char header[8];
    UInt32 size = 0;

    msxSaveFile = fopen(savePath, "rb");
    fread((unsigned char *)header, 1, 8, msxSaveFile);
    if (memcmp(headerString, header, 8) == 0) {
        // Copy sections header in structure
        fread(msxSaveState.sections, 1, sizeof(msxSaveState.sections[0])*MAX_SECTIONS, msxSaveFile);
        boardInfo.loadState();
        load_gnw_msx_data();
        size = ftell(msxSaveFile);
    }
    fclose(msxSaveFile);
    return size;
}

SaveState* saveStateOpenForRead(const char* fileName)
{
    // find offset
    UInt32 tag = tagFromName(fileName);
    for (int i = 0; i<MAX_SECTIONS; i++) {
        if (msxSaveState.sections[i].tag == tag) {
            // Found tag
            fseek(msxSaveFile, msxSaveState.sections[i].offset, SEEK_SET);
            return &msxSaveState;
        }
    }
    return &msxSaveState;
}

UInt32 saveStateGet(SaveState* state, const char* tagName, UInt32 defValue)
{
    UInt32 value;
    wdog_refresh();
    fread((unsigned char *)&value, 1, 4, msxSaveFile);
    return value;
}

void saveStateGetBuffer(SaveState* state, const char* tagName, void* buffer, UInt32 length)
{
    fread(buffer, 1, length, msxSaveFile);
}

void saveStateCreateForRead(const char* fileName)
{
}
