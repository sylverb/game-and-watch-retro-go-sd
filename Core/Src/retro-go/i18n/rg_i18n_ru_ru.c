//#include "rg_i18n_lang.h"
//Stand Russian


int ru_ru_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, day, month, weekday, hour, minutes, seconds);
};

int ru_ru_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, day, month, year, weekday);
};

int ru_ru_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_ru_ru LANG_DATA = {
    .codepage = 1251,
    .s_LangUI = "Язык",
    .s_LangName = "Russian",

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
    .s_Palette = "Палитра",
    .s_System = "Система",
    .s_SGB_Border = "Рамка SGB",
    //=====================================================================

    // Core\Src\porting\nes\main_nes.c =====================================
    //.s_Palette= "Palette" dul
    .s_Default = "По умолчанию",
    //=====================================================================

    // Core\Src\porting\pkmini\main_pkmini.c ==============================
    .s_pkmini_LCD_Filter = "Фильтр LCD",
    .s_pkmini_LCD_Mode = "Режим LCD",
    .s_pkmini_Piezo_Filter = "Фильтр Пьезо",
    .s_pkmini_Low_Pass_Filter = "Фильтр Низких Частот",
    // PokeMini palette names
    .s_pkmini_palette_Default = "По умолчанию",
    .s_pkmini_palette_Old = "Старый",
    .s_pkmini_palette_BlackWhite = "Чёрно-белый",
    .s_pkmini_palette_Green = "Зелёный",
    .s_pkmini_palette_InvertedGreen = "Инвертированный зелёный",
    .s_pkmini_palette_Red = "Красный",
    .s_pkmini_palette_InvertedRed = "Инвертированный красный",
    .s_pkmini_palette_BlueLCD = "Синий LCD",
    .s_pkmini_palette_LEDBacklight = "LED подсветка",
    .s_pkmini_palette_GirlPower = "Girl Power",
    .s_pkmini_palette_Blue = "Синий",
    .s_pkmini_palette_InvertedBlue = "Инвертированный синий",
    .s_pkmini_palette_Sepia = "Сепия",
    .s_pkmini_palette_InvertedBlackWhite = "Инвертированный чёрно-белый",
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
    .s_md_Region = "Регион",
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
    .s_copy_RTC_to_GW_time = "Копировать RTC в время G&W",
    .s_copy_GW_time_to_RTC = "Копировать время G&W в RTC",
    .s_LCD_filter = "Фильтр LCD",
    .s_Display_RAM = "Показать RAM",
    .s_Press_ACL = "Нажмите ACL или перезагрузите",
    .s_Press_TIME = "Нажмите TIME [B+TIME]",
    .s_Press_ALARM = "Нажмите ALARM [B+GAME]",
    .s_filter_0_none = "0-none",     //?
    .s_filter_1_medium = "1-medium", //?
    .s_filter_2_high = "2-high",     //?
    //=====================================================================

    // Core\Src\porting\odroid_overlay.c ==================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",
    .s_No_Cover = "Нет обложки",
    .s_Yes = "Да",
    .s_No = "Нет",
    .s_PlsChose = "Вопрос",
    .s_OK = "ОК",
    .s_Confirm = "Подтвердить",
    .s_Brightness = "Яркость",
    .s_Volume = "Громкость",
    .s_OptionsTit = "Опции",
    .s_FPS = "FPS",
    .s_BUSY = "BUSY",
    .s_Scaling = "Мастшабирование",
    .s_SCalingOff = "Выкл",
    .s_SCalingFit = "Заполнение", //?
    .s_SCalingFull = "Полное",
    .s_SCalingCustom = "Свое",
    .s_Filtering = "Фильтрация",
    .s_FilteringNone = "Нет",
    .s_FilteringOff = "Выкл",
    .s_FilteringSharp = "Резкая",
    .s_FilteringSoft = "Мягкая",
    .s_Speed = "Скорость",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "Сохранить и продолжить",
    .s_Save_Quit = "Сохранить и выйти",
    .s_Reload = "Перезагрузить",
    .s_Options = "Опции",
    .s_Power_off = "Выключить",
    .s_Quit_to_menu = "Выйти в меню",
    .s_Retro_Go_options = "Retro-Go SD",
    .s_Font = "Шрифт",
    .s_Colors = "Цвета",
    .s_Theme_Title = "Тема интерфейса",
    .s_Theme_sList = "Простой список",
    .s_Theme_CoverV = "Coverflow V",
    .s_Theme_CoverH = "Coverflow H",
    .s_Theme_CoverLightV = "CoverLight V",
    .s_Theme_CoverLightH = "CoverLight H",
    .s_Caching_Game = "Кэширование игры",
    .s_Loading_Banner = "Loading",
    .s_Pause_Banner = "PAUSE",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================
    .s_File = "Файл",
    .s_Type = "Тип",
    .s_Size = "Размер",
    .s_Close = "Закрыть",
    .s_Delete_Rom_File = "Delete ROM",
    .s_Delete_Rom_File_Confirm = "Delete '%s'?",
    .s_GameProp = "Соотношение",
    .s_Resume_game = "Продолжить игру",
    .s_New_game = "Новая игра",
    .s_Del_favorite = "Удалить избранное",
    .s_Add_favorite = "Добавить избранное",
    .s_Delete_save = "Удалить сохранение",
    .s_Confirm_del_save = "Удалить файл сохранения?",
    .s_Confirm_del_sram = "Delete SRAM file?",
    .s_Free_space_alert = "Not enough free space for a new save, please delete some.",
    .s_Corrupted_Title = "Обнаружена поврежденная установка",
    .s_Corrupted_Install_1 = "переустановите",
    .s_Corrupted_Install_2 = "Retro-Go-SD",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "Game Genie Коды",
    .s_Cheat_Codes_Title = "GG Опции",
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
    .s_Second_Unit = "c",
    .s_Author = "от",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "UI Mod",
    .s_Lang = "Russian",
    .s_LangAuthor = "teuchezh",
    .s_Debug_menu = "Иеню отладки",
    .s_Reset_settings = "Сбросить настройки",
    .s_Patreon_menu = "Patreon / новости",
    .s_Retro_Go = "Об %s",
    .s_Confirm_Reset_settings = "Сбросить все настройки?",
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
    .s_Debug_Title = "Отладка",
    .s_Idle_power_off = "Отключение в простое",
    .s_Time = "Время",
    .s_Date = "Дата",
    .s_Time_Title = "ВРЕМЯ",
    .s_Hour = "Часы",
    .s_Minute = "Минуты",
    .s_Second = "Секунды",
    .s_Time_setup = "Настройка времени",
    .s_Day = "День",
    .s_Month = "Месяц",
    .s_Year = "Год",
    .s_Weekday = "День недели",
    .s_Date_setup = "Настройка даты",
    .s_Weekday_Mon = "Пн",
    .s_Weekday_Tue = "Вт",
    .s_Weekday_Wed = "Ср",
    .s_Weekday_Thu = "Чт",
    .s_Weekday_Fri = "Пт",
    .s_Weekday_Sat = "Сб",
    .s_Weekday_Sun = "Вс",
    .s_Turbo_Button = "Turbo",
    .s_Turbo_None = "None",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",
    .s_Title_Date_Format = "%02d-%02d %s %02d:%02d:%02d",
    .s_Date_Format = "%02d.%02d.20%02d %s",
    .s_Time_Format = "%02d:%02d:%02d",
    .s_favorite = "Избранное",
    .fmt_Title_Date_Format = ru_ru_fmt_Title_Date_Format,
    .fmtDate = ru_ru_fmt_Date,
    .fmtTime = ru_ru_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};

#endif
