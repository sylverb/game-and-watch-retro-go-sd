#pragma once
#if !defined (CHEAT_CODES)
#define CHEAT_CODES 0
#endif
typedef struct
{
    const uint32_t codepage;
    const char *s_LangUI;
    const char *s_LangName;  //used for English name
    // Shared labels still used by firmware-resident overlays
    // (zelda3/smw Reset; classic nofrendo Palette/Default). Per-core option
    // strings for standalone cores live in Core/Src/porting/*/*_i18n.c.
    const char *s_Reset;
    const char *s_Palette;
    const char *s_Default;

    // Core\Src\porting\odroid_overlay.c ===================================
    const char *s_Option_ON;   /* toggle glyph \x6 */
    const char *s_Option_OFF;  /* toggle glyph \x5 */
    const char *s_Full;
    const char *s_Fill;
    const char *s_No_Cover;
    const char *s_Yes;
    const char *s_No;
    const char *s_PlsChose;
    const char *s_OK;
    const char *s_Confirm;
    const char *s_Brightness;
    const char *s_Volume;
    const char *s_OptionsTit;
    const char *s_FPS;
    const char *s_BUSY;
    const char *s_Scaling;
    const char *s_SCalingOff;
    const char *s_SCalingFit;
    const char *s_SCalingFull;
    const char *s_SCalingCustom;
    const char *s_Filtering;
    const char *s_FilteringNone;
    const char *s_FilteringOff;
    const char *s_FilteringSharp;
    const char *s_FilteringSoft;
    const char *s_Speed;
    const char *s_Speed_Unit;
    const char *s_Save_Cont;
    const char *s_Save_Quit;
    const char *s_Reload;
    const char *s_Options;
    const char *s_Power_off;
    const char *s_Quit_to_menu;
    const char *s_Retro_Go_options;
    const char *s_Font;
    const char *s_Colors;
    const char *s_Theme_Title;
    const char *s_Theme_sList;
    const char *s_Theme_CoverV;
    const char *s_Theme_CoverH;
    const char *s_Theme_CoverLightV;
    const char *s_Theme_CoverLightH;
    const char *s_Caching_Game;
    const char *s_Loading_Banner;
    const char *s_Pause_Banner;
    //=====================================================================
    // Core\Src\retro-go\rg_emulators.c ====================================
    const char *s_File;
    const char *s_Type;
    const char *s_Size;
    const char *s_Close;
    const char *s_Delete_Rom_File;
    const char *s_Delete_Rom_File_Confirm;
    const char *s_GameProp;
    const char *s_Resume_game;
    const char *s_New_game;
    const char *s_Del_favorite;
    const char *s_Add_favorite;
    const char *s_Delete_save;
    const char *s_Confirm_del_save;
    const char *s_Confirm_del_sram;
    const char *s_Free_space_alert;
    const char *s_Corrupted_Title;
    const char *s_Corrupted_Install_1;
    const char *s_Corrupted_Install_2;
#if CHEAT_CODES == 1
    const char *s_Cheat_Codes;
    const char *s_Cheat_Codes_Title;
#endif    
    //=====================================================================
    // Core\Src\retro-go\rg_main.c =========================================
    const char *s_CPU_Overclock;
    const char *s_CPU_Overclock_0;
    const char *s_CPU_Overclock_1;
    const char *s_CPU_Overclock_2;
#if INTFLASH_BANK == 2
    const char *s_Reboot;
    const char *s_Original_system;
    const char *s_Confirm_Reboot;
#endif
    const char *s_Second_Unit;
    const char *s_Author;
    const char *s_Author_;
    const char *s_UI_Mod;
    const char *s_Lang;
    const char *s_LangAuthor;
    const char *s_Debug_menu;
    const char *s_Reset_settings;
    const char *s_Patreon_menu;
    const char *s_Retro_Go;
    const char *s_Confirm_Reset_settings;
    const char *s_Flash_JEDEC_ID;
    const char *s_Flash_Name;
    const char *s_Flash_SR;
    const char *s_Flash_CR;
    const char *s_Flash_Size;
    const char *s_Smallest_erase;
    const char *s_DBGMCU_IDCODE;
    const char *s_DBGMCU_CR;
    const char *s_DBGMCU_clock;
    const char *s_DBGMCU_clock_on;
    const char *s_DBGMCU_clock_auto;
    const char *s_Debug_Title;
    const char *s_Idle_power_off;
    const char *s_Time;
    const char *s_Date;
    const char *s_Time_Title;
    const char *s_Hour;
    const char *s_Minute;
    const char *s_Second;
    const char *s_Time_setup;
    const char *s_Day;
    const char *s_Month;
    const char *s_Year;
    const char *s_Weekday;
    const char *s_Date_setup;
    const char *s_Weekday_Mon;
    const char *s_Weekday_Tue;
    const char *s_Weekday_Wed;
    const char *s_Weekday_Thu;
    const char *s_Weekday_Fri;
    const char *s_Weekday_Sat;
    const char *s_Weekday_Sun;
    const char *s_Title_Date_Format;
    const char *s_Date_Format;
    const char *s_Time_Format;
    const char *s_Turbo_Button;
    const char *s_Turbo_None;
    const char *s_Turbo_A;
    const char *s_Turbo_B;
    const char *s_Turbo_AB;

    // Launcher favorites tab (appended for SD .bin index compatibility)
    const char *s_favorite;

    const int (*fmt_Title_Date_Format)(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds);
    // const char *fmt_Title_Date_Format(outstr,datefmt,day,month,weekday,hour,minutes,seconds) sprintf(outstr,datefmt,day,month,weekday,hour,minutes,seconds)
    const int (*fmtDate)(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday);
    // const char *fmtDate(outstr,datefmt,day,month,year,weekday) sprintf(outstr,datefmt,day,month,year,weekday)
    const int (*fmtTime)(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds);
    //=====================================================================
    //           ------------ end ---------------
} lang_t;
