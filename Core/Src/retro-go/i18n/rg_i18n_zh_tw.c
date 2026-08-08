// Stand 繁體中文

int zh_tw_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, month, day, weekday, hour, minutes, seconds);
};

int zh_tw_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, year, month, day, weekday);
};

int zh_tw_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_zh_tw LANG_DATA = {
    .codepage = 950,
    .s_LangUI = "語言",
    .s_LangName = "Traditional Chinese",
    
    // Shared (firmware overlays); core-specific strings are in *_i18n.c
    .s_Reset = "重置遊戲",
    .s_Palette = "調色盤",
    .s_Default = "預設",

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",

    .s_No_Cover = "無封面",

    .s_Yes = "是",
    .s_No = "否",
    .s_PlsChose = "請選擇",
    .s_OK = "確定",
    .s_Confirm = "確認",
    .s_Brightness = "螢幕亮度",
    .s_Volume = "音量",
    .s_OptionsTit = "系統設定",
    .s_FPS = "影格率",
    .s_BUSY = "CPU負載",
    .s_Scaling = "縮放",
    .s_SCalingOff = "關閉",
    .s_SCalingFit = "自適應",
    .s_SCalingFull = "延伸",
    .s_SCalingCustom = "填滿",
    .s_Filtering = "影像濾鏡",
    .s_FilteringNone = "無",
    .s_FilteringOff = "關閉",
    .s_FilteringSharp = "銳利",
    .s_FilteringSoft = "柔和",
    .s_Speed = "速度",
    .s_Speed_Unit = "倍",
    .s_Save_Cont = "儲存進度",
    .s_Save_Quit = "儲存並離開",
    .s_Reload = "重新載入",
    .s_Options = "遊戲設定",
    .s_Power_off = "關機休眠",
    .s_Quit_to_menu = "結束遊戲",
    .s_Retro_Go_options = "遊戲選項",

     .s_Font = "字體",
    .s_Colors = "配色",
    .s_Theme_Title = "介面主題",
    .s_Theme_sList = "簡潔清單",
    .s_Theme_CoverV = "封面流垂直",
    .s_Theme_CoverH = "封面流水平",
    .s_Theme_CoverLightV = "海報垂直",
    .s_Theme_CoverLightH = "海報水平",
    .s_Caching_Game = "快取遊戲中",
    .s_Loading_Banner = "載入中...",
    .s_Pause_Banner = "暫停",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================

    .s_File = "Rom名稱：",
    .s_Type = "Rom類型：",
    .s_Size = "Rom大小：",
    .s_Close = "關閉",
    .s_Delete_Rom_File = "刪除遊戲Rom",
    .s_Delete_Rom_File_Confirm = "刪除遊戲 '%s'?",
    .s_GameProp = "遊戲檔案屬性",
    .s_Resume_game = " 繼續遊戲",
    .s_New_game = " 開始遊戲",
    .s_Del_favorite = "移除收藏",
    .s_Add_favorite = "加入收藏",
    .s_Delete_save = " 刪除存檔",
    .s_Confirm_del_save = "要刪除即時存檔嗎?",
    .s_Confirm_del_sram = "要刪除SRAM存檔嗎?",
    .s_Free_space_alert = "沒有足夠的空間儲存新檔案，請刪除一些.",
    .s_Corrupted_Title = "偵測到安裝已損毀",
    .s_Corrupted_Install_1 = "請重新安裝",
    .s_Corrupted_Install_2 = "Retro-Go-SD",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "金手指碼",
    .s_Cheat_Codes_Title = "金手指",
#endif

    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "超頻",
    .s_CPU_Overclock_0 = "關閉",
    .s_CPU_Overclock_1 = "適度",
    .s_CPU_Overclock_2 = "極限",
#if INTFLASH_BANK == 2
    .s_Reboot = "重新開機",
    .s_Original_system = "原生系統",
    .s_Confirm_Reboot = "您確定要重新開機嗎？",
#endif
    .s_Second_Unit = "秒",
    .s_Author = "特別貢獻：",
    .s_Author_ = "　　　　：",
    .s_UI_Mod = "介面美化：",
    .s_Lang = "繁體中文：",
    .s_LangAuthor = "挠浆糊的",
    .s_Debug_menu = "除錯資訊",
    .s_Reset_settings = "重置設定",
    .s_Patreon_menu = "Patreon / 動態",
    .s_Retro_Go = "關於 %s",
    .s_Confirm_Reset_settings = "是否重置所有設定？",

    .s_Flash_JEDEC_ID = "儲存 JEDEC ID",
    .s_Flash_Name = "Flash晶片型號",
    .s_Flash_SR = "Flash狀態",
    .s_Flash_CR = "Flash設定",
    .s_Flash_Size = "Flash容量",
    .s_Smallest_erase = "最小清除單位",
    .s_DBGMCU_IDCODE = "除錯 MCU 元件 ID 碼暫存器",
    .s_DBGMCU_CR = "除錯 MCU 設定暫存器",
    .s_DBGMCU_clock = "除錯 MCU 時脈暫存器",
    .s_DBGMCU_clock_on = "開啟",
    .s_DBGMCU_clock_auto = "自動",
    .s_Debug_Title = "除錯選項",
    .s_Idle_power_off = "待機",

    .s_Time = "時間",
    .s_Date = "日期",
    .s_Time_Title = "時間",
    .s_Hour = "時",
    .s_Minute = "分",
    .s_Second = "秒",
    .s_Time_setup = "時間設定",

    .s_Day = "日",
    .s_Month = "月",
    .s_Year = "年",
    .s_Weekday = "星期",
    .s_Date_setup = "日期設定",

    .s_Weekday_Mon = "一",
    .s_Weekday_Tue = "二",
    .s_Weekday_Wed = "三",
    .s_Weekday_Thu = "四",
    .s_Weekday_Fri = "五",
    .s_Weekday_Sat = "六",
    .s_Weekday_Sun = "日",

    .s_Turbo_Button = "連發",
    .s_Turbo_None = "無",
    .s_Turbo_A = "Ａ",
    .s_Turbo_B = "Ｂ",
    .s_Turbo_AB = "Ａ和Ｂ",

    .s_Date_Format = "20%02d年%02d月%02d日 週%s",
    .s_Title_Date_Format = "%02d-%02d 週%s %02d:%02d:%02d",
    .s_Time_Format = "%02d:%02d:%02d",

    .s_favorite = "收藏",
    .fmt_Title_Date_Format = zh_tw_fmt_Title_Date_Format,
    .fmtDate = zh_tw_fmt_Date,
    .fmtTime = zh_tw_fmt_Time,
    //=====================================================================
};

#endif
