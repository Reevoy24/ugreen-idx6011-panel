import { STRINGS, pickLang } from "./i18n.js";

const POLL_MS = 2500;
const MODE_GRACE_MS = 6000;          // optimistic-highlight grace, mirrors the panel
const RPM_MAX = { cpu: 2400, sys: 2200 };
const TIMEOUTS = [60, 300, 900, 1800, 0];
const LANG_NAMES = { en: "English", de: "Deutsch", es: "Español", fr: "Français", pt: "Português", id: "Indonesia" };
const TIMEZONES = [
  "UTC",
  "Europe/London", "Europe/Berlin", "Europe/Paris", "Europe/Madrid", "Europe/Rome",
  "Europe/Amsterdam", "Europe/Zurich", "Europe/Lisbon", "Europe/Warsaw", "Europe/Athens",
  "Europe/Moscow", "Europe/Istanbul",
  "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
  "America/Sao_Paulo", "America/Mexico_City", "America/Toronto",
  "Asia/Dubai", "Asia/Kolkata", "Asia/Jakarta", "Asia/Singapore", "Asia/Shanghai",
  "Asia/Tokyo", "Asia/Seoul",
  "Australia/Sydney", "Pacific/Auckland"
];

let lang = pickLang();
let last = null;
let optimisticMode = null, lastModeClick = 0;
let authHeader = sessionStorage.getItem("ugpaneld_auth") || "";
let toastTimer = null;
let pendingPower = null;
let pveOpen = false; // remember the Proxmox expand state across polls
let editing = false, editDomain = "cpu", editMode = "default", editPoints = [];

const t = (k) => (STRINGS[lang] || STRINGS.en)[k] ?? STRINGS.en[k] ?? k;
const $ = (id) => document.getElementById(id);
const setText = (id, v) => { const e = $(id); if (e) e.textContent = v; };
const toggle = (id, show) => { const e = $(id); if (e) e.hidden = !show; };
const num = (v) => (v == null ? null : v);
const esc = (s) => String(s).replace(/[<>&]/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;" }[c]));

function applyI18n() {
  document.documentElement.lang = lang;
  document.querySelectorAll("[data-i18n]").forEach((e) => { e.textContent = t(e.dataset.i18n); });
}

/* ---------- fetch ---------- */
async function authed(url, opts) {
  opts = opts || {};
  const send = (auth) => fetch(url, { ...opts, headers: { ...(opts.headers || {}), ...(auth ? { Authorization: auth } : {}) } });
  let r = await send(authHeader);
  if (r.status === 401) {
    const pw = prompt(t("password_prompt"));
    if (pw == null) return null;
    authHeader = "Basic " + btoa(":" + pw);
    sessionStorage.setItem("ugpaneld_auth", authHeader);
    r = await send(authHeader);
  }
  let data = {};
  try { data = await r.json(); } catch { /* ignore */ }
  if (!r.ok) throw new Error(data.error || "error " + r.status);
  return data;
}
const postJSON = (url, obj) =>
  authed(url, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(obj) });
const postSettings = (obj) => postJSON("/api/settings", obj).catch((e) => toast(e.message));

function toast(msg, ok) {
  const el = $("toast");
  el.textContent = msg;
  el.className = "toast" + (ok ? " ok" : "");
  el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (el.hidden = true), 3500);
}

/* ---------- rendering ---------- */
function rate(bps) { // bps = bits/sec
  if (bps == null) return "--";
  const Bps = bps / 8;
  if (Bps >= 1048576) return (Bps / 1048576).toFixed(1) + " MB/s";
  return (Bps / 1024).toFixed(1) + " KB/s";
}
function uptimeStr(sec) {
  if (sec == null) return "--";
  const d = Math.floor(sec / 86400), h = Math.floor((sec % 86400) / 3600), m = Math.floor((sec % 3600) / 60);
  return `${d}d ${h}h ${m}m`;
}
function metric(k, v) { return `<div class="m"><div class="k">${k}</div><div class="v">${v}</div></div>`; }
function setBar(id, rpm, dom) {
  setText(id, rpm == null ? "--" : rpm);
  const b = $(id + "-bar");
  if (b) b.style.width = (rpm == null ? 0 : Math.max(0, Math.min(100, (rpm / RPM_MAX[dom]) * 100))) + "%";
}

