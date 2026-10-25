import { loadSg2gr } from "./sg2gr.mjs";

const $ = (id) => document.getElementById(id);
const els = {
  drop: $("drop"), file: $("file"), fileName: $("file-name"),
  advanced: $("advanced"), overrides: $("overrides"), ovStatus: $("ov-status"),
  add: $("add"), oTitle: $("o-title"), oIsbn: $("o-isbn"), oFormat: $("o-format"), oExclude: $("o-exclude"),
  result: $("result"), summary: $("summary"), notes: $("notes"), download: $("download"),
  preview: $("preview").tBodies[0], error: $("error"),
};

const OUTPUT_NAME = "goodreads_library_export.csv";
const STORE_KEY = "sg2gr.overrides";
const PREVIEW_COLS = ["Title", "Author", "Exclusive Shelf", "My Rating", "ISBN13", "Binding"];

let sg = null;          // loaded wasm wrapper
let csvText = null;     // the user's export, kept in memory only
let outputText = null;

const store = {
  get() { try { return localStorage.getItem(STORE_KEY) || ""; } catch { return ""; } },
  set(v) { try { localStorage.setItem(STORE_KEY, v); } catch { /* private mode etc. */ } },
};

function showError(msg) {
  els.error.textContent = msg || "";
  els.error.hidden = !msg;
}

// Minimal RFC 4180 parser, only used to preview our own output.
function parseCsv(text) {
  const rows = [];
  let row = [], field = "", quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"') { if (text[i + 1] === '"') { field += '"'; i++; } else quoted = false; }
      else field += c;
    } else if (c === '"') quoted = true;
    else if (c === ",") { row.push(field); field = ""; }
    else if (c === "\n") { row.push(field); rows.push(row); row = []; field = ""; }
    else if (c !== "\r") field += c;
  }
  if (field || row.length) { row.push(field); rows.push(row); }
  return rows;
}

const unwrapIsbn = (v) => v.replace(/^="?|"$/g, "");

function renderPreview(csv) {
  const [header, ...rows] = parseCsv(csv);
  const idx = PREVIEW_COLS.map((c) => header.indexOf(c));
  els.preview.replaceChildren(...rows.slice(0, 10).map((r) => {
    const tr = document.createElement("tr");
    idx.forEach((i, k) => {
      const td = document.createElement("td");
      td.textContent = PREVIEW_COLS[k] === "ISBN13" ? unwrapIsbn(r[i] || "") : r[i] || "";
      tr.append(td);
    });
    return tr;
  }));
}

function checkOverrides() {
  const msg = sg.validateOverrides(els.overrides.value);
  els.ovStatus.className = "status " + (msg ? "bad" : els.overrides.value.trim() ? "good" : "");
  els.ovStatus.textContent = msg ? `Invalid JSON: ${msg}` : els.overrides.value.trim() ? "Overrides look valid." : "";
  return !msg;
}

function run() {
  showError("");
  if (!sg || csvText === null) return;
  if (!checkOverrides()) {
    els.advanced.open = true;
    els.result.hidden = true;
    return;
  }
  const r = sg.convert(csvText, els.overrides.value);
  if (r.error) {
    els.result.hidden = true;
    showError(r.error);
    return;
  }
  outputText = r.text;
  const { rowsOut, excluded, blankStatus, blankTitles } = r.stats;
  els.summary.textContent = `Converted ${rowsOut} book${rowsOut === 1 ? "" : "s"}` +
    (excluded ? `, left out ${excluded}.` : ".");
  const notes = [];
  if (blankStatus) {
    const shown = blankTitles.slice(0, 10).map((t) => `“${t}”`).join(", ");
    const more = blankTitles.length > 10 ? ` and ${blankTitles.length - 10} more` : "";
    notes.push(`No read status in StoryGraph, so ${blankStatus === 1 ? "this book imports" : "these books import"} as "to-read": ${shown}${more}. Use the advanced overrides to leave ${blankStatus === 1 ? "it" : "them"} out.`);
  }
  els.notes.replaceChildren(...notes.map((n) => Object.assign(document.createElement("li"), { textContent: n })));
  renderPreview(r.text);
  els.result.hidden = false;
}

async function takeFile(file) {
  if (!file) return;
  els.fileName.textContent = file.name;
  try {
    csvText = await file.text();
  } catch {
    showError("Couldn't read that file.");
    return;
  }
  run();
}

// ---- events ---------------------------------------------------------------
els.file.addEventListener("change", () => takeFile(els.file.files[0]));
for (const ev of ["dragenter", "dragover"]) els.drop.addEventListener(ev, (e) => { e.preventDefault(); els.drop.classList.add("over"); });
for (const ev of ["dragleave", "drop"]) els.drop.addEventListener(ev, () => els.drop.classList.remove("over"));
els.drop.addEventListener("drop", (e) => { e.preventDefault(); takeFile(e.dataTransfer.files[0]); });

let timer;
els.overrides.addEventListener("input", () => {
  store.set(els.overrides.value);
  if (sg) checkOverrides();
  clearTimeout(timer);
  timer = setTimeout(run, 300);
});

els.add.addEventListener("submit", (e) => {
  e.preventDefault();
  let doc = {};
  if (els.overrides.value.trim()) {
    try { doc = JSON.parse(els.overrides.value); } catch { /* validation message already shown */ }
    if (typeof doc !== "object" || doc === null || Array.isArray(doc)) doc = {};
    if (!checkOverrides()) { els.ovStatus.textContent += " Fix it before adding entries."; return; }
  }
  const title = els.oTitle.value.trim();
  const isbn = els.oIsbn.value.trim(), format = els.oFormat.value;
  if (!title) return;
  if (els.oExclude.checked) doc[title] = { exclude: true };
  else if (isbn && !format) doc[title] = isbn;
  else if (isbn || format) doc[title] = { ...(isbn && { isbn }), ...(format && { format }) };
  else { els.ovStatus.className = "status bad"; els.ovStatus.textContent = "Give an ISBN, a format, or tick “Leave out”."; return; }
  els.overrides.value = JSON.stringify(doc, null, 2);
  els.add.reset();
  els.overrides.dispatchEvent(new Event("input"));
});

els.download.addEventListener("click", () => {
  const url = URL.createObjectURL(new Blob([outputText], { type: "text/csv;charset=utf-8" }));
  const a = Object.assign(document.createElement("a"), { href: url, download: OUTPUT_NAME });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

// ---- start ------------------------------------------------------------------
els.overrides.value = store.get();
if (els.overrides.value.trim()) els.advanced.open = true;

try {
  const res = await fetch("sg2gr.wasm");
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  sg = await loadSg2gr(await res.arrayBuffer());
  checkOverrides();
} catch (e) {
  showError(`Couldn't load the converter (${e.message}). If you opened this file directly, serve the folder over http instead, e.g. "make serve".`);
}
