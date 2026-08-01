//#include "rg_i18n_lang.h"
// Norwegian Bokmål

int no_nb_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, weekday, day, month, hour, minutes, seconds);
};

int no_nb_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, weekday, day, month, year);
};

int no_nb_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_no_nb LANG_DATA = {
    .codepage = 1252,
    .s_LangUI = "Språk",
    .s_LangName = "Norwegian",

    // Core\Src\porting\nes-fceu\main_nes_fceu.c ===========================
    .s_Crop_Vertical_Overscan = "Beskjær vertikal overscan",
    .s_Crop_Horizontal_Overscan = "Beskjær horisontal overscan",
    .s_Disable_Sprite_Limit = "Deaktiver sprite-grense",
    .s_Reset = "Tilbakestill",
    .s_NES_CPU_OC = "NES CPU-overklokking",
    .s_NES_Eject_Insert_FDS = "Løs ut / sett inn disk",
    .s_NES_Eject_FDS = "Løs ut disk",
    .s_NES_Insert_FDS = "Sett inn disk",
    .s_NES_Swap_Side_FDS = "Bytt disk-side",
    .s_NES_FDS_Side_Format = "Disk %d Side %s",
    //=====================================================================

    // Core\Src\porting\gb\main_gb.c =======================================
    .s_Palette = "Palett",
    .s_System = "System",
    .s_SGB_Border = "SGB-ramme",
    //=====================================================================

    // Core\Src\porting\nes\main_nes.c =====================================
    //.s_Palette = "Palette" dul
    .s_Default = "Standard",
    //=====================================================================
    
    // Core\Src\porting\pkmini\main_pkmini.c ==============================
    .s_pkmini_LCD_Filter = "LCD-filter",
    .s_pkmini_LCD_Mode = "LCD-modus",
    .s_pkmini_Piezo_Filter = "Piezo-filter",
    .s_pkmini_Low_Pass_Filter = "Lavpassfilter",
    // PokeMini palette names
    .s_pkmini_palette_Default = "Standard",
    .s_pkmini_palette_Old = "Gammel",
    .s_pkmini_palette_BlackWhite = "Svart-hvitt",
    .s_pkmini_palette_Green = "Grønn",
    .s_pkmini_palette_InvertedGreen = "Invertert grønn",
    .s_pkmini_palette_Red = "Rød",
    .s_pkmini_palette_InvertedRed = "Invertert rød",
    .s_pkmini_palette_BlueLCD = "Blå LCD",
    .s_pkmini_palette_LEDBacklight = "LED-bakgrunnsbelysning",
    .s_pkmini_palette_GirlPower = "Jentekraft",
    .s_pkmini_palette_Blue = "Blå",
    .s_pkmini_palette_InvertedBlue = "Invertert blå",
    .s_pkmini_palette_Sepia = "Sepia",
    .s_pkmini_palette_InvertedBlackWhite = "Invertert svart-hvitt",
    // PokeMini LCD filter names
    .s_pkmini_lcd_filter_None = "Ingen",
    .s_pkmini_lcd_filter_DotMatrix = "Punktmatrise",
    .s_pkmini_lcd_filter_Scanlines = "Skannlinjer",
    // PokeMini LCD mode names
    .s_pkmini_lcd_mode_Analog = "Analog",
    .s_pkmini_lcd_mode_3Shades = "3 nyanser",
    .s_pkmini_lcd_mode_2Shades = "2 nyanser",
    //=====================================================================
    
    // Core\Src\porting\md\main_gwenesis.c ================================
    .s_md_keydefine = "Taster: A-B-C",
    .s_md_Synchro = "Synk",
    .s_md_Synchro_Audio = "LYD",
    .s_md_Synchro_Vsync = "VSYNC",
    .s_md_Dithering = "Dithering",
    .s_md_Debug_bar = "Feilsøkingslinje",
    .s_md_AudioFilter = "Lydfilter",
    .s_md_VideoUpscaler = "Oppskaler video",
    .s_md_Region = "Region",
    //=====================================================================

    // Core\Src\porting\md\main_wsv.c ================================
    .s_wsv_palette_Default = "Standard",
    .s_wsv_palette_Amber = "Oransje",
    .s_wsv_palette_Green = "Grønn",
    .s_wsv_palette_Blue = "Blå",
    .s_wsv_palette_BGB = "BGB",
    .s_wsv_palette_Wataroo = "Wataroo",
    //=====================================================================

    // Core\Src\porting\md\main_msx.c ================================
    .s_msx_Change_Dsk = "Bytt disk",
    .s_msx_Select_MSX = "Velg MSX",
    .s_msx_MSX1_EUR = "MSX1 (EUR)",
    .s_msx_MSX2_EUR = "MSX2 (EUR)",
    .s_msx_MSX2_JP = "MSX2+ (JP)",
    .s_msx_Frequency = "Frekvens",
    .s_msx_Freq_Auto = "Auto",
    .s_msx_Freq_50 = "50Hz",
    .s_msx_Freq_60 = "60Hz",
    .s_msx_A_Button = "A-knapp",
    .s_msx_B_Button = "B-knapp",
    .s_msx_Press_Key = "Trykk tast",
    //=====================================================================

    // Core\Src\porting\md\main_amstrad.c ================================
    .s_amd_Change_Dsk = "Bytt disk",
    .s_amd_Controls = "Kontroller",
    .s_amd_Controls_Joystick = "Joystick",
    .s_amd_Controls_Keyboard = "Tastatur",
    .s_amd_palette_Color = "Farge",
    .s_amd_palette_Green = "Grønn",
    .s_amd_palette_Grey = "Grå",
    .s_amd_game_Button = "GAME-knapp",
    .s_amd_time_Button = "TIME-knapp",
    .s_amd_start_Button = "START-knapp",
    .s_amd_select_Button = "SELECT-knapp",
    .s_amd_A_Button = "A-knapp",
    .s_amd_B_Button = "B-knapp",
    .s_amd_Press_Key = "Trykk tast",
    //=====================================================================

    // Core\Src\porting\gw\main_gw.c =======================================
    .s_copy_RTC_to_GW_time = "kopier realtidsklokke til G&W-tid",
    .s_copy_GW_time_to_RTC = "kopier G&W-tid til realtidsklokke",
    .s_LCD_filter = "LCD-filter",
    .s_Display_RAM = "Vis RAM",
    .s_Press_ACL = "Trykk ACL eller tilbakestill",
    .s_Press_TIME = "Trykk TIME [B+TIME]",
    .s_Press_ALARM = "Trykk ALARM [B+GAME]",
    .s_filter_0_none = "0-ingen",
    .s_filter_1_medium = "1-middels",
    .s_filter_2_high = "2-høy",
    //=====================================================================

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",
    .s_No_Cover = "ingen omslag",
    .s_Yes = "Ja",
    .s_No = "Nei",
    .s_PlsChose = "Spørsmål",
    .s_OK = "OK",
    .s_Confirm = "Bekreft",
    .s_Brightness = "Lysstyrke",
    .s_Volume = "Volum",
    .s_OptionsTit = "Alternativer",
    .s_FPS = "FPS",
    .s_BUSY = "OPPTATT",
    .s_Scaling = "Skalering",
    .s_SCalingOff = "Av",
    .s_SCalingFit = "Tilpass",
    .s_SCalingFull = "Full",
    .s_SCalingCustom = "Egendefinert",
    .s_Filtering = "Filtrering",
    .s_FilteringNone = "Ingen",
    .s_FilteringOff = "Av",
    .s_FilteringSharp = "Skarp",
    .s_FilteringSoft = "Myk",
    .s_Speed = "Hastighet",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "Lagre og fortsett",
    .s_Save_Quit = "Lagre og avslutt",
    .s_Reload = "Last inn på nytt",
    .s_Options = "Alternativer",
    .s_Power_off = "Slå av",
    .s_Quit_to_menu = "Avslutt til hovedmeny",
    .s_Retro_Go_options = "Retro-Go SD",
    .s_Font = "Skrifttype",
    .s_Colors = "Farger",
    .s_Theme_Title = "Grensesnitt-tema",
    .s_Theme_sList = "Enkel liste",
    .s_Theme_CoverV = "Coverflow V",
    .s_Theme_CoverH = "Coverflow H",
    .s_Theme_CoverLightV = "CoverLight V",
    .s_Theme_CoverLightH = "CoverLight H",
    .s_Caching_Game = "Bufrer spillfil",
    .s_Loading_Banner = "Laster",
    .s_Pause_Banner = "PAUSE",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================
    .s_File = "Fil",
    .s_Type = "Type",
    .s_Size = "Størrelse",
    .s_Close = "Lukk",
    .s_Delete_Rom_File = "Slett ROM",
    .s_Delete_Rom_File_Confirm = "Slett '%s'?",
    .s_GameProp = "Egenskaper",
    .s_Resume_game = "Gjenoppta spill",
    .s_New_game = "Nytt spill",
    .s_Del_favorite = "Fjern favoritt",
    .s_Add_favorite = "Legg til favoritt",
    .s_Delete_save = "Slett lagring",
    .s_Confirm_del_save = "Slett lagringsfil?",
    .s_Confirm_del_sram = "Slett SRAM-fil?",
    .s_Free_space_alert = "Ikke nok ledig plass til en ny lagring, vennligst slett noe.",
    .s_Corrupted_Title = "Ødelagt installasjon oppdaget",
    .s_Corrupted_Install_1 = "vennligst installer på nytt",
    .s_Corrupted_Install_2 = "Retro-Go-SD",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "Juksekoder",
    .s_Cheat_Codes_Title = "Juksealternativer",
#endif
    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "Prosessor-overklokking",
    .s_CPU_Overclock_0 = "Nei",
    .s_CPU_Overclock_1 = "Middels",
    .s_CPU_Overclock_2 = "Maksimum",
#if INTFLASH_BANK == 2
    .s_Reboot = "Start på nytt",
    .s_Original_system = "Originalt system",
    .s_Confirm_Reboot = "Bekreft omstart?",
#endif
    .s_Second_Unit = "s",
    .s_Author = "Av",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "Grensesnitt-mod",
    .s_Lang = "Norsk",
    .s_LangAuthor = "Idar Lund",
    .s_Debug_menu = "Feilsøkingsmeny",
    .s_Reset_settings = "Tilbakestill innstillinger",
    .s_Patreon_menu = "Patreon og nyheter",
    .s_Retro_Go = "Om %s",
    .s_Confirm_Reset_settings = "Tilbakestille alle innstillinger?",
    .s_Flash_JEDEC_ID = "Flash JEDEC-ID",
    .s_Flash_Name = "Flash-navn",
    .s_Flash_SR = "Flash SR",
    .s_Flash_CR = "Flash CR",
    .s_Flash_Size = "Flash-størrelse",
    .s_Smallest_erase = "Minste sletting",
    .s_DBGMCU_IDCODE = "DBGMCU ID-KODE",
    .s_DBGMCU_CR = "DBGMCU CR",
    .s_DBGMCU_clock = "DBGMCU-klokke",
    .s_DBGMCU_clock_on = "På",
    .s_DBGMCU_clock_auto = "Auto",
    .s_Debug_Title = "Feilsøking",
    .s_Idle_power_off = "Slå av ved inaktivitet",
    .s_Time = "Tid",
    .s_Date = "Dato",
    .s_Time_Title = "TID",
    .s_Hour = "Timer",
    .s_Minute = "Minutter",
    .s_Second = "Sekunder",
    .s_Time_setup = "Tidsoppsett",
    .s_Day = "Dag",
    .s_Month = "Måned",
    .s_Year = "År",
    .s_Weekday = "Ukedag",
    .s_Date_setup = "Datooppsett",
    .s_Weekday_Mon = "Man",
    .s_Weekday_Tue = "Tir",
    .s_Weekday_Wed = "Ons",
    .s_Weekday_Thu = "Tor",
    .s_Weekday_Fri = "Fre",
    .s_Weekday_Sat = "Lør",
    .s_Weekday_Sun = "Søn",
    .s_Turbo_Button = "Turbo",
    .s_Turbo_None = "Ingen",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",
    .s_Title_Date_Format = "%s %02d/%02d %02d:%02d:%02d",
    .s_Date_Format = "%s %02d/%02d 20%02d",
    .s_Time_Format = "%02d:%02d:%02d",
    .s_favorite = "Favoritter",
    .fmt_Title_Date_Format = no_nb_fmt_Title_Date_Format,
    .fmtDate = no_nb_fmt_Date,
    .fmtTime = no_nb_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};
#endif
