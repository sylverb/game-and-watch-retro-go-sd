//#include "rg_i18n_lang.h"
// Stand German

int de_de_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, weekday, day, month, hour, minutes, seconds);
};

int de_de_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, weekday, day, month, year);
};

int de_de_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_de_de LANG_DATA = {
    .codepage = 1252,
    .s_LangUI = "Sprache",
    .s_LangName = "Deutsch",

    // Shared (firmware overlays); core-specific strings are in *_i18n.c
    .s_Reset = "Reset",
    .s_Palette = "Palette",
    .s_Default = "Standard",

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",
    .s_No_Cover = "kein Cover",
    .s_Yes = "Ja",
    .s_No = "Nein",
    .s_PlsChose = "Auswahl",
    .s_OK = "OK",
    .s_Confirm = "Bestätige",
    .s_Brightness = "Helligkeit",
    .s_Volume = "Lautstärke",
    .s_OptionsTit = "Optionen",
    .s_FPS = "FPS",
    .s_BUSY = "ausgelastet",
    .s_Scaling = "Skalierung",
    .s_SCalingOff = "aus",
    .s_SCalingFit = "einpassen",
    .s_SCalingFull = "Vollbild",
    .s_SCalingCustom = "benutzerdef.",
    .s_Filtering = "Filterung",
    .s_FilteringNone = "keine",
    .s_FilteringOff = "aus",
    .s_FilteringSharp = "scharf",
    .s_FilteringSoft = "weich",
    .s_Speed = "Geschwindigkeit",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "Speichern & fortsetzen",
    .s_Save_Quit = "Speichern & beenden",
    .s_Reload = "Neu laden",
    .s_Options = "Optionen",
    .s_Power_off = "Abschalten",
    .s_Quit_to_menu = "Verlassen (Hautptmenü)",
    .s_Retro_Go_options = "Retro-Go SD",
    .s_Font = "Schriftart",
    .s_Colors = "Farben",
    .s_Theme_Title = "UI Darstellung",
    .s_Theme_sList = "Einfache Liste",
    .s_Theme_CoverV = "Coverflow V",
    .s_Theme_CoverH = "Coverflow H",
    .s_Theme_CoverLightV = "CoverLight V",
    .s_Theme_CoverLightH = "CoverLight H",
    .s_Caching_Game = "Caching des Spiels",
    .s_Loading_Banner = "Loading",
    .s_Pause_Banner = "PAUSE",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================
    .s_File = "Datei",
    .s_Type = "Typ",
    .s_Size = "Größe",
    .s_Close = "Schließen",
    .s_Delete_Rom_File = "Delete ROM",
    .s_Delete_Rom_File_Confirm = "Delete '%s'?",
    .s_GameProp = "Eigenschaften",
    .s_Resume_game = "Spiel fortsetzen",
    .s_New_game = "Neues Spiel",
    .s_Del_favorite = "Favorit löschen",
    .s_Add_favorite = "Favorit hinzufügen",
    .s_Delete_save = "Spielstand löschen",
    .s_Confirm_del_save = "Spielstand wirklich löschen?",
    .s_Confirm_del_sram = "Delete SRAM file?",
    .s_Free_space_alert = "Not enough free space for a new save, please delete some.",
    .s_Corrupted_Title = "Beschädigte Installation erkannt",
    .s_Corrupted_Install_1 = "bitte installiere",
    .s_Corrupted_Install_2 = "Retro-Go-SD neu",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "Cheat Codes",
    .s_Cheat_Codes_Title = "Cheat Options",
#endif
    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "CPU Overclock",
    .s_CPU_Overclock_0 = "No",
    .s_CPU_Overclock_1 = "Intermediate",
    .s_CPU_Overclock_2 = "Maximum",
#if INTFLASH_BANK == 2
    .s_Reboot = "Reboot",
    .s_Original_system = "Original system",
    .s_Confirm_Reboot = "Confirm reboot?",
#endif
    .s_Second_Unit = "s",
    .s_Author = "von",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "UI Mod",
    .s_Lang = "Deutsch",
    .s_LangAuthor = "LeZerb",
    .s_Debug_menu = "Debug Menü",
    .s_Reset_settings = "Einstellungen zurücksetzen",
    .s_Patreon_menu = "Patreon / News",
    .s_Retro_Go = "Über %s",
    .s_Confirm_Reset_settings = "Alle Einstellungen zurücksetzen?",
    .s_Flash_JEDEC_ID = "Flash JEDEC ID",
    .s_Flash_Name = "Flash Name",
    .s_Flash_SR = "Flash SR",
    .s_Flash_CR = "Flash CR",
    .s_Flash_Size = "Flash Size",
    .s_Smallest_erase = "Smallest erase",
    .s_DBGMCU_IDCODE = "DBGMCU IDCODE",
     .s_DBGMCU_CR = "DBGMCU CR",
    .s_DBGMCU_clock = "DBGMCU Clock",
    .s_DBGMCU_clock_on = "On",
    .s_DBGMCU_clock_auto = "Auto",
    .s_Debug_Title = "Debug",
    .s_Idle_power_off = "Abschalten bei Leerlauf",
    .s_Time = "Zeit",
    .s_Date = "Datum",
    .s_Time_Title = "Zeit",
    .s_Hour = "Stunde",
    .s_Minute = "Minute",
    .s_Second = "Sekunde",
    .s_Time_setup = "Zeit setzen",
    .s_Day = "Tag",
    .s_Month = "Monat",
    .s_Year = "Jahr",
    .s_Weekday = "Wochentag",
    .s_Date_setup = "Datum setzen",
    .s_Weekday_Mon = "Mon",
    .s_Weekday_Tue = "Die",
    .s_Weekday_Wed = "Mit",
    .s_Weekday_Thu = "Don",
    .s_Weekday_Fri = "Fre",
    .s_Weekday_Sat = "Sam",
    .s_Weekday_Sun = "Son",
    .s_Turbo_Button = "Turbo",
    .s_Turbo_None = "None",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",
    .s_Title_Date_Format = "%s %02d.%02d. %02d:%02d:%02d",
    .s_Date_Format = "%s der %02d.%02d.20%02d",
    .s_Time_Format = "%02d:%02d:%02d",
    .s_favorite = "Favorit",
    .s_Info = "Info",
    .s_Name = "Name",
    .s_Version = "Version",
    .fmt_Title_Date_Format = de_de_fmt_Title_Date_Format,
    .fmtDate = de_de_fmt_Date,
    .fmtTime = de_de_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};

#endif
