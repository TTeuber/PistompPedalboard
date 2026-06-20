// app.js -- generic pedalboard UI. Builds itself from /api/state, so new effects
// (and, later, reordering) appear with no frontend changes. Pure vanilla JS.

const $ = (sel) => document.querySelector(sel);
const board = $("#board");

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

function pedalCard(fx) {
  const card = document.createElement("div");
  card.className = "pedal" + (fx.enabled ? "" : " off");

  const head = document.createElement("div");
  head.className = "pedal-head";
  const title = document.createElement("h2");
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
  return card;
}

function render(state) {
  board.innerHTML = "";
  state.effects.forEach((fx) => board.appendChild(pedalCard(fx)));
  const m = Math.round(state.master * 100);
  $("#master").value = m; $("#masterVal").textContent = m + "%";
  $("#bypass").classList.toggle("active", state.bypassed);
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
