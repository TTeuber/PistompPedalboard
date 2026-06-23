// app.js -- pedalboard web UI. Mirrors the device's Input / FX / Output layout:
// an Input lane, an 8-slot FX grid (add / move / remove + footswitch assign),
// and an Output lane. Everything is built from /api/state, so new effects appear
// with no frontend changes. Pure vanilla JS.

const $ = (sel) => document.querySelector(sel);

// Footswitch colors, FS1..FS4 -- must match the device (kFsColors in
// ui_controller.cpp): red / green / blue / yellow.
const FS_COLORS = ["#ff453a", "#30d158", "#4d96ff", "#ffcc00"];

async function api(path, body) {
  const opt = body
    ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }
    : {};
  const res = await fetch(path, opt);
  if (!res.ok) throw new Error(path + " -> " + res.status);
  const text = await res.text();
  return text ? JSON.parse(text) : null;
}

// A few special params render as a toggle (0/1) or a dropdown rather than a slider.
const ENUMS = { type: ["Overdrive", "Distortion", "Fuzz"] };

function paramControl(effectType, p) {
  const wrap = document.createElement("div");
  wrap.className = "param";

  const label = document.createElement("label");
  label.textContent = p.name;
  const val = document.createElement("output");
  label.appendChild(val);
  wrap.appendChild(label);

  // boolean-ish 0..1 with no unit -> toggle (e.g. Reverb "Freeze")
  if (p.min === 0 && p.max === 1 && !p.unit && !ENUMS[p.id]) {
    const btn = document.createElement("button");
    btn.className = "toggle";
    const sync = (v) => { btn.classList.toggle("on", v > 0.5); val.textContent = v > 0.5 ? "on" : "off"; };
    sync(p.value);
    btn.onclick = () => { const nv = btn.classList.contains("on") ? 0 : 1; sync(nv); send(effectType, p.id, nv); };
    wrap.appendChild(btn);
    return wrap;
  }

  // enumerated index -> dropdown
  if (ENUMS[p.id]) {
    const sel = document.createElement("select");
    ENUMS[p.id].forEach((name, i) => {
      const o = document.createElement("option");
      o.value = i; o.textContent = name; sel.appendChild(o);
    });
    sel.value = Math.round(p.value);
    val.textContent = ENUMS[p.id][Math.round(p.value)] || "";
    sel.onchange = () => { val.textContent = ENUMS[p.id][+sel.value]; send(effectType, p.id, +sel.value); };
    wrap.appendChild(sel);
    return wrap;
  }

  // default -> slider
  const fmt = (v) => p.unit === "%" ? `${Math.round(v)}%` : (p.unit ? `${v.toFixed(2)} ${p.unit}` : v.toFixed(2));
  const range = document.createElement("input");
  range.type = "range";
  range.min = p.min; range.max = p.max;
  range.step = (p.max - p.min) <= 4 ? 0.01 : 1;
  range.value = p.value;
  val.textContent = fmt(p.value);
  range.oninput = () => { val.textContent = fmt(+range.value); send(effectType, p.id, +range.value); };
  wrap.appendChild(range);
  return wrap;
}

let sendTimers = {};
function send(effect, param, value) {
  // light debounce per-control so a slider drag doesn't flood the Pi
  const key = effect + "/" + param;
  clearTimeout(sendTimers[key]);
  sendTimers[key] = setTimeout(() => api("/api/param", { effect, param, value }).catch(console.error), 30);
}

// Footswitch-assignment strip: four colored chips. Clicking one cycles this
// pedal's binding for that footswitch -- unassigned -> on -> inverted -> clear --
// exactly like pressing the physical switch on the device's assign page.
function assignStrip(fx) {
  const strip = document.createElement("div");
  strip.className = "assign";
  for (let i = 0; i < 4; i++) {
    const chip = document.createElement("button");
    chip.className = "fschip";
    chip.style.setProperty("--fs", FS_COLORS[i]);
    const active = fx.fsAssign === i;
    chip.textContent = active && fx.fsMode === 1 ? (i + 1) + "⌀" : (i + 1);
    if (active) chip.classList.add("on");
    if (active && fx.fsMode === 1) chip.classList.add("inv");
    chip.title = "Footswitch " + (i + 1);
    chip.onclick = async () => {
      let fs, mode;
      if (fx.fsAssign !== i)      { fs = i;  mode = 0; }  // bind, normal
      else if (fx.fsMode === 0)   { fs = i;  mode = 1; }  // -> inverted
      else                        { fs = -1; mode = 0; }  // -> clear
      render(await api("/api/assign", { effect: fx.type, fs, mode }));
    };
    strip.appendChild(chip);
  }
  return strip;
}

