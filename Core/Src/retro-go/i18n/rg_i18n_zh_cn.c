// Stand 简体中文

int zh_cn_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, month, day, weekday, hour, minutes, seconds);
};

int zh_cn_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, year, month, day, weekday);
};

int zh_cn_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_zh_cn LANG_DATA = {
    .codepage = 936,
    .s_LangUI = "语言",
    .s_LangName = "Simplified Chinese",
    
    // Shared (firmware overlays); core-specific strings are in *_i18n.c
    .s_Reset = "重置游戏",
    .s_Palette = "调色板",
    .s_Default = "默认",

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",

    .s_No_Cover = "无封面",

    .s_Yes = "是",
    .s_No = "否",
    .s_PlsChose = "请选择",
    .s_OK = "确定",
    .s_Confirm = "确认",
    .s_Brightness = "屏幕亮度",
    .s_Volume = "音量",
    .s_OptionsTit = "系统设置",
    .s_FPS = "帧率",
    .s_BUSY = "CPU负载",
    .s_Scaling = "缩放",
    .s_SCalingOff = "关闭",
    .s_SCalingFit = "自适应",
    .s_SCalingFull = "拉伸",
    .s_SCalingCustom = "填充",
    .s_Filtering = "图像滤镜",
    .s_FilteringNone = "无",
    .s_FilteringOff = "关闭",
    .s_FilteringSharp = "锐利",
    .s_FilteringSoft = "柔和",
    .s_Speed = "速度",
    .s_Speed_Unit = "倍",
    .s_Save_Cont = "保存进度",
    .s_Save_Quit = "保存退出",
    .s_Reload = "重新加载",
    .s_Options = "游戏设置",
    .s_Power_off = "关机休眠",
    .s_Quit_to_menu = "退出游戏",
    .s_Retro_Go_options = "游戏选项",

     .s_Font = "字体",
    .s_Colors = "配色",
    .s_Theme_Title = "界面主题",
    .s_Theme_sList = "简洁列表",
    .s_Theme_CoverV = "封面流纵向", // vertical
    .s_Theme_CoverH = "封面流横向", // horizontal
    .s_Theme_CoverLightV = "海报纵向",
    .s_Theme_CoverLightH = "海报横向",
    .s_Caching_Game = "正在缓存游戏",
    .s_Loading_Banner = "加载中...",
    .s_Pause_Banner = "暂停",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================

    .s_File = "Rom名称：",
    .s_Type = "Rom类型：",
    .s_Size = "Rom大小：",
    .s_Close = "关闭",
    .s_Delete_Rom_File = "删除游戏Rom",
    .s_Delete_Rom_File_Confirm = "删除游戏 '%s'?",
    .s_GameProp = "游戏文件属性",
    .s_Resume_game = " 继续游戏",
    .s_New_game = " 开始游戏",
    .s_Del_favorite = "移除收藏",
    .s_Add_favorite = "添加收藏",
    .s_Delete_save = " 删除存档",
    .s_Confirm_del_save = "要删除即时存档吗?",
    .s_Confirm_del_sram = "要删除SRAM存档吗?",
    .s_Free_space_alert = "没有足够的空间保存新文件，请删除一些.",
    .s_Corrupted_Title = "检测到安装已损坏",
    .s_Corrupted_Install_1 = "请重新安装",
    .s_Corrupted_Install_2 = "Retro-Go-SD",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "金手指码",
    .s_Cheat_Codes_Title = "金手指",
#endif

    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "超频",
    .s_CPU_Overclock_0 = "关闭",
    .s_CPU_Overclock_1 = "适度",
    .s_CPU_Overclock_2 = "极限",
#if INTFLASH_BANK == 2
    .s_Reboot = "重启",
    .s_Original_system = "原生系统",
    .s_Confirm_Reboot = "您确定要重启设备？",
#endif
    .s_Second_Unit = "秒",
    .s_Author = "特别贡献：",
    .s_Author_ = "　　　　：",
    .s_UI_Mod = "界面美化：",
    .s_Lang = "简体中文：",
    .s_LangAuthor = "挠浆糊的",
    .s_Debug_menu = "调试信息",
    .s_Reset_settings = "重置设定",
    .s_Patreon_menu = "Patreon / 动态",
    //.s_Close                  = "Close",
    .s_Retro_Go = "关于 %s",
    .s_Confirm_Reset_settings = "是否重置所有设置？",

    .s_Flash_JEDEC_ID = "存储 JEDEC ID",
    .s_Flash_Name = "flash芯片型号",
    .s_Flash_SR = "Flash状态",
    .s_Flash_CR = "Flash配置",
    .s_Flash_Size = "Flash容量",
    .s_Smallest_erase = "最小擦除单位",
    .s_DBGMCU_IDCODE = "调试 MCU 器件 ID 码寄存器",
    .s_DBGMCU_CR = "调试 MCU 配置寄存器",
    .s_DBGMCU_clock = "调试 MCU 时钟寄存器",
    .s_DBGMCU_clock_on = "开启",
    .s_DBGMCU_clock_auto = "自动",
    //.s_Close                  = "Close",
    .s_Debug_Title = "调试选项",
    .s_Idle_power_off = "待机",

    .s_Time = "时间",
    .s_Date = "日期",
    .s_Time_Title = "时间",
    .s_Hour = "时",
    .s_Minute = "分",
    .s_Second = "秒",
    .s_Time_setup = "时间设置",

    .s_Day = "日",
    .s_Month = "月",
    .s_Year = "年",
    .s_Weekday = "星期",
    .s_Date_setup = "日期设置",

    .s_Weekday_Mon = "一",
    .s_Weekday_Tue = "二",
    .s_Weekday_Wed = "三",
    .s_Weekday_Thu = "四",
    .s_Weekday_Fri = "五",
    .s_Weekday_Sat = "六",
    .s_Weekday_Sun = "日",

    .s_Turbo_Button = "连发",
    .s_Turbo_None = "无",
    .s_Turbo_A = "Ａ",
    .s_Turbo_B = "Ｂ",
    .s_Turbo_AB = "Ａ和Ｂ",

    .s_Date_Format = "20%02d年%02d月%02d日 周%s",
    .s_Title_Date_Format = "%02d-%02d 周%s %02d:%02d:%02d",
    .s_Time_Format = "%02d:%02d:%02d",

    .s_favorite = "收藏",
    .fmt_Title_Date_Format = zh_cn_fmt_Title_Date_Format,
    .fmtDate = zh_cn_fmt_Date,
    .fmtTime = zh_cn_fmt_Time,
    //=====================================================================
};

#endif

