#include "gw_core_i18n.h"

#include <string.h>

#include "gw_firmware_abi.h"

static int gw_i18n_lang_matches(const char *entry, const char *current)
{
    size_t n;

    if (!entry || !current)
        return 0;
    if (strcmp(entry, current) == 0)
        return 1;
    /* Short tag: "fr" matches "fr_fr", but "zh" must NOT match both
     * "zh_cn" and "zh_tw" — callers use full codes for those. */
    n = strlen(entry);
    if (n == 0)
        return 0;
    if (strncmp(entry, current, n) == 0 && current[n] == '_')
        return 1;
    return 0;
}

static int gw_i18n_is_english(const char *lang)
{
    return lang && (strcmp(lang, "en") == 0 || strcmp(lang, "en_us") == 0);
}

const char *gw_i18n(const gw_i18n_entry_t *entries)
{
    const char *lang;
    const char *en = NULL;
    const gw_i18n_entry_t *e;

    if (!entries)
        return "";

    lang = gw_firmware_abi()->i18n_lang_code();
    if (!lang)
        lang = "en_us";

    for (e = entries; e->lang != NULL; e++) {
        if (gw_i18n_is_english(e->lang))
            en = e->text;
        if (gw_i18n_lang_matches(e->lang, lang))
            return e->text ? e->text : "";
    }

    return en ? en : "";
}
