//#include "rg_i18n_lang.h"
// Jp lang

int ja_jp_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, day, month, weekday, hour, minutes, seconds);
};

int ja_jp_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, day, month, year, weekday);
};

int ja_jp_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_ja_jp LANG_DATA = {
    .codepage = 932,
    .s_LangUI = "言語",
    .s_LangName = "Japanese",
    // Core\Src\porting\gb\main_gb.c =======================================
    .s_Palette = "パレット",
    .s_System = "システム",
    .s_SGB_Border = "SGBボーダー",
    //=====================================================================

    // Core\Src\porting\nes-fceu\main_nes_fceu.c ===========================
    .s_Crop_Vertical_Overscan = "縦のオーバースキャン削除",
    .s_Crop_Horizontal_Overscan = "横のオーバースキャン削除",
    .s_Disable_Sprite_Limit = "スプライトリミット無し",
    .s_Reset = "Reset",
    .s_NES_CPU_OC = "NES CPUのオーバークロック",
    .s_NES_Eject_Insert_FDS = "ディスクの取り出し・挿入",
    .s_NES_Eject_FDS = "ディスク取り出し",
    .s_NES_Insert_FDS = "ディスク挿入",
    .s_NES_Swap_Side_FDS = "ディスクサイド交換",
    .s_NES_FDS_Side_Format = "ディスク %d - %s 面",
    //=====================================================================

    // Core\Src\porting\nes\main_nes.c =====================================
    //.s_Palette = "パレット" dul
    .s_Default = "標準",
    //=====================================================================

    // Core\Src\porting\pkmini\main_pkmini.c ==============================
    .s_pkmini_LCD_Filter = "LCD フィルター",
    .s_pkmini_LCD_Mode = "LCD モード",
    .s_pkmini_Piezo_Filter = "ピエゾフィルター",
    .s_pkmini_Low_Pass_Filter = "ローパスフィルター",
    // PokeMini palette names
    .s_pkmini_palette_Default = "デフォルト",
    .s_pkmini_palette_Old = "旧式",
    .s_pkmini_palette_BlackWhite = "白黒",
    .s_pkmini_palette_Green = "緑",
    .s_pkmini_palette_InvertedGreen = "反転緑",
    .s_pkmini_palette_Red = "赤",
    .s_pkmini_palette_InvertedRed = "反転赤",
    .s_pkmini_palette_BlueLCD = "ブルー LCD",
    .s_pkmini_palette_LEDBacklight = "LED バックライト",
    .s_pkmini_palette_GirlPower = "Girl Power",
    .s_pkmini_palette_Blue = "青",
    .s_pkmini_palette_InvertedBlue = "反転青",
    .s_pkmini_palette_Sepia = "セピア",
    .s_pkmini_palette_InvertedBlackWhite = "反転白黒",
    // PokeMini LCD filter names
    .s_pkmini_lcd_filter_None = "なし",
    .s_pkmini_lcd_filter_DotMatrix = "ドットマトリックス",
    .s_pkmini_lcd_filter_Scanlines = "スキャンライン",
    // PokeMini LCD mode names
    .s_pkmini_lcd_mode_Analog = "アナログ",
    .s_pkmini_lcd_mode_3Shades = "3 階調",
    .s_pkmini_lcd_mode_2Shades = "2 階調",
    //=====================================================================

    // Core\Src\porting\md\main_gwenesis.c ================================
    .s_md_keydefine = "keys: A-B-C",
    .s_md_Synchro = "シンクロ",
    .s_md_Synchro_Audio = "オーディオ",
    .s_md_Synchro_Vsync = "VSYNC",
    .s_md_Dithering = "ディザリング",
    .s_md_Debug_bar = "デバッグバー",
    .s_md_AudioFilter = "オーディオフィルター",
    .s_md_VideoUpscaler = "ビデオアップスケール",
    .s_md_Region = "地域",
    //=====================================================================
    
    // Core\Src\porting\md\main_wsv.c ================================
    .s_wsv_palette_Default = "標準",
    .s_wsv_palette_Amber = "琥珀",
    .s_wsv_palette_Green = "緑",
    .s_wsv_palette_Blue = "青",
    .s_wsv_palette_BGB = "BGB",
    .s_wsv_palette_Wataroo = "パレット",
    //=====================================================================

    // Core\Src\porting\md\main_msx.c ================================
    .s_msx_Change_Dsk = "Dsk交換",
    .s_msx_Select_MSX = "MSX選択",
    .s_msx_MSX1_EUR = "MSX1(EUR)",
    .s_msx_MSX2_EUR = "MSX2(EUR)",
    .s_msx_MSX2_JP = "MSX2+(日本)",
    .s_msx_Frequency = "周波数",
    .s_msx_Freq_Auto = "自動",
    .s_msx_Freq_50 = "50Hz",
    .s_msx_Freq_60 = "60Hz",
    .s_msx_A_Button = "A ボタン",
    .s_msx_B_Button = "B ボタン",
    .s_msx_Press_Key = "キーを押す",
    //=====================================================================

    // Core\Src\porting\md\main_amstrad.c ================================
    .s_amd_Change_Dsk = "Dsk交換",
    .s_amd_Controls = "コントロール",
    .s_amd_Controls_Joystick = "ジョイスティック",
    .s_amd_Controls_Keyboard = "キーボード",
    .s_amd_palette_Color = "色",
    .s_amd_palette_Green = "緑",
    .s_amd_palette_Grey = "灰色",
    .s_amd_game_Button = "Game ボタン",
    .s_amd_time_Button = "Time ボタン",
    .s_amd_start_Button = "Start ボタン",
    .s_amd_select_Button = "Select ボタン",
    .s_amd_A_Button = "A ボタン",
    .s_amd_B_Button = "B ボタン",
    .s_amd_Press_Key = "キーを押す",
    //=====================================================================

    // Core\Src\porting\gw\main_gw.c =======================================
    .s_copy_RTC_to_GW_time = "RTCをG&W時刻にコピー",
    .s_copy_GW_time_to_RTC = "G&W時刻をRTCにコピー",
    .s_LCD_filter = "LCDフィルター",
    .s_Display_RAM = "RAMを表示",
    .s_Press_ACL = "ACLを押すかリセット",
    .s_Press_TIME = "TIMEを押す [B+TIME]",
    .s_Press_ALARM = "ALARMを押す [B+GAME]",
    .s_filter_0_none = "0-なし",
    .s_filter_1_medium = "1-中",
    .s_filter_2_high = "2-高",
    //=====================================================================

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",

    .s_No_Cover = "カバー無し",

    .s_Yes = "Yes",
    .s_No = "No",
    .s_PlsChose = "選択してください",
    .s_OK = "OK",
    .s_Confirm = "確認",
    .s_Brightness = "明るさ",
    .s_Volume = "音量",
    .s_OptionsTit = "オプション",
    .s_FPS = "FPS",
    .s_BUSY = "ビジー",
    .s_Scaling = "スケーリング",
    .s_SCalingOff = "Off",
    .s_SCalingFit = "フィット",
    .s_SCalingFull = "フル",
    .s_SCalingCustom = "カスタム",
    .s_Filtering = "フィルタリング",
    .s_FilteringNone = "なし",
    .s_FilteringOff = "オフ",
    .s_FilteringSharp = "Sharp",
    .s_FilteringSoft = "Soft",
    .s_Speed = "スピード",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "セーブ+続行",
    .s_Save_Quit = "セーブ+終了",
    .s_Reload = "再読み込み",
    .s_Options = "オプション",
    .s_Power_off = "電源オフ",
    .s_Quit_to_menu = "Menuに戻る",
    .s_Retro_Go_options = "Retro-Go SD",

    .s_Font = "フォント",
    .s_Colors = "色",
    .s_Theme_Title = "テーマ",
    .s_Theme_sList = "シンプルなリスト",
    .s_Theme_CoverV = "Coverflow縦",
    .s_Theme_CoverH = "Coverflow横",
    .s_Theme_CoverLightV = "CoverLight縦",
    .s_Theme_CoverLightH = "CoverLight横",
    .s_Caching_Game = "ゲームをキャッシュ中",
    .s_Loading_Banner = "Loading",
    .s_Pause_Banner = "PAUSE",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================

    .s_File = "ファイル",
    .s_Type = "タイプ",
    .s_Size = "サイズ",
    .s_Close = "閉じる",
    .s_Delete_Rom_File = "Delete ROM",
    .s_Delete_Rom_File_Confirm = "Delete '%s'?",
    .s_GameProp = "プロパティ",
    .s_Resume_game = "続きから遊ぶ",
    .s_New_game = "最初から遊ぶ",
    .s_Del_favorite = "お気に入り削除",
    .s_Add_favorite = "お気に入り追加",
    .s_Delete_save = "セーブを削除",
    .s_Confirm_del_save = "セーブを消す？",
    .s_Confirm_del_sram = "SRAM ファイルを削除しますか？",
    .s_Free_space_alert = "Nセーブ用空き容量が不足しています。一部を削除してください",
    .s_Corrupted_Title = "破損したインストールを検出",
    .s_Corrupted_Install_1 = "Retro-Go-SD を",
    .s_Corrupted_Install_2 = "再インストールしてください",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "チートコード",
    .s_Cheat_Codes_Title = "チートオプション",
#endif

    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "CPUのOverClock",
    .s_CPU_Overclock_0 = "None",
    .s_CPU_Overclock_1 = "Mid",
    .s_CPU_Overclock_2 = "Max",
#if INTFLASH_BANK == 2
    .s_Reboot = "再起動",
    .s_Original_system = "オリジナルシステム",
    .s_Confirm_Reboot = "再起動してよろしいですか？",
#endif
    .s_Second_Unit = "s",
    .s_Author = "By",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "UI Mod",
    .s_Lang = "日本語",
    .s_LangAuthor = "標準",
    .s_Debug_menu = "デバッグメニュー",
    .s_Reset_settings = "設定をリセット",
    .s_Patreon_menu = "Patreon / お知らせ",
    //.s_Close                   = "閉じる",
    .s_Retro_Go = "%sについて",
    .s_Confirm_Reset_settings = "全ての設定をリセットしますか?",

    .s_Flash_JEDEC_ID = "フラッシュJEDEC ID",
    .s_Flash_Name = "フラッシュ名",
    .s_Flash_SR = "フラッシュSR",
    .s_Flash_CR = "フラッシュCR",
    .s_Flash_Size = "フラッシュサイズ",
    .s_Smallest_erase = "最小消去単位",
    .s_DBGMCU_IDCODE = "DBGMCU IDCODE",
    .s_DBGMCU_CR = "DBGMCU CR",
    .s_DBGMCU_clock = "DBGMCUクロック",
    .s_DBGMCU_clock_on = "オン",
    .s_DBGMCU_clock_auto = "オート",
    //.s_Close                   = "閉じる",
    .s_Debug_Title = "デバッグ",
    .s_Idle_power_off = "スリープまでの時間",

    .s_Time = "時刻",
    .s_Date = "日付",
    .s_Time_Title = "時間",
    .s_Hour = "時",
    .s_Minute = "分",
    .s_Second = "秒",
    .s_Time_setup = "時刻設定",

    .s_Day = "日",
    .s_Month = "月",
    .s_Year = "年",
    .s_Weekday = "曜日",
    .s_Date_setup = "日付設定",
            
/*
    .s_Weekday_Mon = "月",
    .s_Weekday_Tue = "火",
    .s_Weekday_Wed = "水",
    .s_Weekday_Thu = "木",
    .s_Weekday_Fri = "金",
    .s_Weekday_Sat = "土",
    .s_Weekday_Sun = "日",
*/
    .s_Weekday_Mon = "Mon",
    .s_Weekday_Tue = "Tue",
    .s_Weekday_Wed = "Wed",
    .s_Weekday_Thu = "Thu",
    .s_Weekday_Fri = "Fri",
    .s_Weekday_Sat = "Sat",
    .s_Weekday_Sun = "Sun",
            
    .s_Turbo_Button = "連射",
    .s_Turbo_None = "無し",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",
    
    .s_Title_Date_Format = "%02d-%02d %s %02d:%02d:%02d",
    .s_Date_Format = "%02d.%02d.20%02d %s",
    .s_Time_Format = "%02d:%02d:%02d",

    .s_favorite = "お気に入り",
    .fmt_Title_Date_Format = ja_jp_fmt_Title_Date_Format,
    .fmtDate = ja_jp_fmt_Date,
    .fmtTime = ja_jp_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};
#endif
