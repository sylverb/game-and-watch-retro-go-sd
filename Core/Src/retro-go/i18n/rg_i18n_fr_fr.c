//#include "rg_i18n_lang.h"
// Stand French


int fr_fr_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, day, month, weekday, hour, minutes, seconds);
};

int fr_fr_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, day, month, year, weekday);
};

int fr_fr_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

// do not compile this part, it will be parsed by a script to create a bin file with language content
#ifdef DO_NOT_COMPILE
const lang_t lang_fr_fr LANG_DATA = {
    .codepage = 1252,
    .s_LangUI = "Langue",
    .s_LangName = "French",

    // Core\Src\porting\nes-fceu\main_nes_fceu.c ===========================
    .s_Crop_Vertical_Overscan = "Recadrage Vertical",
    .s_Crop_Horizontal_Overscan = "Recadrage Horizontal",
    .s_Disable_Sprite_Limit = "Désactiver limit. nb sprites",
    .s_Reset = "Reset",
    .s_NES_CPU_OC = "Overclocking du CPU NES",
    .s_NES_Eject_Insert_FDS = "Ejecter/Insérer le disque",
    .s_NES_Eject_FDS = "Ejecter Disque",
    .s_NES_Insert_FDS = "Insérer Disque",
    .s_NES_Swap_Side_FDS = "Changer la face du disque",
    .s_NES_FDS_Side_Format = "Disque %d Face %s",
    //=====================================================================

    // Core\Src\porting\gb\main_gb.c =======================================
    .s_Palette = "Palette",
    .s_System = "Système",
    .s_SGB_Border = "Cadre SGB",
    //=====================================================================

    // Core\Src\porting\nes\main_nes.c =====================================
    //.s_Palette = "Palette" dul
    .s_Default = "Par défaut",
    //=====================================================================

    // Core\Src\porting\pkmini\main_pkmini.c ==============================
    .s_pkmini_LCD_Filter = "Filtre LCD",
    .s_pkmini_LCD_Mode = "Mode LCD",
    .s_pkmini_Piezo_Filter = "Filtre Piezo",
    .s_pkmini_Low_Pass_Filter = "Filtre Passe-Bas",
    // PokeMini palette names
    .s_pkmini_palette_Default = "Défaut",
    .s_pkmini_palette_Old = "Vieux",
    .s_pkmini_palette_BlackWhite = "Noir et Blanc",
    .s_pkmini_palette_Green = "Vert",
    .s_pkmini_palette_InvertedGreen = "Vert Inversé",
    .s_pkmini_palette_Red = "Rouge",
    .s_pkmini_palette_InvertedRed = "Rouge Inversé",
    .s_pkmini_palette_BlueLCD = "LCD Bleu",
    .s_pkmini_palette_LEDBacklight = "Rétro-éclairage LED",
    .s_pkmini_palette_GirlPower = "Pouvoir des Filles",
    .s_pkmini_palette_Blue = "Bleu",
    .s_pkmini_palette_InvertedBlue = "Bleu Inversé",
    .s_pkmini_palette_Sepia = "Sépia",
    .s_pkmini_palette_InvertedBlackWhite = "Noir et Blanc Inversé",
    // PokeMini LCD filter names
    .s_pkmini_lcd_filter_None = "Aucun",
    .s_pkmini_lcd_filter_DotMatrix = "Matrice de points",
    .s_pkmini_lcd_filter_Scanlines = "Lignes de balayage",
    // PokeMini LCD mode names
    .s_pkmini_lcd_mode_Analog = "Analogique",
    .s_pkmini_lcd_mode_3Shades = "3 Nuances",
    .s_pkmini_lcd_mode_2Shades = "2 Nuances",
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
    .s_md_Region = "Région",
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
    .s_msx_Select_MSX = "Type de MSX",
    .s_msx_MSX1_EUR = "MSX1 (EUR)",
    .s_msx_MSX2_EUR = "MSX2 (EUR)",
    .s_msx_MSX2_JP = "MSX2+ (JP)",
    .s_msx_Frequency = "Fréquence",
    .s_msx_Freq_Auto = "Auto",
    .s_msx_Freq_50 = "50Hz",
    .s_msx_Freq_60 = "60Hz",
    .s_msx_A_Button = "Bouton A",
    .s_msx_B_Button = "Bouton B",
    .s_msx_Press_Key = "Press Key",
    //=====================================================================

    // Core\Src\porting\md\main_amstrad.c ================================
    .s_amd_Change_Dsk = "Change Dsk",
    .s_amd_Controls = "Contrôles",
    .s_amd_Controls_Joystick = "Manette",
    .s_amd_Controls_Keyboard = "Clavier",
    .s_amd_palette_Color = "Couleur",
    .s_amd_palette_Green = "Vert",
    .s_amd_palette_Grey = "Gris",
    .s_amd_game_Button = "Bouton Game",
    .s_amd_time_Button = "Bouton Time",
    .s_amd_start_Button = "Bouton Start",
    .s_amd_select_Button = "Bouton Select",
    .s_amd_A_Button = "Bouton A",
    .s_amd_B_Button = "Bouton B",
    .s_amd_Press_Key = "Press Key",
    //=====================================================================

    // Core\Src\porting\gw\main_gw.c =======================================
    .s_copy_RTC_to_GW_time = "Copie RTC vers horloge G&W",
    .s_copy_GW_time_to_RTC = "Copie temps G&W vers horloge RTC",
    .s_LCD_filter = "Filtre LCD",
    .s_Display_RAM = "Montrer la RAM",
    .s_Press_ACL = "Presser ACL ou Reset",
    .s_Press_TIME = "Presser TIME [B+TIME]",
    .s_Press_ALARM = "Presser ALARM [B+GAME]",
    .s_filter_0_none = "0-aucun",
    .s_filter_1_medium = "1-moyen",
    .s_filter_2_high = "2-élevé",
    //=====================================================================

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Option_ON = "\x6",
    .s_Option_OFF = "\x5",
    .s_Full = "\x7",
    .s_Fill = "\x8",
    .s_No_Cover = "Pas d'image",
    .s_Yes = "Oui",
    .s_No = "Non",
    .s_PlsChose = "Question",
    .s_OK = "OK",
    .s_Confirm = "Confirmer",
    .s_Brightness = "Luminosité",
    .s_Volume = "Volume",
    .s_OptionsTit = "Options",
    .s_FPS = "FPS",
    .s_BUSY = "Occupé",
    .s_Scaling = "Echelle",
    .s_SCalingOff = "Off",
    .s_SCalingFit = "Adaptée",
    .s_SCalingFull = "Complete",
    .s_SCalingCustom = "Personalisée",
    .s_Filtering = "Filtrage",
    .s_FilteringNone = "Aucun",
    .s_FilteringOff = "Off",
    .s_FilteringSharp = "Précis",
    .s_FilteringSoft = "Léger",
    .s_Speed = "Vitesse",
    .s_Speed_Unit = "x",
    .s_Save_Cont = "Sauver & Continuer",
    .s_Save_Quit = "Sauver & Quitter",
    .s_Reload = "Recharger",
    .s_Options = "Options",
    .s_Power_off = "Eteindre",
    .s_Quit_to_menu = "Quitter vers le menu",
    .s_Retro_Go_options = "Retro-Go SD",
    .s_Font = "Polices",
    .s_Colors = "Couleurs",
    .s_Theme_Title = "Theme UI",
    .s_Theme_sList = "Liste seule",
    .s_Theme_CoverV = "Galerie V",
    .s_Theme_CoverH = "Galerie H",
    .s_Theme_CoverLightV = "Mix V",
    .s_Theme_CoverLightH = "Mix H",
    .s_Caching_Game = "Mise en cache du jeu",
    .s_Loading_Banner = "Loading",
    .s_Pause_Banner = "PAUSE",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================
    .s_File = "Fichier",
    .s_Type = "Type",
    .s_Size = "Taille",
    .s_Close = "Fermer",
    .s_Delete_Rom_File = "Delete ROM",
    .s_Delete_Rom_File_Confirm = "Delete '%s'?",
    .s_GameProp = "Propriétés",
    .s_Resume_game = "Reprendre le jeu",
    .s_New_game = "Nouvelle partie",
    .s_Del_favorite = "Retirer des favoris",
    .s_Add_favorite = "Ajouter aux favoris",
    .s_Delete_save = "Supprimer la sauvegarde",
    .s_Confirm_del_save = "Supprimer la sauvegarde ?",
    .s_Confirm_del_sram = "Supprimer la SRAM ?",
    .s_Free_space_alert = "Pas assez d'espace pour une nouvelle sauvegarde, merci d'en supprimer.",
    .s_Corrupted_Title = "Installation corrompue détectée",
    .s_Corrupted_Install_1 = "veuillez réinstaller",
    .s_Corrupted_Install_2 = "Retro-Go-SD",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "Codes de triche",
    .s_Cheat_Codes_Title = "Options de triche",
#endif
    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_CPU_Overclock = "Overclocking du CPU",
    .s_CPU_Overclock_0 = "Sans",
    .s_CPU_Overclock_1 = "Moyen",
    .s_CPU_Overclock_2 = "Maximum",
#if INTFLASH_BANK == 2
    .s_Reboot = "Redémarrer",
    .s_Original_system = "système original",
    .s_Confirm_Reboot = "Confirmer redémarrage ?",
#endif
    .s_Second_Unit = "s",
    .s_Author = "De",
    .s_Author_ = "\t\t+",
    .s_UI_Mod = "UI Mod",
    .s_Lang = "Français",
    .s_LangAuthor = "Narkoa",
    .s_Debug_menu = "Menu Debug",
    .s_Reset_settings = "Restaurer les paramètres",
    .s_Patreon_menu = "Patreon / actus",
    .s_Retro_Go = "A propos de %s",
    .s_Confirm_Reset_settings = "Restaurer les paramètres ?",
    .s_Flash_JEDEC_ID = "Id Flash JEDEC",
    .s_Flash_Name = "Nom Flash",
    .s_Flash_SR = "SR Flash",
    .s_Flash_CR = "CR Flash",
    .s_Flash_Size = "Taille de la Flash",
    .s_Smallest_erase = "Plus petite suppression",
    .s_DBGMCU_IDCODE = "DBGMCU IDCODE",
    .s_DBGMCU_CR = "DBGMCU CR",
    .s_DBGMCU_clock = "DBGMCU Clock",
    .s_DBGMCU_clock_on = "On",
    .s_DBGMCU_clock_auto = "Auto",
    .s_Debug_Title = "Debug",
    .s_Idle_power_off = "Temps avant veille",
    .s_Time = "Heure",
    .s_Date = "Date",
    .s_Time_Title = "TEMPS",
    .s_Hour = "Heure",
    .s_Minute = "Minute",
    .s_Second = "Seconde",
    .s_Time_setup = "Réglage",
    .s_Day = "Jour",
    .s_Month = "Mois",
    .s_Year = "Année",
    .s_Weekday = "Jour de la semaine",
    .s_Date_setup = "Réglage Date",
    .s_Weekday_Mon = "Lun",
    .s_Weekday_Tue = "Mar",
    .s_Weekday_Wed = "Mer",
    .s_Weekday_Thu = "Jeu",
    .s_Weekday_Fri = "Ven",
    .s_Weekday_Sat = "Sam",
    .s_Weekday_Sun = "Dim",
    .s_Turbo_Button = "Turbo",
    .s_Turbo_None = "Aucun",
    .s_Turbo_A = "A",
    .s_Turbo_B = "B",
    .s_Turbo_AB = "A & B",
    .s_Title_Date_Format = "%02d-%02d %s %02d:%02d:%02d",
    .s_Date_Format = "%02d.%02d.20%02d %s",
    .s_Time_Format = "%02d:%02d:%02d",
    .s_favorite = "Favori",
    .fmt_Title_Date_Format = fr_fr_fmt_Title_Date_Format,
    .fmtDate = fr_fr_fmt_Date,
    .fmtTime = fr_fr_fmt_Time,
    //=====================================================================
    //           ------------ end ---------------
};

#endif
