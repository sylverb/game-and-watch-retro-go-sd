//#include "rg_i18n_lang.h"
// European Portuguese

int pt_pt_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, day, month, weekday, hour, minutes, seconds);
};

int pt_pt_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, day, month, year, weekday);
};

int pt_pt_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_pt_pt LANG_DATA = {
    .codepage = 1252,
    .s_LangUI = "Idioma",
    .s_LangName = "Portuguese",

    // Shared (firmware overlays); core-specific strings are in *_i18n.c
    .s_Reset = "Reiniciar",
    .s_Palette = "Paleta",
    .s_Default = "Padrão",

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",
    .s_No_Cover = "Sem capa",
    .s_Yes = "Sim",
    .s_No = "Não",
    .s_PlsChose = "Atenção",
    .s_OK = "OK",
    .s_Confirm = "Confirmar",
    .s_Brightness = "Brilho",
    .s_Volume = "Volume",
    .s_OptionsTit = "Opções",
    .s_FPS = "FPS",
    .s_BUSY = "OCUPAÇÃO",
    .s_Scaling = "Escala",
    .s_SCalingOff = "Desligado",
    .s_SCalingFit = "Ajustar",
    .s_SCalingFull = "Preencher",
    .s_SCalingCustom = "Personalizado",
    .s_Filtering = "Filtro",
    .s_FilteringNone = "Nenhum",
    .s_FilteringOff = "Desligado",
    .s_FilteringSharp = "Afiado",
    .s_FilteringSoft = "Suave",
    .s_Speed = "Velocidade",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "Gravar e Continuar",
    .s_Save_Quit = "Gravar e Sair",
    .s_Reload = "Recarregar",
    .s_Options = "Opções",
    .s_Power_off = "Desligar",
    .s_Quit_to_menu = "Sair para o menu",
    .s_Retro_Go_options = "Retro-Go SD",
    .s_Font = "Tipo de letra",
    .s_Colors = "Cores",
    .s_Theme_Title = "Tema UI",
    .s_Theme_sList = "Lista Simples",
    .s_Theme_CoverV = "Deslizante V",
    .s_Theme_CoverH = "Deslizante H",
    .s_Theme_CoverLightV = "Encadeado V",
    .s_Theme_CoverLightH = "Encadeado H",
    .s_Caching_Game = "Armazenando em cache",
    .s_Loading_Banner = "Carregando",
    .s_Pause_Banner = "PAUSA",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================
    .s_File = "Ficheiro",
    .s_Type = "Tipo",
    .s_Size = "ROM",
    .s_Close = "Fechar",
    .s_Delete_Rom_File = "Eliminar ROM",
    .s_Delete_Rom_File_Confirm = "Eliminar '%s'?",
    .s_GameProp = "Propriedades",
    .s_Resume_game = "Continuar jogo",
    .s_New_game = "Novo jogo",
    .s_Del_favorite = "Eliminar favorito",
    .s_Add_favorite = "Adicionar favorito",
    .s_Delete_save = "Eliminar gravação",
    .s_Confirm_del_save = "Eliminar gravação?",
    .s_Confirm_del_sram = "Eliminar SRAM?",
    .s_Free_space_alert = "Espaço para gravação insuficiente, liberte algum.",
    .s_Corrupted_Title = "Instalação corrompida detectada",
    .s_Corrupted_Install_1 = "reinstale o",
    .s_Corrupted_Install_2 = "Retro-Go-SD",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "Código de Batota",
    .s_Cheat_Codes_Title = "Opções de Batota",
#endif
    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "CPU Overclock",
    .s_CPU_Overclock_0 = "Zero",
    .s_CPU_Overclock_1 = "Intermédio",
    .s_CPU_Overclock_2 = "Máximo",
#if INTFLASH_BANK == 2
    .s_Reboot = "Reiniciar",
    .s_Original_system = "Reposição do Sistema",
    .s_Confirm_Reboot = "Confirmar reinício?",
#endif
    .s_Second_Unit = "s",
    .s_Author = "Por",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "UI Mod",
    .s_Lang = "Português",
    .s_LangAuthor = "Pollux",
    .s_Debug_menu = "Menu de depuração",
    .s_Reset_settings = "Repôr configurações",
    .s_Patreon_menu = "Patreon / novidades",
    .s_Retro_Go = "Sobre %s",
    .s_Confirm_Reset_settings = "Repôr configurações?",
    .s_Flash_JEDEC_ID = "Flash JEDEC ID",
    .s_Flash_Name = "Flash Ref",
    .s_Flash_SR = "Flash SR",
    .s_Flash_CR = "Flash CR",
    .s_Flash_Size = "Flash Capacidade",
    .s_Smallest_erase = "Menor apagamento",
    .s_DBGMCU_IDCODE = "DBGMCU IDCODE",
    .s_DBGMCU_CR = "DBGMCU CR",
    .s_DBGMCU_clock = "DBGMCU Clock",
    .s_DBGMCU_clock_on = "Ligado",
    .s_DBGMCU_clock_auto = "Auto",
    .s_Debug_Title = "Depuração",
    .s_Idle_power_off = "Desligar se inativo",
    .s_Time = "Hora",
    .s_Date = "Data",
    .s_Time_Title = "Data e Hora",
    .s_Hour = "Horas",
    .s_Minute = "Minutos",
    .s_Second = "Segundos",
    .s_Time_setup = "Acertar hora",
    .s_Day = "Dia",
    .s_Month = "Mês",
    .s_Year = "Ano",
    .s_Weekday = "Dia da semana",
    .s_Date_setup = "Acertar data",
    .s_Weekday_Mon = "Seg",
    .s_Weekday_Tue = "Ter",
    .s_Weekday_Wed = "Qua",
    .s_Weekday_Thu = "Qui",
    .s_Weekday_Fri = "Sex",
    .s_Weekday_Sat = "Sáb",
    .s_Weekday_Sun = "Dom",
    .s_Turbo_Button = "Turbo",
    .s_Turbo_None = "Nenhum",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",
    .s_Title_Date_Format = "%02d-%02d %s %02d:%02d:%02d",
    .s_Date_Format = "%02d.%02d.20%02d %s",
    .s_Time_Format = "%02d:%02d:%02d",
    .s_favorite = "Favorito",
    .fmt_Title_Date_Format = pt_pt_fmt_Title_Date_Format,
    .fmtDate = pt_pt_fmt_Date,
    .fmtTime = pt_pt_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};

#endif
