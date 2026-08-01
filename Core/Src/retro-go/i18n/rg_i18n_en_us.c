//#include "rg_i18n_lang.h"
// Stand English


int en_us_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, day, month, weekday, hour, minutes, seconds);
};

int en_us_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, day, month, year, weekday);
};

int en_us_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

const lang_t lang_en_us LANG_DATA = {
    .codepage = 1252,
    .s_LangUI = "Language",
    .s_LangName = "English",

    // Core\Src\porting\nes-fceu\main_nes_fceu.c ===========================
    .s_Crop_Vertical_Overscan = "Crop Vertical Overscan",
    .s_Crop_Horizontal_Overscan = "Crop Horizontal Overscan",
    .s_Disable_Sprite_Limit = "Disable sprite limit",
    .s_Reset = "Reset",
    .s_NES_CPU_OC = "NES CPU Overclocking",
    .s_NES_Eject_Insert_FDS = "Eject/Insert Disk",
    .s_NES_Eject_FDS = "Eject Disk",
    .s_NES_Insert_FDS = "Insert Disk",
    .s_NES_Swap_Side_FDS = "Swap FDisk side",
    .s_NES_FDS_Side_Format = "Disk %d Side %s",
    //=====================================================================

    // Core\Src\porting\gb\main_gb.c =======================================
    .s_Palette = "Palette",
    .s_System = "System",
    .s_SGB_Border = "SGB Border",
    //=====================================================================

    // Core\Src\porting\nes\main_nes.c =====================================
    //.s_Palette = "Palette" dul
    .s_Default = "Default",
    //=====================================================================
    
    // Core\Src\porting\pkmini\main_pkmini.c ==============================
    .s_pkmini_LCD_Filter = "LCD Filter",
    .s_pkmini_LCD_Mode = "LCD Mode",
    .s_pkmini_Piezo_Filter = "Piezo Filter",
    .s_pkmini_Low_Pass_Filter = "Low Pass Filter",
    // PokeMini palette names
    .s_pkmini_palette_Default = "Default",
    .s_pkmini_palette_Old = "Old",
    .s_pkmini_palette_BlackWhite = "Black & White",
    .s_pkmini_palette_Green = "Green",
    .s_pkmini_palette_InvertedGreen = "Inverted Green",
    .s_pkmini_palette_Red = "Red",
    .s_pkmini_palette_InvertedRed = "Inverted Red",
    .s_pkmini_palette_BlueLCD = "Blue LCD",
    .s_pkmini_palette_LEDBacklight = "LED Backlight",
    .s_pkmini_palette_GirlPower = "Girl Power",
    .s_pkmini_palette_Blue = "Blue",
    .s_pkmini_palette_InvertedBlue = "Inverted Blue",
    .s_pkmini_palette_Sepia = "Sepia",
    .s_pkmini_palette_InvertedBlackWhite = "Inverted Black & White",
    // PokeMini LCD filter names
    .s_pkmini_lcd_filter_None = "None",
    .s_pkmini_lcd_filter_DotMatrix = "Dot Matrix",
    .s_pkmini_lcd_filter_Scanlines = "Scanlines",
    // PokeMini LCD mode names
    .s_pkmini_lcd_mode_Analog = "Analog",
    .s_pkmini_lcd_mode_3Shades = "3 Shades",
    .s_pkmini_lcd_mode_2Shades = "2 Shades",
    //=====================================================================
    
    // Core\Src\porting\md\main_gwenesis.c ================================
    .s_md_keydefine = "keys: A-B-C",
    .s_md_Synchro = "Synchro",
    .s_md_Synchro_Audio = "AUDIO",
    .s_md_Synchro_Vsync = "VSYNC",
    .s_md_Dithering = "Dithering",
    .s_md_Debug_bar = "Debug bar",
    .s_md_AudioFilter = "Audio Filter",
    .s_md_VideoUpscaler = "Video Upscaler",
    .s_md_Region = "Region",
    //=====================================================================

    // Core\Src\porting\md\main_wsv.c ================================
    .s_wsv_palette_Default = "Default",
    .s_wsv_palette_Amber = "Amber",
    .s_wsv_palette_Green = "Green",
    .s_wsv_palette_Blue = "Blue",
    .s_wsv_palette_BGB = "BGB",
    .s_wsv_palette_Wataroo = "Wataroo",
    //=====================================================================

    // Core\Src\porting\md\main_msx.c ================================
    .s_msx_Change_Dsk = "Change Dsk",
    .s_msx_Select_MSX = "Select MSX",
    .s_msx_MSX1_EUR = "MSX1 (EUR)",
    .s_msx_MSX2_EUR = "MSX2 (EUR)",
    .s_msx_MSX2_JP = "MSX2+ (JP)",
    .s_msx_Frequency = "Frequency",
    .s_msx_Freq_Auto = "Auto",
    .s_msx_Freq_50 = "50Hz",
    .s_msx_Freq_60 = "60Hz",
    .s_msx_A_Button = "A Button",
    .s_msx_B_Button = "B Button",
    .s_msx_Press_Key = "Press Key",
    //=====================================================================

    // Core\Src\porting\md\main_amstrad.c ================================
    .s_amd_Change_Dsk = "Change Dsk",
    .s_amd_Controls = "Controls",
    .s_amd_Controls_Joystick = "Joystick",
    .s_amd_Controls_Keyboard = "Keyboard",
    .s_amd_palette_Color = "Color",
    .s_amd_palette_Green = "Green",
    .s_amd_palette_Grey = "Grey",
    .s_amd_game_Button = "Game Button",
    .s_amd_time_Button = "Time Button",
    .s_amd_start_Button = "Start Button",
    .s_amd_select_Button = "Select Button",
    .s_amd_A_Button = "A Button",
    .s_amd_B_Button = "B Button",
    .s_amd_Press_Key = "Press Key",
    //=====================================================================

    // Core\Src\porting\gw\main_gw.c =======================================
    .s_copy_RTC_to_GW_time = "copy RTC to G&W time",
    .s_copy_GW_time_to_RTC = "copy G&W time to RTC",
    .s_LCD_filter = "LCD filter",
    .s_Display_RAM = "Display RAM",
    .s_Press_ACL = "Press ACL or reset",
    .s_Press_TIME = "Press TIME [B+TIME]",
    .s_Press_ALARM = "Press ALARM [B+GAME]",
    .s_filter_0_none = "0-none",
    .s_filter_1_medium = "1-medium",
    .s_filter_2_high = "2-high",
    //=====================================================================

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",
    .s_No_Cover = "no Cover",
    .s_Yes = "Yes",
    .s_No = "No",
    .s_PlsChose = "Question",
    .s_OK = "OK",
    .s_Confirm = "Confirm",
    .s_Brightness = "Brightness",
    .s_Volume = "Volume",
    .s_OptionsTit = "Options",
    .s_FPS = "FPS",
    .s_BUSY = "BUSY",
    .s_Scaling = "Scaling",
    .s_SCalingOff = "Off",
    .s_SCalingFit = "Fit",
    .s_SCalingFull = "Full",
    .s_SCalingCustom = "Custom",
    .s_Filtering = "Filtering",
    .s_FilteringNone = "None",
    .s_FilteringOff = "Off",
    .s_FilteringSharp = "Sharp",
    .s_FilteringSoft = "Soft",
    .s_Speed = "Speed",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "Save & Continue",
    .s_Save_Quit = "Save & Quit",
    .s_Reload = "Reload",
    .s_Options = "Options",
    .s_Power_off = "Power off",
    .s_Quit_to_menu = "Quit to menu",
    .s_Retro_Go_options = "Retro-Go SD",
    .s_Font = "Font",
    .s_Colors = "Colors",
    .s_Theme_Title = "UI Theme",
    .s_Theme_sList = "Simple List",
    .s_Theme_CoverV = "Coverflow V",
    .s_Theme_CoverH = "Coverflow H",
    .s_Theme_CoverLightV = "CoverLight V",
    .s_Theme_CoverLightH = "CoverLight H",
    .s_Caching_Game = "Caching game file",
    .s_Loading_Banner = "Loading",
    .s_Pause_Banner = "PAUSE",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================
    .s_File = "File",
    .s_Type = "Type",
    .s_Size = "Size",
    .s_Close = "Close",
    .s_Delete_Rom_File = "Delete ROM",
    .s_Delete_Rom_File_Confirm = "Delete '%s'?",
    .s_GameProp = "Properties",
    .s_Resume_game = "Resume game",
    .s_New_game = "New game",
    .s_Del_favorite = "Del favorite",
    .s_Add_favorite = "Add favorite",
    .s_Delete_save = "Delete save",
    .s_Confirm_del_save = "Delete save file?",
    .s_Confirm_del_sram = "Delete SRAM file?",
    .s_Free_space_alert = "Not enough free space for a new save, please delete some.",
    .s_Corrupted_Title = "Corrupted installation detected",
    .s_Corrupted_Install_1 = "please reinstall",
    .s_Corrupted_Install_2 = "Retro-Go-SD",
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
    .s_Author = "By",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "UI Mod",
    .s_Lang = "English",
    .s_LangAuthor = "Original",
    .s_Debug_menu = "Debug menu",
    .s_Reset_settings = "Reset settings",
    .s_Patreon_menu = "Patreon / news",
    .s_Retro_Go = "About %s",
    .s_Confirm_Reset_settings = "Reset all settings?",
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
    .s_Idle_power_off = "Idle power off",
    .s_Time = "Time",
    .s_Date = "Date",
    .s_Time_Title = "TIME",
    .s_Hour = "Hour",
    .s_Minute = "Minute",
    .s_Second = "Second",
    .s_Time_setup = "Time setup",
    .s_Day = "Day",
    .s_Month = "Month",
    .s_Year = "Year",
    .s_Weekday = "Weekday",
    .s_Date_setup = "Date setup",
    .s_Weekday_Mon = "Mon",
    .s_Weekday_Tue = "Tue",
    .s_Weekday_Wed = "Wed",
    .s_Weekday_Thu = "Thu",
    .s_Weekday_Fri = "Fri",
    .s_Weekday_Sat = "Sat",
    .s_Weekday_Sun = "Sun",
    .s_Turbo_Button = "Turbo",
    .s_Turbo_None = "None",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",
    .s_Title_Date_Format = "%02d-%02d %s %02d:%02d:%02d",
    .s_Date_Format = "%02d.%02d.20%02d %s",
    .s_Time_Format = "%02d:%02d:%02d",
    .s_favorite = "Favorite",
    .fmt_Title_Date_Format = en_us_fmt_Title_Date_Format,
    .fmtDate = en_us_fmt_Date,
    .fmtTime = en_us_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};


