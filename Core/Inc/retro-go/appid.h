#pragma once

/* Runtime role of the process, not the emulated system.
 * Per-core settings live in /data/<stem>.cfg — these IDs are only used to
 * distinguish the launcher from a loaded core / homebrew. */
typedef enum {
    APPID_LAUNCHER = 0,
    APPID_CORE     = 1,
    APPID_HOMEBREW = 2,
} appid_t;
