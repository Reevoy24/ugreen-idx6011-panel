#include "i18n.h"
#include <string.h>

/* Column order matches lang_codes / lang_names below. English is the default
 * (index 1). config.json / state.json store the 2-letter code, so this order
 * may change freely without breaking persisted settings. */
enum { LANG_DE, LANG_EN, LANG_ES, LANG_FR, LANG_PT, LANG_ID, LANG_COUNT };

static int cur_lang = LANG_EN; /* default English */

static const char *const lang_codes[LANG_COUNT] = {
    "de", "en", "es", "fr", "pt", "id"
};
static const char *const lang_names[LANG_COUNT] = {
    "Deutsch", "English", "Español", "Français", "Português", "Indonesia"
};

static const char *strings[TR_COUNT][LANG_COUNT] = {
    /*                       de                          en                       es                          fr                       pt                          id */
    [TR_HARDWARE]      = { "Hardware",                 "Hardware",              "Hardware",                 "Matériel",              "Hardware",                 "Perangkat Keras" },
    [TR_NETWORK]       = { "Netzwerk",                 "Network",               "Red",                      "Réseau",                "Rede",                     "Jaringan" },
    [TR_DISKS]         = { "Festplatten",              "Disks",                 "Discos",                   "Disques",               "Discos",                   "Disk" },
    [TR_PROXMOX]       = { "Proxmox",                  "Proxmox",               "Proxmox",                  "Proxmox",               "Proxmox",                  "Proxmox" },
    [TR_OPNSENSE]      = { "OPNsense",                 "OPNsense",              "OPNsense",                 "OPNsense",              "OPNsense",                 "OPNsense" },
    [TR_LOAD]          = { "Auslastung",               "Load",                  "Carga",                    "Charge",                "Carga",                    "Beban" },
    [TR_TEMPERATURE]   = { "Temperatur",               "Temperature",           "Temperatura",              "Température",            "Temperatura",              "Suhu" },
    [TR_MEMORY]        = { "Arbeitsspeicher",          "Memory",                "Memoria",                  "Mémoire",               "Memória",                  "Memori" },
    [TR_UPTIME]        = { "Uptime",                   "Uptime",                "Uptime",                   "Uptime",                "Uptime",                   "Uptime" },
    [TR_TOTAL]         = { "Insgesamt",                "Total",                 "Total",                    "Total",                 "Total",                    "Total" },
    [TR_CAP]           = { "Kap.",                     "Cap.",                  "Cap.",                     "Cap.",                  "Cap.",                     "Kap." },
    [TR_TEMP_SHORT]    = { "Temp.",                    "Temp.",                 "Temp.",                    "Temp.",                 "Temp.",                    "Suhu" },
    [TR_VMS_ACTIVE]    = { "VMs aktiv",                "VMs active",            "VMs activas",              "VM actives",            "VMs ativas",               "VM aktif" },
    [TR_LXC_ACTIVE]    = { "LXC aktiv",                "LXC active",            "LXC activos",              "LXC actifs",            "LXC ativos",               "LXC aktif" },
    [TR_NO_DISKS]      = { "Keine Laufwerke gefunden", "No drives found",       "Sin discos", "Aucun disque trouvé",   "Nenhum disco encontrado",  "Tidak ada disk ditemukan" },
    [TR_NO_PVE]        = { "Kein Proxmox erkannt",     "No Proxmox detected",   "Proxmox no detectado",    "Proxmox non détecté",   "Proxmox não detectado",    "Proxmox tidak terdeteksi" },
    [TR_DISK_N]        = { "Festplatte %d",            "Disk %d",               "Disco %d",                 "Disque %d",             "Disco %d",                 "Disk %d" },
    [TR_NVME_N]        = { "NVMe %d",                  "NVMe %d",               "NVMe %d",                  "NVMe %d",               "NVMe %d",                  "NVMe %d" },
    [TR_NOT_AVAILABLE] = { "inaktiv",                  "unavailable",           "no disponible",            "indisponible",          "indisponível",             "tidak tersedia" },
    [TR_BRIGHTNESS]    = { "Helligkeit",               "Brightness",            "Brillo",                   "Luminosité",            "Brilho",                   "Kecerahan" },
    [TR_SCREEN_OFF]    = { "Bildschirm aus",           "Screen off",            "Apagar pantalla",          "Écran éteint",          "Desligar tela",            "Matikan layar" },
    [TR_WALLPAPER]     = { "Hintergrundbild",          "Wallpaper",             "Fondo",                    "Fond d'écran",          "Papel de parede",          "Wallpaper" },
    [TR_LANGUAGE]      = { "Sprache",                  "Language",              "Idioma",                   "Langue",                "Idioma",                   "Bahasa" },
    [TR_RESTART]       = { "Neu starten",              "Restart",               "Reiniciar",                "Redémarrer",            "Reiniciar",                "Mulai ulang" },
    [TR_SHUTDOWN]      = { "Herunterfahren",           "Shut down",             "Apagar",                   "Éteindre",              "Desligar",                 "Matikan" },
    [TR_NEVER]         = { "Nie",                      "Never",                 "Nunca",                    "Jamais",                "Nunca",                    "Tidak pernah" },
    [TR_YES]           = { "Ja",                       "Yes",                   "Sí",                       "Oui",                   "Sim",                      "Ya" },
    [TR_CANCEL]        = { "Abbrechen",                "Cancel",                "Cancelar",                 "Annuler",               "Cancelar",                 "Batal" },
    [TR_CONFIRM_RESTART]  = { "Wirklich neu starten?",     "Really restart?",   "¿Reiniciar de verdad?",    "Vraiment redémarrer ?", "Reiniciar mesmo?",         "Yakin mulai ulang?" },
    [TR_CONFIRM_SHUTDOWN] = { "Wirklich herunterfahren?", "Really shut down?",  "¿Apagar de verdad?",       "Vraiment éteindre ?",   "Desligar mesmo?",          "Yakin matikan?" },
    [TR_NONE]          = { "Kein",                     "None",                  "Ninguno",                  "Aucun",                 "Nenhum",                   "Tidak ada" },
    [TR_CUSTOM]        = { "Eigenes",                  "Custom",                "Personalizado",            "Personnalisé",          "Personalizado",            "Kustom" },
    [TR_LEDS]          = { "Status-LEDs",              "Status LEDs",           "LED de estado",            "LED d'état",            "LEDs de status",           "LED status" },
    [TR_LED_NIGHT]     = { "Nachtmodus",               "Night mode",            "Modo nocturno",            "Mode nuit",             "Modo noturno",             "Mode malam" },
    [TR_ON]            = { "An",                       "On",                    "Encendido",                "Allumé",                "Ligado",                   "Nyala" },
    [TR_OFF]           = { "Aus",                      "Off",                   "Apagado",                  "Éteint",                "Desligado",                "Mati" },
    [TR_SEC_DISPLAY]   = { "Anzeige",                  "Display",               "Pantalla",                 "Affichage",             "Tela",                     "Layar" },
    [TR_SEC_LEDS]      = { "LEDs",                     "LEDs",                  "LEDs",                     "LEDs",                  "LEDs",                     "LED" },
    [TR_FANS]          = { "Lüfter",                   "Fans",                  "Ventiladores",             "Ventilateurs",          "Ventoinhas",               "Kipas" },
    [TR_SILENT]        = { "Leise",                    "Silent",                "Silencioso",               "Silencieux",            "Silencioso",               "Senyap" },
    [TR_MODE_DEFAULT]  = { "Standard",                 "Default",               "Normal",                   "Normal",                "Padrão",                   "Standar" },
    [TR_TURBO]         = { "Turbo",                    "Turbo",                 "Turbo",                    "Turbo",                 "Turbo",                    "Turbo" },
};

const char *tr(tr_key_t key)
{
    if (key < 0 || key >= TR_COUNT) return "?";
    return strings[key][cur_lang];
}

void i18n_set_language(const char *lang)
{
    if (lang) {
        for (int i = 0; i < LANG_COUNT; i++) {
            if (strcmp(lang, lang_codes[i]) == 0) { cur_lang = i; return; }
        }
    }
    cur_lang = LANG_EN; /* unknown code -> English */
}

const char *i18n_get_language(void)
{
    return lang_codes[cur_lang];
}

int i18n_language_count(void) { return LANG_COUNT; }
int i18n_language_index(void) { return cur_lang; }

const char *i18n_language_code(int idx)
{
    if (idx < 0 || idx >= LANG_COUNT) return "en";
    return lang_codes[idx];
}

const char *i18n_language_name(int idx)
{
    if (idx < 0 || idx >= LANG_COUNT) return "English";
    return lang_names[idx];
}
