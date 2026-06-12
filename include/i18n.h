#ifndef I18N_H
#define I18N_H

/* Built-in LVGL fonts only cover ASCII + ° — German strings must avoid
 * umlauts (use "Kap.", "inaktiv", ... instead of words with ae/oe/ue). */
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
void i18n_set_language(const char *lang);  /* "de" or "en" */
const char *i18n_get_language(void);

#endif
