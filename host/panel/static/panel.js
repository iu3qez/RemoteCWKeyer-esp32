// Station panel: draws the state that /events sends, and nothing else.
//
// Every text that came from the daemon - callsigns, addresses, event lines -
// goes in with textContent and is never parsed as HTML (R14). The page
// commands nothing: it only listens.
//
// It decides nothing either (#90): state.py decides, where the tests run,
// and the page copies the values or looks up their text in the tables
// below. It computes four things only: "-" or "?" for an absent value,
// event times in this browser's time zone, "waiting" before the first
// message and "unreachable" when /events goes quiet.
"use strict";

// One table per kind of id the model sends, each an object literal with
// bare keys: test_web reads the keys and requires one for every id that
// state.py exports.

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

// A banner's look belongs to its id: the ones that say "do not trust" are
// danger, the configuration warnings are warning.
const BANNER_CLASS = {
  unreachable: "danger",
  stopped: "danger",
  silent: "danger",
  input_closed: "danger",
  waiting: "danger",
  not_guaranteed: "danger",
  mixed_edges: "warning",
  vocabulary: "warning",
};

const LIVENESS_TEXT = {
  waiting: "in attesa",
  alive: "vivo",
  silent: "silenzioso",
  stopped: "fermo",
  input_closed: "ingresso chiuso",
};

// Stale: key and PTT are the last known values, not the current ones.
const STALE_TEXT = {true: " - stato da verificare", false: ""};

// A held key reads "client", the holder's index and its name when known.
const KEY_TEXT = {unknown: "?", free: "libera", held: "client"};

const PTT_TEXT = {unknown: "?", on: "acceso", off: "spento"};

const READY_TEXT = {true: "fatto", false: "in attesa"};

const FITNESS_TEXT = {unknown: "-", fit: "idoneo", unfit: "non idoneo"};

const EDGES_TEXT = {file: "su file", stderr: "su stderr"};

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
  // In the order the model sends them; before its first message there is
  // no model yet, only "waiting". The watchdog's "unreachable" goes first.
  const ids = (unreachable ? ["unreachable"] : []).concat(s ? s.banners : ["waiting"]);
  const box = $("banners");
  clear(box);
  for (const id of ids) {
    box.appendChild(el("p", "banner " + BANNER_CLASS[id], BANNER_TEXT[id]));
  }
}

function renderKey(s) {
  const view = s.key_view;
  const key = $("key");
  // The model sends null for a part that is not there: no index while the
  // key is free or unknown, no name when the holder has none.
  key.textContent = [KEY_TEXT[view.state], view.idx, view.name].filter(function (part) {
    return part !== null;
  }).join(" ");
  const ptt = $("ptt");
  ptt.textContent = PTT_TEXT[s.ptt_view];
  // The PTT state is the class: panel.css lights #ptt.on.
  ptt.className = "big " + s.ptt_view;
  for (const node of [key, ptt]) {
    node.classList.toggle("stale", s.stale);
  }
}

function renderClients(s) {
  const body = $("clients").querySelector("tbody");
  clear(body);
  for (const c of s.clients) {
    // The fitness is the row's class: panel.css marks tr.unfit.
    const tr = el("tr", c.fitness);
    tr.appendChild(el("td", "num", c.idx));
    const name = el("td", "call", c.name === "" ? "-" : c.name);
    if (c.name_truncated) {
      name.appendChild(el("span", "muted", " (troncato)"));
    }
    tr.appendChild(name);
    tr.appendChild(el("td", "addr", c.addr));
    tr.appendChild(el("td", "", READY_TEXT[c.ready]));
    tr.appendChild(el("td", "num", ms(c.lat_ms)));
    tr.appendChild(el("td", "num", ms(c.peak_ms)));
    tr.appendChild(el("td", "", FITNESS_TEXT[c.fitness]));
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
    parts.push("uscita " + s.output.backend + ", fronti " + EDGES_TEXT[s.output.edges]);
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
    // An event read back from the file at panel start has no time of its
    // own: say so instead of formatting one.
    const t = e.from_file ? "dal file" : new Date(e.t * 1000).toLocaleTimeString("it-IT");
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
  $("liveness").textContent = "daemon " + lv + STALE_TEXT[last.stale];
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
  // EventSource reconnects by itself after a dropped connection, but the
  // HTML spec makes it "fail the connection" instead on a 503 (16-stream
  // cap full) or 421: readyState goes CLOSED and it never retries on its
  // own. onerror below replaces a CLOSED source so a freed slot is picked
  // up again, on the same 2 s the server's "retry:" already asks for. The
  // watchdog, not onerror, still decides when what is on screen can no
  // longer be trusted.
  const source = new EventSource("/events");
  source.onmessage = function (ev) {
    arm();
    last = JSON.parse(ev.data);
    render();
  };
  source.addEventListener("keepalive", arm);
  source.onerror = function () {
    // Only CLOSED means the browser gave up for good; any other state
    // (CONNECTING) is a normal retry in progress, and starting a second
    // source here would leave two live connections open.
    if (source.readyState === EventSource.CLOSED) {
      source.close();
      setTimeout(start, 2000);
    }
  };
}

document.addEventListener("DOMContentLoaded", function () {
  // Armed once here, and after that only by what arrives on /events: a
  // retry is no sign of life, and arming on each one kept the watchdog
  // from ever firing while the panel refused the page.
  arm();
  render();
  start();
});
