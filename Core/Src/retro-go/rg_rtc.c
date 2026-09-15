#include <string.h>
#include <time.h>

#include "main.h"
#include "rg_rtc.h"
#include "stm32h7xx_hal.h"

const uint8_t MIN_TM_YEAR = 100; // 2000
const uint8_t MAX_TM_YEAR = 199; // 2099
const uint8_t MIN_TM_MON = 0;
const uint8_t MAX_TM_MON = 11;
const uint8_t MIN_TM_DAY = 1;
const uint8_t MAX_TM_DAY = 31;
const uint8_t MAX_TM_WEEKDAY = 6;
const uint8_t MIN_TM_HOUR = 0;
const uint8_t MAX_TM_HOUR = 23;
const uint8_t MIN_TM_MIN = 0;
const uint8_t MAX_TM_MIN = 59;
const uint8_t MIN_TM_SEC = 0;
const uint8_t MAX_TM_SEC = 59;
const uint8_t MIN_RTC_MONTH = 1;   // tm month is 0 based while RTC is 1 based
const uint8_t MIN_RTC_WEEKDAY = 1; // tm month is 0 and sunday based while RTC is 1 and monday based
const uint8_t MAX_RTC_WEEKDAY = 7;

struct tm TM;
uint8_t subSecond = 0; // Between 0 and 0xFF in the G&W

void ReadRTC() {
    RTC_TimeTypeDef GW_currentTime = {0};
    RTC_DateTypeDef GW_currentDate = {0};

    // Get date & time. According to STM docs, both functions need to be called at once.
    HAL_RTC_GetTime(&hrtc, &GW_currentTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &GW_currentDate, RTC_FORMAT_BIN);

    TM.tm_year = GW_currentDate.Year + MIN_TM_YEAR; // tm_year base is 1900, RTC can only save 0 - 99, so bump to 2000.
    TM.tm_mon = GW_currentDate.Month - MIN_RTC_MONTH;
    TM.tm_mday = GW_currentDate.Date;

    TM.tm_hour = GW_currentTime.Hours;
    TM.tm_min = GW_currentTime.Minutes;
    TM.tm_sec = GW_currentTime.Seconds;
    subSecond = 0xFF - (GW_currentTime.SubSeconds & 0xFF); // GW_currentTime.SubSeconds counts down from 0xFF towards 0

    /* Derive weekday from the calendar date. The STM32 WeekDay register is an
     * independent field and is often stale (changing day/month/year does not
     * update it). Prefer mktime() over the hardware value; do not round-trip
     * through localtime_r() — that timezone conversion was returning the wrong
     * weekday for Saturday on this newlib build. */
    TM.tm_isdst = 0;
    mktime(&TM);
}

void UpdateRTC() {
    RTC_TimeTypeDef GW_currentTime = {0};
    RTC_DateTypeDef GW_currentDate = {0};

    // Get date & time. According to STM docs, both functions need to be called at once before updating
    HAL_RTC_GetTime(&hrtc, &GW_currentTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &GW_currentDate, RTC_FORMAT_BIN);

    /* Recompute tm_wday from the date being written — AddToCurrentDay/Month/Year
     * only touch tm_mday/tm_mon/tm_year and leave a stale weekday behind. */
    TM.tm_isdst = 0;
    mktime(&TM);

    GW_currentDate.Year = TM.tm_year - MIN_TM_YEAR;
    GW_currentDate.Month = TM.tm_mon + MIN_RTC_MONTH;
    GW_currentDate.Date = TM.tm_mday;
    /* tm_wday 0=Sunday .. 6=Saturday → STM32 RTC 1=Monday .. 7=Sunday. */
    GW_currentDate.WeekDay = ((TM.tm_wday + MAX_TM_WEEKDAY) % MAX_RTC_WEEKDAY) + MIN_RTC_WEEKDAY;

    GW_currentTime.Hours = TM.tm_hour;
    GW_currentTime.Minutes = TM.tm_min;
    GW_currentTime.Seconds = TM.tm_sec;

    if (HAL_RTC_SetDate(&hrtc, &GW_currentDate, RTC_FORMAT_BIN) != HAL_OK || HAL_RTC_SetTime(&hrtc, &GW_currentTime, RTC_FORMAT_BIN) != HAL_OK) {
        Error_Handler();
    }

    ReadRTC(); // Should really not be necessary, but we want to ensure that the RTC is the master
}

uint8_t GetMaxDayOfCurrentMonth(void) {
    // Brute force
    for (int i = MAX_TM_DAY; i >= MIN_TM_DAY; i--) {
        struct tm tm = TM; // Clone
        tm.tm_mday = i;
        mktime(&tm);
        if (tm.tm_mday == i) {
            return i;
        }
    }

    // Never going to happen
    Error_Handler();
    return 0;
}

