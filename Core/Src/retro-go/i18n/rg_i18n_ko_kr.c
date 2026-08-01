int ko_kr_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, day, month, weekday, hour, minutes, seconds);
};

int ko_kr_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, day, month, year, weekday);
};

int ko_kr_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_ko_kr LANG_DATA = {
    .codepage = 949,
    .s_LangUI = "언어",
    .s_LangName = "Korean",
    //한국어
    // If you can translate, please feed back the translation results to me, thank you
    // translate by  Augen(히힛마스터):

    // Core\Src\porting\nes-fceu\main_nes_fceu.c ===========================
    .s_Crop_Vertical_Overscan = "수직 오버스캔 자르기",
    .s_Crop_Horizontal_Overscan = "수평 오버스캔 자르기",
    .s_Disable_Sprite_Limit = "스프라이트 제한 해제",
    .s_Reset = "리셋",
    .s_NES_CPU_OC = "NES CPU 오버클럭",
    .s_NES_Eject_Insert_FDS = "디스크 꺼내기/넣기",
    .s_NES_Eject_FDS = "디스크 꺼내기",
    .s_NES_Insert_FDS = "디스크 넣기",
    .s_NES_Swap_Side_FDS = "디스크 면 전환",
    .s_NES_FDS_Side_Format = "디스크 %d %s면",
    //=====================================================================

    // Core\Src\porting\gb\main_gb.c =======================================
    .s_Palette = "팔레트",
    .s_System = "시스템",
    .s_SGB_Border = "SGB 테두리",
    //=====================================================================

    // Core\Src\porting\nes\main_nes.c =====================================
    //.s_Palette= "Palette" dul
    .s_Default = "기본",
    //=====================================================================

    // Core\Src\porting\pkmini\main_pkmini.c ==============================
    .s_pkmini_LCD_Filter = "LCD 필터",
    .s_pkmini_LCD_Mode = "LCD 모드",
    .s_pkmini_Piezo_Filter = "피에조 필터",
    .s_pkmini_Low_Pass_Filter = "저역 통과 필터",
    // PokeMini palette names
    .s_pkmini_palette_Default = "기본",
    .s_pkmini_palette_Old = "구식",
    .s_pkmini_palette_BlackWhite = "흑백",
    .s_pkmini_palette_Green = "녹색",
    .s_pkmini_palette_InvertedGreen = "반전 녹색",
    .s_pkmini_palette_Red = "빨간색",
    .s_pkmini_palette_InvertedRed = "반전 빨간색",
    .s_pkmini_palette_BlueLCD = "파란색 LCD",
    .s_pkmini_palette_LEDBacklight = "LED 백라이트",
    .s_pkmini_palette_GirlPower = "걸 파워",
    .s_pkmini_palette_Blue = "파란색",
    .s_pkmini_palette_InvertedBlue = "반전 파란색",
    .s_pkmini_palette_Sepia = "세피아",
    .s_pkmini_palette_InvertedBlackWhite = "반전 흑백",
    // PokeMini LCD filter names
    .s_pkmini_lcd_filter_None = "없음",
    .s_pkmini_lcd_filter_DotMatrix = "도트 매트릭스",
    .s_pkmini_lcd_filter_Scanlines = "스캔라인",
    // PokeMini LCD mode names
    .s_pkmini_lcd_mode_Analog = "아날로그",
    .s_pkmini_lcd_mode_3Shades = "3단계",
    .s_pkmini_lcd_mode_2Shades = "2단계",
    //=====================================================================

    // Core\Src\porting\md\main_gwenesis.c ================================
    .s_md_keydefine = "키: A-B-C",
    .s_md_Synchro = "동기화",
    .s_md_Synchro_Audio = "오디오",
    .s_md_Synchro_Vsync = "수직동기",
    .s_md_Dithering = "디더링",
    .s_md_Debug_bar = "디버그 바",
    .s_md_AudioFilter = "오디오 필터",
    .s_md_VideoUpscaler = "비디오 업스케일러",
    .s_md_Region = "지역",
    //=====================================================================
    
    // Core\Src\porting\md\main_wsv.c ================================
    .s_wsv_palette_Default = "기본",
    .s_wsv_palette_Amber = "앰버",
    .s_wsv_palette_Green = "녹색",
    .s_wsv_palette_Blue = "파란색",
    .s_wsv_palette_BGB = "BGB",
    .s_wsv_palette_Wataroo = "Wataroo",
    //=====================================================================

    // Core\Src\porting\md\main_msx.c ================================
    .s_msx_Change_Dsk = "디스크 변경",
    .s_msx_Select_MSX = "MSX 선택",
    .s_msx_MSX1_EUR = "MSX1(EUR)",
    .s_msx_MSX2_EUR = "MSX2(EUR)",
    .s_msx_MSX2_JP = "MSX2+(JP)",
    .s_msx_Frequency = "주파수",
    .s_msx_Freq_Auto = "자동",
    .s_msx_Freq_50 = "50Hz",
    .s_msx_Freq_60 = "60Hz",
    .s_msx_A_Button = "A 버튼",
    .s_msx_B_Button = "B 버튼",
    .s_msx_Press_Key = "키 입력",
    //=====================================================================

    // Core\Src\porting\md\main_amstrad.c ================================
    .s_amd_Change_Dsk = "디스크 변경",
    .s_amd_Controls = "조작",
    .s_amd_Controls_Joystick = "조이스틱",
    .s_amd_Controls_Keyboard = "키보드",
    .s_amd_palette_Color = "컬러",
    .s_amd_palette_Green = "녹색",
    .s_amd_palette_Grey = "회색",
    .s_amd_game_Button = "게임 버튼",
    .s_amd_time_Button = "시간 버튼",
    .s_amd_start_Button = "시작 버튼",
    .s_amd_select_Button = "선택 버튼",
    .s_amd_A_Button = "A 버튼",
    .s_amd_B_Button = "B 버튼",
    .s_amd_Press_Key = "키 입력",
    //=====================================================================

    // Core\Src\porting\gw\main_gw.c =======================================
    .s_copy_RTC_to_GW_time = "RTC → G&W 시간 복사",
    .s_copy_GW_time_to_RTC = "G&W → RTC 시간 복사",
    .s_LCD_filter = "LCD 필터",
    .s_Display_RAM = "디스플레이 RAM",
    .s_Press_ACL = "ACL 또는 리셋 누르기",
    .s_Press_TIME = "TIME 누르기 [B+TIME]",
    .s_Press_ALARM = "ALARM 누르기 [B+GAME]",
    .s_filter_0_none = "0-없음",
    .s_filter_1_medium = "1-중간",
    .s_filter_2_high = "2-높음",
    //=====================================================================

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",

    .s_No_Cover = "커버 없음",

    .s_Yes = "네",
    .s_No = "아니요",
    .s_PlsChose = "선택해 주세요",
    .s_OK = "확인",
    .s_Confirm = "적용",
    .s_Brightness = "밝기",
    .s_Volume = "소리 크기",
    .s_OptionsTit = "환경 설정",
    .s_FPS = "FPS",
    .s_BUSY = "CPU",
    .s_Scaling = "스케일",
    .s_SCalingOff = "끄기",
    .s_SCalingFit = "맞춤",
    .s_SCalingFull = "전체화면",
    .s_SCalingCustom = "사용자 지정",
    .s_Filtering = "필터링",
    .s_FilteringNone = "필터링 없음",
    .s_FilteringOff = "끄기",
    .s_FilteringSharp = "선명하게",
    .s_FilteringSoft = "부드럽게",
    .s_Speed = "속도(배속)",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "저장 및 계속하기",
    .s_Save_Quit = "저장 및 종료하기",
    .s_Reload = "다시 불러오기",
    .s_Options = "설정",
    .s_Power_off = "전원 종료",
    .s_Quit_to_menu = "메뉴로 나가기",
    .s_Retro_Go_options = "Retro-Go SD",

    .s_Font = "폰트",
    .s_Colors = "색상",
    .s_Theme_Title = "UI 테마",
    .s_Theme_sList = "심플 리스트",
    .s_Theme_CoverV = "커버플로우 세로",
    .s_Theme_CoverH = "커버플로우 가로",
    .s_Theme_CoverLightV = "커버라이트 세로",
    .s_Theme_CoverLightH = "커버라이트 가로",
    .s_Caching_Game = "게임 캐싱 중",
    .s_Loading_Banner = "로딩 중",
    .s_Pause_Banner = "일시정지",

    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================
    .s_File = "파일",
    .s_Type = "형식",
    .s_Size = "크기",
    .s_Close = "닫기",
    .s_Delete_Rom_File = "ROM 삭제",
    .s_Delete_Rom_File_Confirm = "'%s' 삭제?",
    .s_GameProp = "속성",
    .s_Resume_game = "이어서 하기",
    .s_New_game = "새 게임",
    .s_Del_favorite = "즐겨찾기 삭제",
    .s_Add_favorite = "즐겨찾기 추가",
    .s_Delete_save = "저장 데이터 삭제",
    .s_Confirm_del_save = "저장 데이터를 삭제하시겠습니까?",
    .s_Confirm_del_sram = "SRAM 파일 삭제?",
    .s_Free_space_alert = "저장 공간이 부족합니다. 일부 데이터를 삭제해 주세요.",
    .s_Corrupted_Title = "손상된 설치가 감지됨",
    .s_Corrupted_Install_1 = "Retro-Go-SD를",
    .s_Corrupted_Install_2 = "다시 설치하세요",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "치트 코드",
    .s_Cheat_Codes_Title = "치트 설정",
#endif

    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "CPU 오버클럭",
    .s_CPU_Overclock_0 = "미사용",
    .s_CPU_Overclock_1 = "사용",
    .s_CPU_Overclock_2 = "최대",
#if INTFLASH_BANK == 2
    .s_Reboot = "재부팅",
    .s_Original_system = "원래 시스템",
    .s_Confirm_Reboot = "재부팅 하시겠습니까?",
#endif
    .s_Second_Unit = "초",
    .s_Author = "By",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "UI 모드",
    .s_Lang = "한국어",
    .s_LangAuthor = "Augen(히힛마스터)",
    .s_Debug_menu = "디버그 메뉴",
    .s_Reset_settings = "모든 설정 초기화",
    .s_Patreon_menu = "Patreon / 소식",
    //.s_Close                  = "닫기",
    .s_Retro_Go = "%s 정보",
    .s_Confirm_Reset_settings = "모든 설정을 재설정 하시겠습니까?",

    .s_Flash_JEDEC_ID = "플래시 JEDEC ID",
    .s_Flash_Name = "플래시 이름",
    .s_Flash_SR = "플래시 SR",
    .s_Flash_CR = "플래시 CR",
    .s_Flash_Size = "플래시 크기",
    .s_Smallest_erase = "최소 지우기 단위",
    .s_DBGMCU_IDCODE = "DBGMCU IDCODE",
    .s_DBGMCU_CR = "DBGMCU CR",
    .s_DBGMCU_clock = "DBGMCU 클럭",
    .s_DBGMCU_clock_on = "켜기",
    .s_DBGMCU_clock_auto = "자동",
    //.s_Close                  = "닫기",
    .s_Debug_Title = "디버그",
    .s_Idle_power_off = "유휴 시 전원 종료",

    .s_Time = "시간",
    .s_Date = "날짜",
    .s_Time_Title = "시간",
    .s_Hour = "시",
    .s_Minute = "분",
    .s_Second = "초",
    .s_Time_setup = "시간 설정",

    .s_Day = "일",
    .s_Month = "월",
    .s_Year = "년",
    .s_Weekday = "요일",
    .s_Date_setup = "날짜 설정",

    .s_Weekday_Mon = "월",
    .s_Weekday_Tue = "화",
    .s_Weekday_Wed = "수",
    .s_Weekday_Thu = "목",
    .s_Weekday_Fri = "금",
    .s_Weekday_Sat = "토",
    .s_Weekday_Sun = "일",

    .s_Turbo_Button = "터보",
    .s_Turbo_None = "없음",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",    

    .s_Title_Date_Format = "%02d-%02d %s %02d:%02d:%02d",
    .s_Date_Format = "%02d.%02d.20%02d %s",
    .s_Time_Format = "%02d:%02d:%02d",
    .s_favorite = "즐겨찾기",
    .fmt_Title_Date_Format = ko_kr_fmt_Title_Date_Format,
    .fmtDate = ko_kr_fmt_Date,
    .fmtTime = ko_kr_fmt_Time,
    //=====================================================================
};
#endif