function setOnline(on) {
  document.body.classList.toggle("offline", !on);
  const pill = $("pill");
  if (pill) pill.dataset.state = on ? "ok" : "bad";
  setText("pill-text", on ? t("online") : t("offline"));
}

function render(s) {
  last = s;
  if (s.valid === false) return; // daemon up but no snapshot yet

  /* dashboard follows the device's language setting */
  const stx = s.settings || {};
  if (stx.language && STRINGS[stx.language] && stx.language !== lang) {
    lang = stx.language;
    localStorage.setItem("ugpaneld_lang", lang);
    applyI18n();
  }

  /* overview */
  const sys = s.system || {}, gpu = s.gpu || {};
  let ov = "";
  ov += metric(t("cpu"), `${Math.round(sys.cpu_usage ?? 0)}<small>%</small>`);
  ov += metric(t("temp"), sys.temp_c != null ? `${Math.round(sys.temp_c)}<small>°C</small>` : "--");
  if (sys.ram_total_mb)
    ov += metric(t("ram"), `${Math.round(sys.ram_usage ?? 0)}<small>% · ${(sys.ram_used_mb / 1024).toFixed(1)}/${Math.round(sys.ram_total_mb / 1024)} GB</small>`);
  if (sys.disk_total_gb)
    ov += metric(t("storage"), `${Math.round(sys.disk_usage ?? 0)}<small>% · ${Math.round(sys.disk_used_gb)}/${Math.round(sys.disk_total_gb)} GB</small>`);
  if (gpu.available) ov += metric(t("gpu"), `${Math.round(gpu.usage ?? 0)}<small>%</small>`);
  ov += metric(t("uptime"), `<span style="font-size:16px">${uptimeStr(s.uptime_seconds)}</span>`);
  $("overview").innerHTML = ov;

  /* fan */
  const fan = s.fan || {}, rpm = fan.rpm || {};
  setText("fan-cpu-temp", num(fan.cpu_temp) ?? "--");
  setText("fan-sys-temp", num(fan.sys_temp) ?? "--");
  setBar("cpufan1", num(rpm.cpufan1), "cpu");
  setBar("cpufan2", num(rpm.cpufan2), "cpu");
  setBar("sysfan1", num(rpm.sysfan1), "sys");
  setBar("sysfan2", num(rpm.sysfan2), "sys");
  setText("cpu-pct", num(fan.cpu_pct) ?? "--");
  setText("sys-pct", num(fan.sys_pct) ?? "--");
  let activeMode = fan.mode;
  if (optimisticMode && Date.now() - lastModeClick < MODE_GRACE_MS) activeMode = optimisticMode;
  document.querySelectorAll("#modes .seg").forEach((b) => b.classList.toggle("on", b.dataset.mode === activeMode));
  if (!editing) drawPlot(fan);

  /* storage */
  const dk = s.disks || { items: [] };
  $("disks").innerHTML = (dk.items || []).map((x) =>
    `<div class="row"><span class="l"><span class="gdot${x.online ? "" : " off"}"></span>${x.dev} · ${(x.size_tb ?? 0).toFixed(1)} TB</span><span>${x.temp_c == null ? "--" : Math.round(x.temp_c) + " °C"}</span></div>`
  ).join("") || `<div class="row"><span class="l">—</span></div>`;

  /* network */
  const net = s.net || { ifaces: [] };
  $("net").innerHTML = (net.ifaces || []).map((f) =>
    `<div class="row"><span class="l"><span class="gdot${f.link_up ? "" : " off"}"></span>${f.name} · ${f.ipv4 || "—"}</span><span>↓ ${rate(f.rx_bps)} · ↑ ${rate(f.tx_bps)}</span></div>`
  ).join("") || `<div class="row"><span class="l">—</span></div>`;

  /* services */
  const caps = s.caps || {};
  const showSvc = caps.has_pve || caps.has_opnsense;
  toggle("block-services", showSvc);
  if (showSvc) {
    let h = "";
    const pve = s.pve || {}, opn = s.opnsense || {};
    if (pve.available) {
      const guests = (pve.guests || []).map((g) =>
        `<div class="subrow"><span class="l"><span class="gdot${g.running ? "" : " off"}"></span>${esc(g.name)}</span>` +
        `<span class="sub-id">${g.is_lxc ? "LXC" : "VM"} ${g.vmid}</span></div>`
      ).join("");
      h += `<details class="exp" id="pve-exp"${pveOpen ? " open" : ""}>` +
        `<summary class="row"><span class="l"><span class="chev"></span>${t("proxmox")}</span>` +
        `<span>${pve.vm_running}/${pve.vm_total} ${t("vms")} · ${pve.lxc_running}/${pve.lxc_total} ${t("containers")}</span></summary>` +
        `<div class="sublist">${guests}</div></details>`;
    }
    if (opn.available) {
      h += `<div class="row"><span class="l">${t("opnsense")} · ${t("gateway")}</span><span>${opn.gw_rtt_ms < 0 ? "--" : opn.gw_rtt_ms + " ms"} · ${esc(opn.gw_status || "")}</span></div>`;
      h += `<div class="row"><span class="l">${t("dns_blocked")} · ${t("leases")}</span><span>${opn.dns_blocked_pct}% · ${opn.dhcp_leases}</span></div>`;
    }
    $("services").innerHTML = h;
    const ex = $("pve-exp");
    if (ex) ex.addEventListener("toggle", () => { pveOpen = ex.open; });
  }

  /* settings (skip controls the user is interacting with) */
  syncRange("set-brightness", stx.brightness);
  syncSelect("set-timeout", stx.backlight_timeout);
  syncRange("set-sleep", stx.sleep_brightness);
  syncSelect("set-language", stx.language);
  syncSelect("lang", stx.language);
  syncCheck("set-leds", stx.leds_on);
  syncCheck("set-night", stx.led_night);
  toggle("row-leds", caps.has_leds);
  toggle("row-night", caps.has_leds);
  if (!caps.has_leds) { const ne = $("night-edit"); if (ne) ne.hidden = true; }
  setText("night-window", stx.led_night_window ? "(" + stx.led_night_window + ")" : "");
  syncVal("night-start", stx.led_night_start);
  syncVal("night-end", stx.led_night_end);
  syncSelect("set-format", stx.clock_24h === false ? "12" : "24");
  syncTzSelect(stx.timezone);

  renderWallpapers(s.wallpapers || { options: [], current: "" });
}