// Getters
uint8_t GW_GetCurrentHour(void) {
    ReadRTC();

    return TM.tm_hour;
}

uint8_t GW_GetCurrentMinute(void) {
    ReadRTC();

    return TM.tm_min;
}

uint8_t GW_GetCurrentSecond(void) {
    ReadRTC();

    return TM.tm_sec;
}

uint8_t GW_GetCurrentSubSeconds(void) {
    ReadRTC();

    return subSecond;
}

uint8_t GW_GetCurrentMonth(void) {
    ReadRTC();

    return TM.tm_mon + MIN_RTC_MONTH; // Compatible with RTC layout (jan = 1, dec = 12)
}

uint8_t GW_GetCurrentDay(void) {
    ReadRTC();

    return TM.tm_mday;
}

uint8_t GW_GetCurrentWeekday(void) {
    ReadRTC();

    return ((TM.tm_wday + MAX_TM_WEEKDAY) % MAX_RTC_WEEKDAY) + MIN_RTC_WEEKDAY; // Compatible with RTC layout (monday = 1, Sunday = 7)
}

uint8_t GW_GetCurrentYear(void) {
    ReadRTC();

    return TM.tm_year - MIN_TM_YEAR;
}

// Setters
void GW_AddToCurrentHour(const int8_t direction) {
    ReadRTC();

    TM.tm_hour += direction;
    if (TM.tm_hour > MAX_TM_HOUR) {
        TM.tm_hour = MIN_TM_HOUR;
    } else if (TM.tm_hour < MIN_TM_HOUR) {
        TM.tm_hour = MAX_TM_HOUR;
    }

    UpdateRTC();
}

void GW_AddToCurrentMinute(const int8_t direction) {
    ReadRTC();

    TM.tm_min += direction;
    if (TM.tm_min > MAX_TM_MIN) {
        TM.tm_min = MIN_TM_MIN;
    } else if (TM.tm_min < MIN_TM_MIN) {
        TM.tm_min = MAX_TM_MIN;
    }

    UpdateRTC();
}

void GW_AddToCurrentSecond(const int8_t direction) {
    ReadRTC();

    TM.tm_sec += direction;
    if (TM.tm_sec > MAX_TM_SEC) {
        TM.tm_sec = MIN_TM_SEC;
    } else if (TM.tm_sec < MIN_TM_SEC) {
        TM.tm_sec = MAX_TM_SEC;
    }

    UpdateRTC();
}

void GW_AddToCurrentMonth(const int8_t direction) {
    ReadRTC();

    uint8_t mday = TM.tm_mday;

    TM.tm_mon += direction;
    if (TM.tm_mon > MAX_TM_MON) {
        TM.tm_mon = MIN_TM_MON;
    } else if (TM.tm_mon < MIN_TM_MON) {
        TM.tm_mon = MAX_TM_MON;
    }

    UpdateRTC();

    // Fixup from long to short month transition like 2000-01-31 -> 2000-02-29 or 2000-05-31 -> 2000-04-30
    if (TM.tm_mday != mday) {
        TM.tm_mon--;
        TM.tm_mday = GetMaxDayOfCurrentMonth();

        UpdateRTC();
    }
}

void GW_AddToCurrentDay(const int8_t direction) {
    ReadRTC();

    uint8_t maxDay = GetMaxDayOfCurrentMonth();
    TM.tm_mday += direction;
    if (TM.tm_mday > maxDay) {
        TM.tm_mday = MIN_TM_DAY;
    } else if (TM.tm_mday < MIN_TM_DAY) {
        TM.tm_mday = maxDay;
    }

    UpdateRTC();
}

void GW_AddToCurrentYear(const int8_t direction) {
    ReadRTC();

    TM.tm_year += direction;
    if (TM.tm_year > MAX_TM_YEAR) {
        TM.tm_year = MIN_TM_YEAR;
    } else if (TM.tm_year < MIN_TM_YEAR) {
        TM.tm_year = MAX_TM_YEAR;
    }

    UpdateRTC();
}

// Function to return Unix timestamp since 1st Jan 1970.
// The time is returned as an 64-bit value, but only the lower 32-bits are populated.
time_t GW_GetUnixTime(void) {
    ReadRTC();

    return mktime(&TM);
}

void GW_GetUnixTM(struct tm *tm) {
    ReadRTC();

    *tm = TM; // Clone
}

void GW_SetUnixTM(struct tm *tm) {
    TM = *tm; // Clone

    UpdateRTC();
}


// Millis since 1st Jan 1970.
uint64_t GW_GetCurrentMillis(void) {
    ReadRTC();

    return (uint64_t) mktime(&TM) * 1000 + (subSecond * 1000 / 256);
}
