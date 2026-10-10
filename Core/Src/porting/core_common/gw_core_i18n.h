/*
 * Per-core option-string i18n for standalone cores.
 *
 * Firmware exposes the active language code via ABI (`i18n_lang_code`);
 * each core keeps its own tiny translation tables and looks them up with
 * gw_i18n() — English is required, every other language is optional and
 * falls back to English automatically. Does not touch firmware lang_t.
 *
 * Example:
 *   static const gw_i18n_entry_t i18n_reset[] = {
 *       { "en", "Reset" },
 *       { "fr", "Réinitialiser" },
 *       { "es", "Reiniciar" },
 *       GW_I18N_END
 *   };
 *   options[i].label = gw_i18n(i18n_reset);
 *
 * Language tags: short ("fr", "de") match "fr_fr" / "de_de". Use the full
 * code ("zh_cn", "zh_tw", "ja_jp", "ko_kr") when the short form would be
 * ambiguous.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *lang; /* "en", "fr", "zh_cn", ... */
    const char *text;
} gw_i18n_entry_t;

#define GW_I18N_END { NULL, NULL }

/* Pick the best translation for the current firmware language.
 * entries must include an "en" (or "en_us") row. Never returns NULL
 * if that English row is present. */
const char *gw_i18n(const gw_i18n_entry_t *entries);

#ifdef __cplusplus
}
#endif