function syncRange(id, v) {
  if (v == null) return;
  const el = $(id);
  if (!el || document.activeElement === el) return;
  el.value = v;
  setText(id + "-v", v + "%");
}
function syncSelect(id, v) {
  if (v == null) return;
  const el = $(id);
  if (!el || document.activeElement === el) return;
  el.value = String(v);
}
function syncCheck(id, v) {
  if (v == null) return;
  const el = $(id);
  if (!el || document.activeElement === el) return;
  el.checked = !!v;
}
function syncVal(id, v) {
  if (v == null) return;
  const el = $(id);
  if (!el || document.activeElement === el) return;
  el.value = v;
}
function syncTzSelect(v) {
  const el = $("set-tz");
  if (!el || document.activeElement === el) return;
  v = v || "";
  if (v && ![...el.options].some((o) => o.value === v)) {
    const o = document.createElement("option");
    o.value = v; o.textContent = v;
    el.insertBefore(o, el.firstChild);
  }
  el.value = v;
}
function renderWallpapers(wp) {
  const box = $("wp-options");
  if (!box) return;
  box.innerHTML = (wp.options || []).map((name) => {
    const bg = name === "none" ? "" : ` style="background-image:url('/wp/${encodeURIComponent(name)}')"`;
    const cls = "wp" + (name === wp.current ? " on" : "") + (name === "none" ? " none" : "");
    const del = name === "custom" ? `<button class="wpdel" aria-label="delete">×</button>` : "";
    return `<div class="${cls}" data-wp="${esc(name)}"${bg}><span class="wpname">${esc(name)}</span>${del}</div>`;
  }).join("");
}

