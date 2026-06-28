// UI strings. English is the default and the fallback for any missing key, so
// a language only needs to override what it translates. Mode/section terms
// reuse the device's wording (src/i18n.c) where they exist.
export const STRINGS = {
  en: {
    title: "iDX6011 Pro", subtitle: "Panel · Web", online: "Online", offline: "Panel offline",
    sec_overview: "Overview", sec_fan: "Fan control", sec_storage: "Storage",
    sec_network: "Network", sec_services: "Services", sec_settings: "Settings", sec_power: "Power",
    cpu: "CPU", system: "System", ram: "RAM", temp: "CPU temp", storage: "Storage",
    uptime: "Uptime", gpu: "GPU", applied: "Applied", fan: "Fan", rpm: "rpm", curve: "Curve",
    silent: "Silent", default: "Default", turbo: "Turbo",
    disk: "Disk", nvme: "NVMe", network_io: "Network",
    proxmox: "Proxmox", vms: "VMs", containers: "Containers", opnsense: "OPNsense",
    gateway: "Gateway", dns_blocked: "DNS blocked", leases: "DHCP leases",
    brightness: "Brightness", timeout: "Screen-off timeout", sleep_brightness: "Sleep brightness",
    language: "Language", status_leds: "Status LEDs", night_mode: "LED night mode",
    timezone: "Timezone", time_format: "Clock", start: "Start", end: "End",
    wallpaper: "Wallpaper", upload_image: "Upload image", on: "On", off: "Off", never: "Never",
    restart: "Restart", shutdown: "Shutdown", confirm: "Confirm", cancel: "Cancel",
    edit: "Edit", save: "Save", add_point: "+ Add point", saved: "Saved", deleted: "Deleted", curve_invalid: "Curve: 1–12 points, temp 0–120, speed 0–100, no duplicate temps",
    confirm_restart: "Restart the NAS now?", confirm_shutdown: "Shut down the NAS now?",
    password_prompt: "Password required:", lan_only: "LAN only — do not expose to the internet.",
    daemon_offline: "Fan daemon not running",
  },
  de: {
    title: "iDX6011 Pro", subtitle: "Panel · Web", online: "Online", offline: "Panel offline",
    sec_overview: "Übersicht", sec_fan: "Lüftersteuerung", sec_storage: "Speicher",
    sec_network: "Netzwerk", sec_services: "Dienste", sec_settings: "Einstellungen", sec_power: "Energie",
    cpu: "CPU", system: "System", ram: "RAM", temp: "CPU-Temp", storage: "Speicher",
    uptime: "Laufzeit", gpu: "GPU", applied: "Anliegend", fan: "Lüfter", rpm: "rpm", curve: "Kurve",
    silent: "Leise", default: "Standard", turbo: "Turbo",
    disk: "Platte", nvme: "NVMe", network_io: "Netzwerk",
    proxmox: "Proxmox", vms: "VMs", containers: "Container", opnsense: "OPNsense",
    gateway: "Gateway", dns_blocked: "DNS geblockt", leases: "DHCP-Leases",
    brightness: "Helligkeit", timeout: "Bildschirm-Timeout", sleep_brightness: "Schlaf-Helligkeit",
    language: "Sprache", status_leds: "Status-LEDs", night_mode: "LED-Nachtmodus",
    timezone: "Zeitzone", time_format: "Uhr", start: "Start", end: "Ende",
    wallpaper: "Hintergrund", upload_image: "Bild hochladen", on: "An", off: "Aus", never: "Nie",
    restart: "Neustart", shutdown: "Herunterfahren", confirm: "Bestätigen", cancel: "Abbrechen",
    edit: "Bearbeiten", save: "Speichern", add_point: "+ Punkt", saved: "Gespeichert", deleted: "Gelöscht", curve_invalid: "Kurve: 1–12 Punkte, Temp 0–120, Drehzahl 0–100, keine doppelten Temps",
    confirm_restart: "NAS jetzt neu starten?", confirm_shutdown: "NAS jetzt herunterfahren?",
    password_prompt: "Passwort erforderlich:", lan_only: "Nur LAN — nicht ins Internet exponieren.",
    daemon_offline: "Lüfter-Daemon läuft nicht",
  },
  es: {
    title: "iDX6011 Pro", sec_overview: "Resumen", sec_fan: "Ventiladores", sec_storage: "Almacenamiento",
    sec_network: "Red", sec_services: "Servicios", sec_settings: "Ajustes", sec_power: "Energía",
    online: "En línea", offline: "Panel desconectado", system: "Sistema", applied: "Aplicado",
    fan: "Ventilador", curve: "Curva", silent: "Silencioso", default: "Normal", turbo: "Turbo",
    brightness: "Brillo", timeout: "Apagado de pantalla", sleep_brightness: "Brillo en reposo",
    language: "Idioma", status_leds: "LED de estado", night_mode: "Modo noche LED", wallpaper: "Fondo",
    upload_image: "Subir imagen", on: "Sí", off: "No", never: "Nunca",
    restart: "Reiniciar", shutdown: "Apagar", confirm: "Confirmar", cancel: "Cancelar",
    confirm_restart: "¿Reiniciar el NAS ahora?", confirm_shutdown: "¿Apagar el NAS ahora?",
    password_prompt: "Se requiere contraseña:", lan_only: "Solo LAN — no exponer a Internet.",
    daemon_offline: "El daemon de ventiladores no está activo",
  },
  fr: {
    title: "iDX6011 Pro", sec_overview: "Aperçu", sec_fan: "Ventilateurs", sec_storage: "Stockage",
    sec_network: "Réseau", sec_services: "Services", sec_settings: "Réglages", sec_power: "Alimentation",
    online: "En ligne", offline: "Panneau hors ligne", system: "Système", applied: "Appliqué",
    fan: "Ventilateur", curve: "Courbe", silent: "Silencieux", default: "Normal", turbo: "Turbo",
    brightness: "Luminosité", timeout: "Extinction de l'écran", sleep_brightness: "Luminosité en veille",
    language: "Langue", status_leds: "LED d'état", night_mode: "Mode nuit LED", wallpaper: "Fond d'écran",
    upload_image: "Téléverser une image", on: "Oui", off: "Non", never: "Jamais",
    restart: "Redémarrer", shutdown: "Éteindre", confirm: "Confirmer", cancel: "Annuler",
    confirm_restart: "Redémarrer le NAS maintenant ?", confirm_shutdown: "Éteindre le NAS maintenant ?",
    password_prompt: "Mot de passe requis :", lan_only: "LAN uniquement — ne pas exposer à Internet.",
    daemon_offline: "Le démon des ventilateurs n'est pas actif",
  },
  pt: {
    title: "iDX6011 Pro", sec_overview: "Visão geral", sec_fan: "Ventoinhas", sec_storage: "Armazenamento",
    sec_network: "Rede", sec_services: "Serviços", sec_settings: "Definições", sec_power: "Energia",
    online: "Online", offline: "Painel offline", system: "Sistema", applied: "Aplicado",
    fan: "Ventoinha", curve: "Curva", silent: "Silencioso", default: "Padrão", turbo: "Turbo",
    brightness: "Brilho", timeout: "Desligar ecrã", sleep_brightness: "Brilho em repouso",
    language: "Idioma", status_leds: "LEDs de estado", night_mode: "Modo noturno LED", wallpaper: "Fundo",
    upload_image: "Carregar imagem", on: "Sim", off: "Não", never: "Nunca",
    restart: "Reiniciar", shutdown: "Desligar", confirm: "Confirmar", cancel: "Cancelar",
    confirm_restart: "Reiniciar o NAS agora?", confirm_shutdown: "Desligar o NAS agora?",
    password_prompt: "Senha necessária:", lan_only: "Apenas LAN — não exponha à Internet.",
    daemon_offline: "O daemon das ventoinhas não está em execução",
  },
  id: {
    title: "iDX6011 Pro", sec_overview: "Ringkasan", sec_fan: "Kontrol Kipas", sec_storage: "Penyimpanan",
    sec_network: "Jaringan", sec_services: "Layanan", sec_settings: "Pengaturan", sec_power: "Daya",
    online: "Online", offline: "Panel offline", system: "Sistem", applied: "Diterapkan",
    fan: "Kipas", curve: "Kurva", silent: "Senyap", default: "Standar", turbo: "Turbo",
    brightness: "Kecerahan", timeout: "Waktu mati layar", sleep_brightness: "Kecerahan tidur",
    language: "Bahasa", status_leds: "LED status", night_mode: "Mode malam LED", wallpaper: "Wallpaper",
    upload_image: "Unggah gambar", on: "Nyala", off: "Mati", never: "Tidak pernah",
    restart: "Mulai ulang", shutdown: "Matikan", confirm: "Konfirmasi", cancel: "Batal",
    confirm_restart: "Mulai ulang NAS sekarang?", confirm_shutdown: "Matikan NAS sekarang?",
    password_prompt: "Kata sandi diperlukan:", lan_only: "Hanya LAN — jangan ekspos ke internet.",
    daemon_offline: "Daemon kipas tidak berjalan",
  },
};

export function pickLang() {
  /* English is the project default; do NOT auto-pick the browser language
   * (the panel follows its device setting, and the non-Pro fan dashboard has no
   * device setting, so without this it would silently come up in the browser
   * locale instead of English). A ?lang= query or the user's saved choice win. */
  const q = new URLSearchParams(location.search).get("lang");
  const stored = localStorage.getItem("ugpaneld_lang");
  const cand = q || stored || "en";
  return STRINGS[cand] ? cand : "en";
}
