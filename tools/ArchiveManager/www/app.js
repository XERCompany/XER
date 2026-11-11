/* XER Archive Manager — single-page UI.
 * Talks to /api/* endpoints served by the ArchiveManager.exe backend.
 */
"use strict";

const state = {
  cwd: "",
  archive: "",          // currently open archive path
  entries: [],          // archive entries
  selected: new Set(),  // selected entry paths
  uploading: false,
};

const $ = (id) => document.getElementById(id);
const statusEl = $("status");

function setStatus(text, isErr) {
  statusEl.textContent = text;
  statusEl.classList.toggle("err", !!isErr);
}

function toast(msg, kind) {
  const el = document.createElement("div");
  el.className = "toast" + (kind ? " " + kind : "");
  el.textContent = msg;
  $("toasts").appendChild(el);
  setTimeout(() => el.remove(), kind === "err" ? 8000 : 4000);
}

async function api(path, opts) {
  const res = await fetch(path, opts);
  const text = await res.text();
  let data;
  try { data = JSON.parse(text); }
  catch { data = { error: text || ("HTTP " + res.status) }; }
  if (!res.ok || data.error) {
    const err = new Error(data.error || ("HTTP " + res.status));
    err.status = res.status;
    throw err;
  }
  return data;
}

const post = (path, obj) => api(path, {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify(obj || {}),
});

function fmtSize(n) {
  if (n == null) return "";
  if (n < 1024) return n + " B";
  if (n < 1048576) return (n / 1024).toFixed(1) + " KB";
  if (n < 1073741824) return (n / 1048576).toFixed(1) + " MB";
  return (n / 1073741824).toFixed(2) + " GB";
}