/* ---------- curve plot (matches the device geometry) ---------- */
function drawPlot(fan) {
  const svg = $("plot");
  if (!svg) return;
  svg.replaceChildren();
  const L = 36, R = 16, T = 12, B = 26, W = 600, H = 180, tmin = 20, tmax = 90;
  const NS = "http://www.w3.org/2000/svg";
  const px = (x) => L + (Math.max(tmin, Math.min(tmax, x)) - tmin) / (tmax - tmin) * (W - L - R);
  const py = (p) => T + (1 - p / 100) * (H - T - B);
  const el = (n, a) => { const e = document.createElementNS(NS, n); for (const k in a) e.setAttribute(k, a[k]); return e; };
  for (const p of [0, 50, 100]) {
    const y = py(p);
    svg.appendChild(el("line", { x1: L, y1: y, x2: W - R, y2: y, stroke: "#2c2c2e" }));
    const x = el("text", { x: L - 6, y: y + 3, fill: "#8e8e93", "font-size": 11, "text-anchor": "end" }); x.textContent = p; svg.appendChild(x);
  }
  for (const tp of [20, 40, 60, 80]) {
    const x = px(tp);
    svg.appendChild(el("line", { x1: x, y1: T, x2: x, y2: H - B, stroke: "#2c2c2e" }));
    const tx = el("text", { x, y: H - B + 15, fill: "#8e8e93", "font-size": 11, "text-anchor": "middle" }); tx.textContent = tp + "°"; svg.appendChild(tx);
  }
  const poly = (pts, c) => {
    if (!pts || !pts.length) return;
    svg.appendChild(el("polyline", { points: pts.map((p) => `${px(p.t)},${py(p.p)}`).join(" "), fill: "none", stroke: c, "stroke-width": 2.5, "stroke-linejoin": "round", "stroke-linecap": "round", "vector-effect": "non-scaling-stroke" }));
  };
  const at = (pts, x) => {
    if (!pts || !pts.length || x == null) return null;
    if (x <= pts[0].t) return pts[0].p;
    const lastp = pts[pts.length - 1];
    if (x >= lastp.t) return lastp.p;
    for (let i = 1; i < pts.length; i++) if (x <= pts[i].t) { const a = pts[i - 1], b = pts[i]; return b.t === a.t ? b.p : a.p + (b.p - a.p) * (x - a.t) / (b.t - a.t); }
    return lastp.p;
  };
  const mark = (pts, x, c) => { const p = at(pts, x); if (p == null) return; svg.appendChild(el("circle", { cx: px(x), cy: py(p), r: 4, fill: c, stroke: "#0a0a0c", "stroke-width": 1.5 })); };
  poly(fan.sys_curve, "#34c759");
  poly(fan.cpu_curve, "#35d1e2");
  mark(fan.sys_curve, fan.sys_temp, "#34c759");
  mark(fan.cpu_curve, fan.cpu_temp, "#35d1e2");
}

/* ---------- wiring ---------- */
function buildSelects() {
  const ts = $("set-timeout");
  ts.innerHTML = TIMEOUTS.map((s) => {
    const label = s === 0 ? t("never") : (s % 60 === 0 ? (s / 60) + " min" : s + " s");
    return `<option value="${s}">${label}</option>`;
  }).join("");
  const langOpts = Object.keys(STRINGS).map((c) => `<option value="${c}">${LANG_NAMES[c] || c}</option>`).join("");
  const sl = $("set-language"); if (sl) sl.innerHTML = langOpts;
  const tl = $("lang"); if (tl) tl.innerHTML = langOpts;
  const tz = $("set-tz");
  if (tz) tz.innerHTML = TIMEZONES.map((z) => `<option value="${z}">${z}</option>`).join("");
}

