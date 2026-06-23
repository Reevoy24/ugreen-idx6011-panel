#ifndef I18N_H
#define I18N_H

/* The text fonts (sizes 14-20, in src/fonts/) include the Latin-1 accented
 * letters (U+00C0..U+00FF) plus the inverted '!' and '?', so German, Spanish,
 * French and Portuguese strings may use them freely. The big numeric fonts
 * (24/32/48) are still ASCII-only built-ins — values rendered there (numbers,
 * units, "%") must stay ASCII. */
typedef enum {
    TR_HARDWARE, TR_NETWORK, TR_DISKS, TR_PROXMOX, TR_OPNSENSE,
    TR_LOAD, TR_TEMPERATURE, TR_MEMORY, TR_UPTIME, TR_TOTAL,
    TR_CAP, TR_TEMP_SHORT, TR_VMS_ACTIVE, TR_LXC_ACTIVE,
    TR_NO_DISKS, TR_NO_PVE, TR_DISK_N, TR_NVME_N, TR_NOT_AVAILABLE,
    TR_BRIGHTNESS, TR_SCREEN_OFF, TR_WALLPAPER, TR_LANGUAGE,
    TR_RESTART, TR_SHUTDOWN, TR_NEVER, TR_YES, TR_CANCEL,
    TR_CONFIRM_RESTART, TR_CONFIRM_SHUTDOWN, TR_NONE, TR_CUSTOM,
    TR_LEDS, TR_LED_NIGHT, TR_ON, TR_OFF,
    TR_SEC_DISPLAY, TR_SEC_LEDS,
    TR_COUNT
} tr_key_t;

const char *tr(tr_key_t key);

/* Language is selected by 2-letter code: "de","en","es","fr","pt","id".
 * Unknown codes fall back to English. */
void i18n_set_language(const char *lang);
const char *i18n_get_language(void);

/* For the settings-panel language picker, which cycles through all languages. */
int i18n_language_count(void);
int i18n_language_index(void);            /* current language, 0..count-1 */
const char *i18n_language_code(int idx);  /* "de","en",... */
const char *i18n_language_name(int idx);  /* native name, "Deutsch","Español",... */

#endif
