// Station panel: draws the state that /events sends, and nothing else.
//
// Every text that came from the daemon - callsigns, addresses, event lines -
// goes in with textContent and is never parsed as HTML (R14). The page
// commands nothing: it only listens.
"use strict";

// KTD9: the banners that say "do not trust" first, in this order; then the
// configuration warnings. They stack and none hides another.
const BANNER_ORDER = [
  "unreachable", "stopped", "silent", "input_closed", "waiting", "not_guaranteed",
  "mixed_edges", "vocabulary",
];
const WARNINGS = new Set(["mixed_edges", "vocabulary"]);

const BANNER_TEXT = {
  unreachable: "Pannello irraggiungibile: quello che vedi e' fermo all'ultimo aggiornamento.",
  stopped: "Daemon fermo: chiave e PTT sono gli ultimi noti.",
  silent: "Daemon silenzioso da oltre tre periodi: chiave e PTT sono gli ultimi noti.",
  input_closed: "Ingresso chiuso: il pannello non riceve piu' righe dal daemon.",
  waiting: "In attesa della prima riga dal vivo: lo stato viene dal file, non da adesso.",
  not_guaranteed: "Stato non garantito: si sono perse righe, e fino alla prossima istantanea "
    + "chiave, PTT e client possono essere sbagliati.",
  mixed_edges: "I fronti arrivano mescolati alle righe di stato (2>&1 verso una pipe o il "
    + "journal): un pannello lento puo' fermare la stazione.",
  vocabulary: "Il daemon parla un vocabolario diverso da quello del pannello: si mostra solo "
    + "quello che il pannello riconosce.",
};

const LIVENESS_TEXT = {
  waiting: "in attesa",
  alive: "vivo",
  silent: "silenzioso",
  stopped: "fermo",
  input_closed: "ingresso chiuso",
};

const EVENT_TEXT = {
  fault: "fault", late_bytes: "byte in ritardo", over: "over", unfit: "link non idoneo",
  refused: "rifiutato", disconnected: "disconnesso", lost_events: "eventi persi",
  dropped_lines: "righe perse", dropped_total: "righe perse in totale",
  missing_snapshots: "istantanee mancanti", discarded_snapshot: "istantanea scartata",
  unreadable: "illeggibile", unknown: "sconosciuta", stopped: "arresto",
  new_instance: "nuovo daemon", edges_stall: "fronti", edges_refused: "fronti",
  edges_summary: "fronti", reader_stalled: "lettore fermo", slot_in_use: "slot",
  accept_failed: "accept", poll_failed: "poll",
};

const SETTINGS = [
  ["max_clients", "Client massimi", ""],
  ["play_floor_ms", "Buffer minimo B", " ms"],
  ["link_ceiling_ms", "Tetto del link", " ms"],
  ["ptt_tail_ms", "Coda del PTT", " ms"],
  ["ptt_lead_ms", "Anticipo del PTT", " ms"],
  ["idle_ms", "Inattivita'", " ms"],
  ["over_max_ms", "Over massimo", " ms"],
  ["handshake_ms", "Tempo per il CONNECT", " ms"],
  ["out_cap_bytes", "Byte non inviati per client", " byte"],
];

// Two keepalives without anything: the panel is gone (the server sends one
// every 5 s when nothing changes).
const KEEPALIVE_MS = 5000;
const WATCHDOG_MS = 2 * KEEPALIVE_MS + 500;

let last = null;
let unreachable = false;
let watchdog = null;

function $(id) {
  return document.getElementById(id);
}

function el(tag, className, text) {
  const e = document.createElement(tag);
  if (className) {
    e.className = className;
  }
  if (text !== undefined && text !== null) {
    e.textContent = String(text);
  }
  return e;
}

function clear(node) {
  while (node.firstChild) {
    node.removeChild(node.firstChild);
  }
}

function ms(v) {
  return v === null || v === undefined ? "-" : v + " ms";
}

function renderBanners(s) {
  const ids = new Set(s ? s.banners : ["waiting"]);
  if (unreachable) {
    ids.add("unreachable");
  }
  const box = $("banners");
  clear(box);
  for (const id of BANNER_ORDER) {
    if (ids.has(id)) {
      box.appendChild(el("p", "banner " + (WARNINGS.has(id) ? "warning" : "danger"),
                         BANNER_TEXT[id]));
    }
  }
}