function wireControls() {
  $("set-brightness").addEventListener("input", (e) => setText("set-brightness-v", e.target.value + "%"));
  $("set-brightness").addEventListener("change", (e) => postSettings({ brightness: parseInt(e.target.value, 10) }));
  $("set-sleep").addEventListener("input", (e) => setText("set-sleep-v", e.target.value + "%"));
  $("set-sleep").addEventListener("change", (e) => postSettings({ sleep_brightness: parseInt(e.target.value, 10) }));
  $("set-timeout").addEventListener("change", (e) => postSettings({ backlight_timeout: parseInt(e.target.value, 10) }));
  const changeLanguage = (v) => {
    if (STRINGS[v]) { lang = v; localStorage.setItem("ugpaneld_lang", lang); applyI18n(); }
    const a = $("lang"), b = $("set-language");
    if (a) a.value = v;
    if (b) b.value = v;
    postSettings({ language: v });
  };
  const sl2 = $("set-language"); if (sl2) sl2.addEventListener("change", (e) => changeLanguage(e.target.value));
  const tl2 = $("lang"); if (tl2) tl2.addEventListener("change", (e) => changeLanguage(e.target.value));
  $("set-leds").addEventListener("change", (e) => postSettings({ leds_on: e.target.checked ? 1 : 0 }));
  $("set-night").addEventListener("change", (e) => postSettings({ led_night: e.target.checked ? 1 : 0 }));

  const nx = $("night-x");
  if (nx) nx.addEventListener("click", () => {
    const e = $("night-edit");
    if (e) { e.hidden = !e.hidden; nx.classList.toggle("open", !e.hidden); }
  });
  // clock format + timezone apply immediately on change (like the language
  // dropdown); the poll then confirms the server value instead of reverting it.
  const sf = $("set-format");
  if (sf) sf.addEventListener("change", (e) => postSettings({ clock_24h: e.target.value === "24" ? 1 : 0 }));
  const stz = $("set-tz");
  if (stz) stz.addEventListener("change", (e) => postSettings({ timezone: e.target.value.trim() }));

  // the Save button only commits the start/end times (a paired range).
  const nsv = $("night-save");
  if (nsv) nsv.addEventListener("click", async () => {
    try {
      await postJSON("/api/settings", {
        led_night_start: $("night-start").value,
        led_night_end: $("night-end").value,
      });
      toast(t("saved"), true);
    } catch (err) { toast(err.message); }
  });

  $("wp-options").addEventListener("click", async (e) => {
    if (e.target.classList.contains("wpdel")) {
      e.stopPropagation();
      try { await authed("/api/wallpaper", { method: "DELETE" }); toast(t("deleted"), true); }
      catch (err) { toast(err.message); }
      return;
    }
    const el = e.target.closest("[data-wp]");
    if (el) postSettings({ wallpaper: el.dataset.wp });
  });
  $("wp-upload").addEventListener("change", async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    try {
      const buf = await file.arrayBuffer();
      const r = await authed("/api/wallpaper", { method: "POST", headers: { "Content-Type": "image/png" }, body: buf });
      if (r) toast(t("upload_image") + " ✓", true);
    } catch (err) { toast(err.message); }
    e.target.value = "";
  });

  document.querySelectorAll("#modes .seg").forEach((b) => {
    b.addEventListener("click", async () => {
      const mode = b.dataset.mode;
      optimisticMode = mode; lastModeClick = Date.now();
      document.querySelectorAll(".seg").forEach((x) => x.classList.toggle("on", x === b));
      try { await postJSON("/api/fan/mode", { mode }); } catch (err) { toast(err.message); }
    });
  });

  $("btn-restart").addEventListener("click", () => openConfirm("reboot"));
  $("btn-shutdown").addEventListener("click", () => openConfirm("poweroff"));
  $("confirm-cancel").addEventListener("click", () => { $("confirm").hidden = true; pendingPower = null; });
  $("confirm-ok").addEventListener("click", async () => {
    $("confirm").hidden = true;
    if (!pendingPower) return;
    try { await postJSON("/api/power", { action: pendingPower, confirm: true }); toast("OK", true); }
    catch (err) { toast(err.message); }
    pendingPower = null;
  });
}
function openConfirm(action) {
  pendingPower = action;
  setText("confirm-text", t(action === "poweroff" ? "confirm_shutdown" : "confirm_restart"));
  $("confirm").hidden = false;
}

