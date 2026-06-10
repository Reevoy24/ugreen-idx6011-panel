#include "i18n.h"
#include <string.h>

static int lang_en = 0; /* 0 = de (default), 1 = en */

static const char *strings[TR_COUNT][2] = {
    /*                          de                          en               */
    [TR_HARDWARE]         = { "Hardware",                 "Hardware" },
    [TR_NETWORK]          = { "Netzwerk",                 "Network" },
    [TR_DISKS]            = { "Festplatten",              "Disks" },
    [TR_PROXMOX]          = { "Proxmox",                  "Proxmox" },
    [TR_OPNSENSE]         = { "OPNsense",                 "OPNsense" },
    [TR_LOAD]             = { "Auslastung",               "Load" },
    [TR_TEMPERATURE]      = { "Temperatur",               "Temperature" },
    [TR_MEMORY]           = { "Arbeitsspeicher",          "Memory" },
    [TR_UPTIME]           = { "Uptime",                   "Uptime" },
    [TR_TOTAL]            = { "Insgesamt",                "Total" },
    [TR_CAP]              = { "Kap.",                     "Cap." },
    [TR_TEMP_SHORT]       = { "Temp.",                    "Temp." },
    [TR_VMS_ACTIVE]       = { "VMs aktiv",                "VMs active" },
    [TR_LXC_ACTIVE]       = { "LXC aktiv",                "LXC active" },
    [TR_NO_DISKS]         = { "Keine Laufwerke gefunden", "No drives found" },
    [TR_NO_PVE]           = { "Kein Proxmox erkannt",     "No Proxmox detected" },
    [TR_DISK_N]           = { "Festplatte %d",            "Disk %d" },
    [TR_NVME_N]           = { "NVMe %d",                  "NVMe %d" },
    [TR_NOT_AVAILABLE]    = { "inaktiv",                  "unavailable" },
    [TR_BRIGHTNESS]       = { "Helligkeit",               "Brightness" },
    [TR_SCREEN_OFF]       = { "Bildschirm aus",           "Screen off" },
    [TR_WALLPAPER]        = { "Hintergrundbild",          "Wallpaper" },
    [TR_LANGUAGE]         = { "Sprache",                  "Language" },
    [TR_RESTART]          = { "Neu starten",              "Restart" },
    [TR_SHUTDOWN]         = { "Herunterfahren",           "Shut down" },
    [TR_NEVER]            = { "Nie",                      "Never" },
    [TR_YES]              = { "Ja",                       "Yes" },
    [TR_CANCEL]           = { "Abbrechen",                "Cancel" },
    [TR_CONFIRM_RESTART]  = { "Wirklich neu starten?",    "Really restart?" },
    [TR_CONFIRM_SHUTDOWN] = { "Wirklich herunterfahren?", "Really shut down?" },
    [TR_NONE]             = { "Kein",                     "None" },
    [TR_CUSTOM]           = { "Eigenes",                  "Custom" },
};

const char *tr(tr_key_t key)
{
    if (key < 0 || key >= TR_COUNT) return "?";
    return strings[key][lang_en];
}

void i18n_set_language(const char *lang)
{
    lang_en = (lang && strncmp(lang, "en", 2) == 0) ? 1 : 0;
}

const char *i18n_get_language(void)
{
    return lang_en ? "en" : "de";
}