function fmtTime(sec) {
  if (!sec) return "";
  const d = new Date(sec * 1000);
  return d.toLocaleDateString() + " " + d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

function esc(s) {
  return String(s).replace(/[&<>"']/g, c =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

// ---------------------------------------------------------------- FS browser

async function loadFs(path) {
  const q = path ? "?path=" + encodeURIComponent(path) : "";
  const data = await api("/api/fs" + q);
  state.cwd = data.path;
  renderBreadcrumb(data.path, data.parent);
  renderFsList(data.entries);
  setStatus("root: " + data.path);
}

function renderBreadcrumb(path, parent) {
  const parts = path.replace(/\\/g, "/").split("/").filter(Boolean);
  let html = parent ? `<a href="#" data-up="1" title="Parent">‹ up</a> · ` : "";
  let acc = "";
  // Rebuild cumulative path — works for both / and C:/ style roots.
  const isWin = /^[A-Za-z]:/.test(path);
  if (isWin) acc = parts.shift() + ":/";
  for (const p of parts) {
    acc += (acc.endsWith("/") ? "" : "/") + p;
    html += `<span class="crumb" data-path="${esc(acc)}">${esc(p)}</span> / `;
  }
  $("breadcrumb").innerHTML = html;
  $("breadcrumb").querySelectorAll(".crumb").forEach(el => {
    el.onclick = () => loadFs(el.dataset.path).catch(showErr);
  });
  const up = $("breadcrumb").querySelector("[data-up]");
  if (up) up.onclick = (e) => { e.preventDefault(); loadFs(parent).catch(showErr); };
}

function renderFsList(entries) {
  const list = $("fs-list");
  list.innerHTML = "";
  for (const e of entries) {
    const row = document.createElement("div");
    row.className = "file-row" +
      (state.archive === e.path ? " sel" : "");
    const icon = e.type === "dir" ? "📁" : e.is_archive ? "🗜" : "📄";
    row.innerHTML =
      `<span class="icon">${icon}</span>` +
      `<span class="name" title="${esc(e.path)}">${esc(e.name)}</span>` +
      `<span class="meta">${e.type === "dir" ? "" : fmtSize(e.size)}</span>`;
    row.onclick = () => {
      if (e.type === "dir") loadFs(e.path).catch(showErr);
      else if (e.is_archive) openArchive(e.path).catch(showErr);
    };
    row.oncontextmenu = (ev) => {
      ev.preventDefault();
      fsContextMenu(ev.clientX, ev.clientY, e);
    };
    list.appendChild(row);
  }
  if (!entries.length) {
    list.innerHTML = '<div class="file-row" style="color:var(--muted)">(empty)</div>';
  }
}

function fsContextMenu(x, y, e) {
  // Minimal context menu: delete + (for dirs) nothing else.
  const menu = document.createElement("div");
  menu.style.cssText =
    `position:fixed;left:${x}px;top:${y}px;z-index:100;background:var(--bg3);` +
    `border:1px solid var(--border);border-radius:6px;padding:4px;` +
    `box-shadow:0 4px 16px rgba(0,0,0,.5)`;
  const mk = (label, fn, danger) => {
    const b = document.createElement("button");
    b.textContent = label;
    b.style.cssText = "display:block;width:100%;text-align:left;border:0;" +
      "background:none;padding:6px 12px;border-radius:4px;" +
      (danger ? "color:#ff9c9c" : "");
    b.onmouseenter = () => b.style.background = "var(--bg2)";
    b.onmouseleave = () => b.style.background = "none";
    b.onclick = () => { menu.remove(); fn(); };
    menu.appendChild(b);
  };
  if (e.type === "dir") {
    mk("New archive inside…", () => promptNewArchive(e.path));
  } else {
    mk("Download file", () => downloadFsFile(e.path));
  }
  mk("Delete", () => deleteFsEntry(e), true);
  document.body.appendChild(menu);
  setTimeout(() => menu.remove(), 6000);
  menu.onclick = (ev) => { if (ev.target === menu) menu.remove(); };
}

async function deleteFsEntry(e) {
  if (!confirm(`Delete ${e.type === "dir" ? "folder" : "file"} "${e.name}"?` +
               (e.type === "dir" ? "\nContents will be removed recursively." : ""))) return;
  try {
    await post("/api/fs/delete", { path: e.path });
    toast("Deleted " + e.name, "ok");
    loadFs(state.cwd).catch(showErr);
    if (state.archive === e.path) closeArchive();
  } catch (err) { toast(err.message, "err"); }
}

async function downloadFsFile(path) {
  const res = await fetch("/api/fs/download?path=" + encodeURIComponent(path));
  if (!res.ok) { toast("Download failed", "err"); return; }
  const blob = await res.blob();
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = path.split(/[\\/]/).pop();
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

// ------------------------------------------------------------- Archive view

async function openArchive(path) {
  state.archive = path;
  state.selected.clear();
  state.entries = [];
  $("archive-view").classList.remove("hidden");
  $("empty-state").classList.add("hidden");
  await refreshArchive();
}

function closeArchive() {
  state.archive = "";
  state.entries = [];
  state.selected.clear();
  $("archive-view").classList.add("hidden");
  $("empty-state").classList.remove("hidden");
}

async function refreshArchive() {
  if (!state.archive) return;
  setStatus("reading " + state.archive + "…");
  try {
    const [info, list] = await Promise.all([
      api("/api/archive/info?path=" + encodeURIComponent(state.archive)),
      api("/api/archive/list?path=" + encodeURIComponent(state.archive)),
    ]);
    renderArcInfo(info);
    state.entries = list.entries || [];
    renderEntries();
    setStatus("");
  } catch (err) {
    setStatus(err.message, true);
    toast(err.message, "err");
  }
}

function renderArcInfo(info) {
  const el = $("arc-info");
  const badges = [];
  if (info.format) badges.push(`<span class="badge">${esc(info.format)}</span>`);
  if (info.compression) badges.push(`<span class="badge">${esc(info.compression)}</span>`);
  if (info.encrypted) badges.push(`<span class="badge enc">encrypted</span>`);
  el.innerHTML =
    `<b>${esc(state.archive.split(/[\\/]/).pop())}</b>` +
    `<span>${fmtSize(info.file_size)}</span>` +
    badges.join("") +
    `<span>${info.entry_count} entries</span>`;
}

function renderEntries() {
  const list = $("arc-list");
  list.innerHTML = "";
  // Sort: dirs first, then name.
  const sorted = [...state.entries].sort((a, b) => {
    if (a.is_dir !== b.is_dir) return a.is_dir ? -1 : 1;
    return a.path.localeCompare(b.path);
  });
  for (const e of sorted) {
    const row = document.createElement("div");
    row.className = "file-row" + (state.selected.has(e.path) ? " sel" : "");
    const icon = e.is_dir ? "📁" : "📄";
    row.innerHTML =
      `<input type="checkbox" ${state.selected.has(e.path) ? "checked" : ""}>` +
      `<span class="icon">${icon}</span>` +
      `<span class="name" title="${esc(e.path)}">${esc(e.path)}</span>` +
      `<span class="meta size">${e.is_dir ? "" : fmtSize(e.size)}</span>` +
      `<span class="meta">${fmtTime(e.mtime)}</span>`;
    const cb = row.querySelector("input");
    cb.onclick = (ev) => ev.stopPropagation();
    cb.onchange = () => {
      if (cb.checked) state.selected.add(e.path);
      else state.selected.delete(e.path);
      row.classList.toggle("sel", cb.checked);
      updateSelCount();
    };
    row.onclick = () => {
      cb.checked = !cb.checked;
      cb.onchange();
    };
    row.ondblclick = () => {
      if (!e.is_dir) downloadEntry(e.path);
    };
    list.appendChild(row);
  }
  updateSelCount();
  if (!sorted.length) {
    list.innerHTML = '<div class="file-row" style="color:var(--muted)">(empty archive)</div>';
  }
}

function updateSelCount() {
  const n = state.selected.size;
  $("sel-count").textContent = n ? n + " selected" : "";
  $("btn-download-sel").disabled = n !== 1;
  $("btn-delete-sel").disabled = n === 0;
  $("btn-extract-sel").disabled = n === 0;
}

function selectedEntries() {
  return [...state.selected];
}

async function extractEntries(entries) {
  try {
    const data = await post("/api/archive/extract", {
      path: state.archive,
      entries: entries && entries.length ? entries : undefined,
    });
    toast(`Extracted ${data.extracted} entries → ${data.dest}`, "ok");
  } catch (err) { toast(err.message, "err"); }
}

async function downloadEntry(entryPath) {
  const url = "/api/archive/download?path=" +
    encodeURIComponent(state.archive) +
    "&entry=" + encodeURIComponent(entryPath);
  const a = document.createElement("a");
  a.href = url;
  a.download = entryPath.split("/").pop();
  document.body.appendChild(a);
  a.click();
  a.remove();
}

async function deleteEntries() {
  const entries = selectedEntries();
  if (!entries.length) return;
  if (!confirm(`Delete ${entries.length} entr${entries.length === 1 ? "y" : "ies"} from the archive?`)) return;
  try {
    const data = await post("/api/archive/delete", {
      path: state.archive, entries,
    });
    toast(`Deleted ${data.removed} entries`, "ok");
    state.selected.clear();
    await refreshArchive();
  } catch (err) { toast(err.message, "err"); }
}

async function uploadFiles(fileList) {
  if (!fileList.length) return;
  if (state.uploading) return;
  state.uploading = true;
  try {
    const fd = new FormData();
    fd.append("archive", state.archive);
    for (const f of fileList) fd.append("file", f, f.name);
    setStatus("uploading " + fileList.length + " file(s)…");
    const data = await api("/api/archive/upload", { method: "POST", body: fd });
    toast(`Added ${data.added} file(s) to archive`, "ok");
    await refreshArchive();
  } catch (err) {
    toast(err.message, "err");
  } finally {
    state.uploading = false;
    setStatus("");
  }
}

async function promptNewArchive(dir) {
  const name = prompt("New archive name (extension picks format):\n" +
    "e.g. backup.zip, disk.7z, photos.tar.gz", "archive.zip");
  if (!name) return;
  const path = (dir.replace(/[\\/]+$/, "") + "/" + name).replace(/\\/g, "/");
  try {
    await post("/api/archive/create", { path });
    toast("Created " + name, "ok");
    loadFs(dir).catch(showErr);
  } catch (err) { toast(err.message, "err"); }
}

// -------------------------------------------------------------------- events

function showErr(err) { toast(err.message || String(err), "err"); }

$("btn-up").onclick = () => {
  const p = $("breadcrumb").querySelector("[data-up]");
  if (p) p.click();
};
$("btn-refresh-fs").onclick = () => loadFs(state.cwd).catch(showErr);
$("btn-new-arc").onclick = () => promptNewArchive(state.cwd);
$("btn-refresh-arc").onclick = () => refreshArchive();
$("btn-extract-all").onclick = () => extractEntries([]);
$("btn-extract-sel").onclick = () => extractEntries(selectedEntries());
$("btn-delete-sel").onclick = deleteEntries;
$("btn-upload").onclick = () => $("file-input").click();
$("file-input").onchange = (ev) => {
  uploadFiles([...ev.target.files]);
  ev.target.value = "";
};

// Keyboard: Esc closes archive view.
document.addEventListener("keydown", (ev) => {
  if (ev.key === "Escape" && state.archive) closeArchive();
});

// ---------------------------------------------------------------------- init

(async function init() {
  try {
    await loadFs("");
  } catch (err) {
    setStatus("server unreachable: " + err.message, true);
  }
})();