/* ---------- curve editor (edits the active mode's CPU or System curve) ---------- */
function setEditDomainButtons() {
  $("ed-cpu").classList.toggle("on", editDomain === "cpu");
  $("ed-sys").classList.toggle("on", editDomain === "sys");
}
function renderEditPoints() {
  $("ed-points").innerHTML = editPoints.map((pt, i) =>
    `<div class="ptrow" data-i="${i}">` +
    `<input type="number" class="pt-t" min="0" max="120" value="${pt.t}"><span class="u">°C</span>` +
    `<input type="number" class="pt-p" min="0" max="100" value="${pt.p}"><span class="u">%</span>` +
    `<button class="rm" aria-label="remove">×</button></div>`
  ).join("");
}
function drawEditPreview() {
  if (!last || !last.fan) return;
  drawPlot({
    cpu_curve: editDomain === "cpu" ? editPoints : (last.fan.cpu_curve || []),
    sys_curve: editDomain === "sys" ? editPoints : (last.fan.sys_curve || []),
    cpu_temp: last.fan.cpu_temp, sys_temp: last.fan.sys_temp,
  });
}
function loadEditPoints() {
  const c = last && last.fan ? (editDomain === "cpu" ? last.fan.cpu_curve : last.fan.sys_curve) : null;
  editPoints = (c && c.length ? c : [{ t: 0, p: 20 }, { t: 80, p: 100 }]).map((p) => ({ t: p.t, p: p.p }));
  renderEditPoints();
  drawEditPreview();
}
function enterEdit() {
  if (!last || !last.fan) return;
  editing = true;
  editMode = last.fan.mode || "default";
  editDomain = "cpu";
  setEditDomainButtons();
  loadEditPoints();
  $("curve-editor").hidden = false;
  $("curve-edit").hidden = true;
}
function exitEdit() {
  editing = false;
  $("curve-editor").hidden = true;
  $("curve-edit").hidden = false;
  if (last && last.fan) drawPlot(last.fan);
}
function wireEditor() {
  $("curve-edit").addEventListener("click", enterEdit);
  $("curve-cancel").addEventListener("click", exitEdit);
  $("ed-cpu").addEventListener("click", () => { editDomain = "cpu"; setEditDomainButtons(); loadEditPoints(); });
  $("ed-sys").addEventListener("click", () => { editDomain = "sys"; setEditDomainButtons(); loadEditPoints(); });
  $("ed-add").addEventListener("click", () => {
    if (editPoints.length >= 12) { toast(t("curve_invalid")); return; }
    const lp = editPoints[editPoints.length - 1] || { t: 40, p: 50 };
    editPoints.push({ t: Math.min(120, lp.t + 5), p: lp.p });
    renderEditPoints();
    drawEditPreview();
  });
  $("ed-points").addEventListener("input", (e) => {
    const row = e.target.closest(".ptrow");
    if (!row) return;
    const i = +row.dataset.i;
    if (e.target.classList.contains("pt-t")) editPoints[i].t = parseInt(e.target.value || "0", 10);
    else if (e.target.classList.contains("pt-p")) editPoints[i].p = parseInt(e.target.value || "0", 10);
    drawEditPreview();
  });
  $("ed-points").addEventListener("click", (e) => {
    if (!e.target.classList.contains("rm")) return;
    if (editPoints.length <= 1) return;
    editPoints.splice(+e.target.closest(".ptrow").dataset.i, 1);
    renderEditPoints();
    drawEditPreview();
  });
  $("curve-save").addEventListener("click", async () => {
    const pts = editPoints.map((p) => ({ t: p.t | 0, p: p.p | 0 }));
    pts.sort((a, b) => a.t - b.t);
    const bad = pts.length < 1 || pts.length > 12 ||
      pts.some((p) => p.t < 0 || p.t > 120 || p.p < 0 || p.p > 100) ||
      pts.some((p, i) => i > 0 && p.t === pts[i - 1].t);
    if (bad) { toast(t("curve_invalid")); return; }
    try {
      await postJSON("/api/fan/mode", { mode: editMode, domain: editDomain, points: pts });
      toast(t("saved"), true);
      exitEdit();
    } catch (err) { toast(err.message); }
  });
}

/* ---------- poll ---------- */
async function tick() {
  try {
    const r = await fetch("/api/stats", { cache: "no-store" });
    if (!r.ok) throw new Error();
    const s = await r.json();
    setOnline(true);
    render(s);
  } catch {
    setOnline(false);
  }
}
function startPolling() {
  tick();
  setInterval(() => { if (!document.hidden) tick(); }, POLL_MS);
  document.addEventListener("visibilitychange", () => { if (!document.hidden) tick(); });
}

buildSelects();
applyI18n();
wireControls();
wireEditor();
startPolling();