function pedalCard(fx, inGrid) {
  const card = document.createElement("div");
  card.className = "pedal" + (fx.enabled ? "" : " off");

  const head = document.createElement("div");
  head.className = "pedal-head";
  const title = document.createElement("h3");
  title.textContent = fx.name;
  const power = document.createElement("button");
  power.className = "power" + (fx.enabled ? " on" : "");
  power.title = "Enable/disable";
  power.onclick = () => {
    const on = !power.classList.contains("on");
    power.classList.toggle("on", on);
    card.classList.toggle("off", !on);
    api("/api/enabled", { effect: fx.type, enabled: on }).catch(console.error);
  };
  head.appendChild(title);
  head.appendChild(power);
  card.appendChild(head);

  fx.params.forEach((p) => card.appendChild(paramControl(fx.type, p)));

  if (inGrid) {
    card.appendChild(assignStrip(fx));
    const foot = document.createElement("div");
    foot.className = "pedal-foot";
    const mk = (txt, fn, cls) => {
      const b = document.createElement("button");
      b.className = "mini" + (cls ? " " + cls : "");
      b.textContent = txt; b.onclick = fn; return b;
    };
    foot.appendChild(mk("◀", async () => render(await api("/api/fx/move", { slot: fx.slot, dir: -1 }))));
    foot.appendChild(mk("✕", async () => render(await api("/api/fx/remove", { slot: fx.slot })), "danger"));
    foot.appendChild(mk("▶", async () => render(await api("/api/fx/move", { slot: fx.slot, dir: 1 }))));
    card.appendChild(foot);
  }
  return card;
}

// An empty FX slot: a dashed tile whose dropdown adds a fresh effect instance.
function emptySlot(slot, kinds) {
  const tile = document.createElement("div");
  tile.className = "slot-empty";
  const sel = document.createElement("select");
  const ph = document.createElement("option");
  ph.value = ""; ph.textContent = "+ add effect";
  sel.appendChild(ph);
  kinds.forEach((k, i) => {
    const o = document.createElement("option");
    o.value = i; o.textContent = k.name; sel.appendChild(o);
  });
  sel.onchange = async () => {
    if (sel.value === "") return;
    render(await api("/api/fx/add", { slot, kind: +sel.value }));
  };
  tile.appendChild(sel);
  return tile;
}

function render(state) {
  // global controls
  const m = Math.round(state.master * 100);
  $("#master").value = m; $("#masterVal").textContent = m + "%";
  $("#bypass").classList.toggle("active", state.bypassed);

  const inputCol = $("#inputCol"), outputCol = $("#outputCol"), grid = $("#fxGrid");
  inputCol.innerHTML = ""; outputCol.innerHTML = ""; grid.innerHTML = "";

  state.effects.filter((e) => e.section === "input").forEach((e) => inputCol.appendChild(pedalCard(e, false)));
  state.effects.filter((e) => e.section === "output").forEach((e) => outputCol.appendChild(pedalCard(e, false)));

  // FX grid: lay effects into their slot index; fill the gaps with add-tiles.
  const bySlot = {};
  state.effects.filter((e) => e.section === "fx").forEach((e) => { bySlot[e.slot] = e; });
  for (let s = 0; s < state.fxSlotCount; s++) {
    grid.appendChild(bySlot[s] ? pedalCard(bySlot[s], true) : emptySlot(s, state.fxKinds || []));
  }
}

async function refresh() { render(await api("/api/state")); }

async function loadPresets() {
  const names = await api("/api/presets");
  const sel = $("#presetSelect");
  sel.innerHTML = '<option value="">—</option>';
  names.forEach((n) => { const o = document.createElement("option"); o.value = n; o.textContent = n; sel.appendChild(o); });
}

// --- global controls ---
$("#master").oninput = (e) => {
  const m = +e.target.value;
  $("#masterVal").textContent = m + "%";
  api("/api/master", { value: m / 100 }).catch(console.error);
};
$("#bypass").onclick = () => {
  const active = !$("#bypass").classList.contains("active");
  $("#bypass").classList.toggle("active", active);
  api("/api/bypass", { bypassed: active }).catch(console.error);
};
$("#presetLoad").onclick = async () => {
  const name = $("#presetSelect").value;
  if (!name) return;
  render(await api("/api/preset/load", { name }));
};
$("#presetSave").onclick = async () => {
  const name = $("#presetName").value.trim();
  if (!name) return;
  await api("/api/preset/save", { name });
  $("#presetName").value = "";
  loadPresets();
};

async function pollTelemetry() {
  try {
    const t = await api("/api/telemetry");
    $("#telemetry").textContent = `DSP ${(t.dspPermille / 10).toFixed(1)}%  ·  xruns ${t.xruns}`;
  } catch (_) {}
}

refresh().catch(console.error);
loadPresets().catch(console.error);
setInterval(pollTelemetry, 1000);
