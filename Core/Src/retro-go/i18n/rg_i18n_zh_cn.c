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
    
    // Core\Src\porting\nes-fceu\main_nes_fceu.c ===========================
   .s_Crop_Vertical_Overscan = "垂直过扫描裁切",
    .s_Crop_Horizontal_Overscan = "水平过扫描裁切",
    .s_Disable_Sprite_Limit = "禁用精灵限制",
    .s_Reset = "重置游戏",
    .s_NES_CPU_OC = "NES CPU超频",
    .s_NES_Eject_Insert_FDS = "弹出/插入磁盘",
    .s_NES_Eject_FDS = "弹出磁盘",
    .s_NES_Insert_FDS = "插入磁盘",
    .s_NES_Swap_Side_FDS = "切换磁盘盘面",
    .s_NES_FDS_Side_Format = "磁盘%d面%s",
    //=====================================================================

    // Core\Src\porting\gb\main_gb.c =======================================
    .s_Palette = "调色板",
    .s_System = "系统",
    .s_SGB_Border = "SGB 边框",
    //=====================================================================

    // Core\Src\porting\md\main_gwenesis.c ================================
    .s_md_keydefine = "按键映射 A-B-C",
    .s_md_Synchro = "同步方式",
    .s_md_Synchro_Audio = "音频",
    .s_md_Synchro_Vsync = "视频",
    .s_md_Dithering = "抖动显示",
    .s_md_Debug_bar = "测试信息",
    .s_md_AudioFilter = "音频提升",
    .s_md_VideoUpscaler = "视频提升",
    .s_md_Region = "地区",
    //=====================================================================

    // Core\Src\porting\nes\main_nes.c =====================================
    //.s_Palette= "调色板" dul
    .s_Default = "默认",
    //=====================================================================

    // Core\Src\porting\pkmini\main_pkmini.c ==============================
   .s_pkmini_LCD_Filter = "LCD滤镜",
    .s_pkmini_LCD_Mode = "LCD模式",
    .s_pkmini_Piezo_Filter = "压电滤波",
    .s_pkmini_Low_Pass_Filter = "低通滤波",
    // PokeMini palette names
   .s_pkmini_palette_Default = "预设",
    .s_pkmini_palette_Old = "旧式",
    .s_pkmini_palette_BlackWhite = "黑白",
    .s_pkmini_palette_Green = "绿色",
    .s_pkmini_palette_InvertedGreen = "绿色反转",
    .s_pkmini_palette_Red = "红色",
    .s_pkmini_palette_InvertedRed = "红色反转",
    .s_pkmini_palette_BlueLCD = "蓝色LCD",
    .s_pkmini_palette_LEDBacklight = "LED背光",
    .s_pkmini_palette_GirlPower = "她力量",
    .s_pkmini_palette_Blue = "蓝色",
    .s_pkmini_palette_InvertedBlue = "反转蓝色",
    .s_pkmini_palette_Sepia = "棕褐色",
    .s_pkmini_palette_InvertedBlackWhite = "黑白反转",
    // PokeMini LCD filter names
    .s_pkmini_lcd_filter_None = "无",
    .s_pkmini_lcd_filter_DotMatrix = "点阵",
    .s_pkmini_lcd_filter_Scanlines = "扫描线",
    // PokeMini LCD mode names
    .s_pkmini_lcd_mode_Analog = "模拟模式",
    .s_pkmini_lcd_mode_3Shades = "3级灰度",
    .s_pkmini_lcd_mode_2Shades = "2级灰度",
    //=====================================================================

    // Core\Src\porting\md\main_wsv.c ================================
    .s_wsv_palette_Default = "默认",
    .s_wsv_palette_Amber = "琥珀",
    .s_wsv_palette_Green = "绿色",
    .s_wsv_palette_Blue = "蓝色",
    .s_wsv_palette_BGB = "蓝绿",
    .s_wsv_palette_Wataroo = "瓦塔罗",
    //=====================================================================

    // Core\Src\porting\md\main_msx.c ================================
    .s_msx_Change_Dsk = "更换磁盘",
    .s_msx_Select_MSX = "选择版本",
    .s_msx_MSX1_EUR = "MSX1 (欧)",
    .s_msx_MSX2_EUR = "MSX2 (欧)",
    .s_msx_MSX2_JP = "MSX2+ (日)",
    .s_msx_Frequency = "场频",
    .s_msx_Freq_Auto = "自动",
    .s_msx_Freq_50 = "50Hz",
    .s_msx_Freq_60 = "60Hz",
    .s_msx_A_Button = "Ａ键",
    .s_msx_B_Button = "Ｂ键",
    .s_msx_Press_Key = "模拟按键",
    //=====================================================================

    // Core\Src\porting\md\main_amstrad.c ================================
    .s_amd_Change_Dsk = "更换磁盘",
    .s_amd_Controls = "控制设备",
    .s_amd_Controls_Joystick = "摇杆",
    .s_amd_Controls_Keyboard = "键盘",
    .s_amd_palette_Color = "彩色",
    .s_amd_palette_Green = "绿色",
    .s_amd_palette_Grey = "灰色",
    .s_amd_game_Button = "Game键",
    .s_amd_time_Button = "Time键",
    .s_amd_start_Button = "Start键",
    .s_amd_select_Button = "Select键",
    .s_amd_A_Button = "A键",
    .s_amd_B_Button = "B键",
    .s_amd_Press_Key = "模拟按键",
    //=====================================================================

    // Core\Src\porting\gw\main_gw.c =======================================
    .s_copy_RTC_to_GW_time = "将系统时间同步到G&W",
    .s_copy_GW_time_to_RTC = "将G&W时间同步到系统时间",
    .s_LCD_filter = "屏幕抗锯齿",
    .s_Display_RAM = "显示内存信息",
    .s_Press_ACL = "重置游戏",
    .s_Press_TIME = "模拟 TIME  键 [B+TIME]",
    .s_Press_ALARM = "模拟 ALARM 键 [B+GAME]",
    .s_filter_0_none = "关",
    .s_filter_1_medium = "中",
    .s_filter_2_high = "高",
    //=====================================================================

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

