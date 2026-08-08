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

    // Shared (firmware overlays); core-specific strings are in *_i18n.c
    .s_Reset = "Reset",
    .s_Palette = "Палитра",
    .s_Default = "По умолчанию",

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
    .s_Info = "Инфо",
    .s_Name = "Имя",
    .s_Version = "Версия",
    .fmt_Title_Date_Format = ru_ru_fmt_Title_Date_Format,
    .fmtDate = ru_ru_fmt_Date,
    .fmtTime = ru_ru_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};

#endif
