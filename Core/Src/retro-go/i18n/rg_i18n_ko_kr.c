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

    // Shared (firmware overlays); core-specific strings are in *_i18n.c
    .s_Reset = "리셋",
    .s_Palette = "팔레트",
    .s_Default = "기본",

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
    .s_Info = "정보",
    .s_Name = "이름",
    .s_Version = "버전",
    .fmt_Title_Date_Format = ko_kr_fmt_Title_Date_Format,
    .fmtDate = ko_kr_fmt_Date,
    .fmtTime = ko_kr_fmt_Time,
    //=====================================================================
};
#endif