function renderKey(s) {
  const key = $("key");
  if (!s.key_known) {
    key.textContent = "?";
  } else if (s.key_holder === null) {
    key.textContent = "libera";
  } else {
    const name = s.key_holder_name === null ? "" : " " + s.key_holder_name;
    key.textContent = "client " + s.key_holder + name;
  }
  const ptt = $("ptt");
  ptt.textContent = s.ptt === null ? "?" : (s.ptt ? "acceso" : "spento");
  ptt.classList.toggle("on", s.ptt === true);
  // Certain only when the daemon is alive and the state guaranteed; any
  // other time these are the last known values, and look it.
  for (const node of [key, ptt]) {
    node.classList.toggle("stale", !s.certain);
  }
}

function renderClients(s) {
  const body = $("clients").querySelector("tbody");
  clear(body);
  for (const c of s.clients) {
    const tr = el("tr", c.unfit ? "unfit" : "");
    tr.appendChild(el("td", "num", c.idx));
    const name = el("td", "call", c.name === "" ? "-" : c.name);
    if (c.name_truncated) {
      name.appendChild(el("span", "muted", " (troncato)"));
    }
    tr.appendChild(name);
    tr.appendChild(el("td", "addr", c.addr));
    tr.appendChild(el("td", "", c.ready ? "fatto" : "in attesa"));
    tr.appendChild(el("td", "num", ms(c.lat_ms)));
    tr.appendChild(el("td", "num", ms(c.peak_ms)));
    tr.appendChild(el("td", "", c.unfit ? "non idoneo" : (c.peak_ms === null ? "-" : "idoneo")));
    body.appendChild(tr);
  }
  $("no-clients").hidden = s.clients.length > 0;
  $("clients").hidden = s.clients.length === 0;
}

function renderSettings(s) {
  const dl = $("settings");
  clear(dl);
  for (const [key, label, unit] of SETTINGS) {
    const v = s.settings[key];
    dl.appendChild(el("dt", "", label));
    dl.appendChild(el("dd", "", v === null ? "?" : v + unit));
  }
  const parts = [];
  if (s.listen) {
    parts.push("in ascolto su " + s.listen);
  }
  if (s.output.backend) {
    const edges = s.output.edges === "file" ? "su file" : "su stderr";
    parts.push("uscita " + s.output.backend + ", fronti " + edges);
  }
  parts.push("istantanea ogni " + s.period_ms + " ms");
  if (s.instance) {
    parts.push("istanza " + s.instance);
  }
  $("daemon").textContent = parts.join(" - ");
}

function renderEvents(s) {
  const list = $("events");
  clear(list);
  const events = s.events.slice().reverse();
  for (const e of events) {
    const li = el("li", "event " + e.kind);
    const t = new Date(e.t * 1000).toLocaleTimeString("it-IT");
    li.appendChild(el("time", "muted", t));
    li.appendChild(el("span", "kind", EVENT_TEXT[e.kind] || e.kind));
    li.appendChild(el("span", "text", e.text));
    list.appendChild(li);
  }
  $("no-events").hidden = events.length > 0;
  $("events-max").textContent = "(ultimi " + s.events_max + ")";
}

function render() {
  document.body.classList.toggle("unreachable", unreachable);
  renderBanners(last);
  if (!last) {
    return;
  }
  const lv = LIVENESS_TEXT[last.liveness] || last.liveness;
  $("liveness").textContent = "daemon " + lv + (last.certain ? "" : " - stato da verificare");
  renderKey(last);
  renderClients(last);
  renderSettings(last);
  renderEvents(last);
}

function arm() {
  clearTimeout(watchdog);
  if (unreachable) {
    unreachable = false;
    render();
  }
  watchdog = setTimeout(function () {
    unreachable = true;
    render();
  }, WATCHDOG_MS);
}

function start() {
  // EventSource reconnects by itself; on every connection the server sends
  // the whole state first. The watchdog, not onerror, decides when what is
  // on screen can no longer be trusted.
  const source = new EventSource("/events");
  source.onmessage = function (ev) {
    arm();
    last = JSON.parse(ev.data);
    render();
  };
  source.addEventListener("keepalive", arm);
  arm();
  render();
}

document.addEventListener("DOMContentLoaded", start);
