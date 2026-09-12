//#include "rg_i18n_lang.h"
//Stand Spanish

int es_es_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, day, month, weekday, hour, minutes, seconds);
};

int es_es_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, day, month, year, weekday);
};

int es_es_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_es_es LANG_DATA = {
    .codepage = 1252,
    .s_LangUI = "Idioma",
    .s_LangName = "Spanish",

    // Shared (firmware overlays); core-specific strings are in *_i18n.c
    .s_Reset = "Reset",
    .s_Palette = "Paleta",
    .s_Default = "Por defecto",

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",
    .s_No_Cover = "Sin imagen",
    .s_Yes = "Si",
    .s_No = "No",
    .s_PlsChose = "Pregunta",
    .s_OK = "OK",
    .s_Confirm = "Confirmar",
    .s_Brightness = "Brillo",
    .s_Volume = "Volumen",
    .s_OptionsTit = "Opciones",
    .s_FPS = "FPS",
    .s_BUSY = "OCUPADO",
    .s_Scaling = "Escalado",
    .s_SCalingOff = "Apagado",
    .s_SCalingFit = "Escala",
    .s_SCalingFull = "Completa",
    .s_SCalingCustom = "Personal",
    .s_Filtering = "Filtro",
    .s_FilteringNone = "Ninguno",
    .s_FilteringOff = "Apagado",
    .s_FilteringSharp = "Agudo",
    .s_FilteringSoft = "Suave",
    .s_Speed = "Velocidad",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "Salvar y Continuar",
    .s_Save_Quit = "Salvar y Quitar",
    .s_Reload = "Recargar",
    .s_Options = "Opciones",
    .s_Power_off = "Apagar",
    .s_Quit_to_menu = "Volver al menu",
    .s_Retro_Go_options = "Retro-Go SD",
    .s_Font = "Tipo de letra",
    .s_Colors = "Colores",
    .s_Theme_Title = "UI Tema",
    .s_Theme_sList = "Listado",
    .s_Theme_CoverV = "Imagen Flow V",
    .s_Theme_CoverH = "Imagen Flow H",
    .s_Theme_CoverLightV = "Imagen simple V",
    .s_Theme_CoverLightH = "Imagen simple H",
    .s_Caching_Game = "Almacenando en caché el juego",
    .s_Loading_Banner = "Loading",
    .s_Pause_Banner = "PAUSE",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================
    .s_File = "Archivo",
    .s_Type = "Tipo",
    .s_Size = "Tamaño",
    .s_Close = "Cerrar",
    .s_Delete_Rom_File = "Delete ROM",
    .s_Delete_Rom_File_Confirm = "Delete '%s'?",
    .s_GameProp = "Propiedades",
    .s_Resume_game = "Continuar",
    .s_New_game = "Nuevo juego",
    .s_Del_favorite = "Borrar favorito",
    .s_Add_favorite = "Añadir favorito",
    .s_Delete_save = "Borrar guardado",
    .s_Confirm_del_save = "¿Borrar guardado?",
    .s_Confirm_del_sram = "¿Delete SRAM file?",
    .s_Free_space_alert = "Not enough free space for a new save, please delete some.",
    .s_Corrupted_Title = "Instalación corrupta detectada",
    .s_Corrupted_Install_1 = "reinstala",
    .s_Corrupted_Install_2 = "Retro-Go-SD",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "Códigos Cheat",
    .s_Cheat_Codes_Title = "Opciones Cheat",
#endif
    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "Overclock CPU",
    .s_CPU_Overclock_0 = "Stock",
    .s_CPU_Overclock_1 = "Medio",
    .s_CPU_Overclock_2 = "Máximo",
#if INTFLASH_BANK == 2
    .s_Reboot = "Reiniciar",
    .s_Original_system = "Sistema original",
    .s_Confirm_Reboot = "¿Confirmar Reinicio?",
#endif
    .s_Second_Unit = "s",
    .s_Author = "Por",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "UI Mod",
    .s_Lang = "Español",
    .s_LangAuthor = "Ninoh-FOX",
    .s_Debug_menu = "Debug_menu",
    .s_Reset_settings = "Resetear configuración",
    .s_Patreon_menu = "Patreon / novedades",
    .s_Retro_Go = "Sobre %s",
    .s_Confirm_Reset_settings = "¿Resetear?",
    .s_Flash_JEDEC_ID = "Flash JEDEC ID",
    .s_Flash_Name = "Flash Nombre",
    .s_Flash_SR = "Flash SR",
    .s_Flash_CR = "Flash CR",
    .s_Flash_Size = "Flash Size",
    .s_Smallest_erase = "Menor borrado",
    .s_DBGMCU_IDCODE = "DBGMCU IDCODE",
    .s_DBGMCU_CR = "DBGMCU CR",
    .s_DBGMCU_clock = "DBGMCU Clock",
    .s_DBGMCU_clock_on = "On",
    .s_DBGMCU_clock_auto = "Auto",
    .s_Debug_Title = "Debug",
    .s_Idle_power_off = "Apagado automático",
    .s_Time = "Hora",
    .s_Date = "Fecha",
    .s_Time_Title = "Fecha y hora",
    .s_Hour = "Hora",
    .s_Minute = "Minuto",
    .s_Second = "Segundo",
    .s_Time_setup = "Conf. hora",
    .s_Day = "Día",
    .s_Month = "Mes",
    .s_Year = "Año",
    .s_Weekday = "Día de la semana",
    .s_Date_setup = "Configurar fecha",
    .s_Weekday_Mon = "Lun",
    .s_Weekday_Tue = "Mar",
    .s_Weekday_Wed = "Míe",
    .s_Weekday_Thu = "Jue",
    .s_Weekday_Fri = "Vie",
    .s_Weekday_Sat = "Sáb",
    .s_Weekday_Sun = "Dom",
    .s_Turbo_Button = "Turbo",
    .s_Turbo_None = "No",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",    
    .s_Title_Date_Format = "%02d-%02d %s %02d:%02d:%02d",
    .s_Date_Format = "%02d.%02d.20%02d %s",
    .s_Time_Format = "%02d:%02d:%02d",
    .s_favorite = "Favorito",
    .s_Info = "Info",
    .s_Name = "Nombre",
    .s_Version = "Versión",
    .fmt_Title_Date_Format = es_es_fmt_Title_Date_Format,
    .fmtDate = es_es_fmt_Date,
    .fmtTime = es_es_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};

#endif
