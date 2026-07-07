// Holy Shifter WebView UI — faithful replica of the Visage design (fixed 700x928,
// scaled to fit). Big rotary shift knob + pagan artwork + the exact section layout.
import * as Juce from "./juce.js";

// Surface JS errors + status to native stderr (so the UI can be debugged headlessly).
const nlog = (...a) => { try { Juce.getNativeFunction("jsLog")(a.map(String).join(" ")); } catch (e) {} };
window.addEventListener("error", (e) => nlog("JS ERROR:", e.message, "@", (e.filename || "") + ":" + e.lineno));
window.addEventListener("unhandledrejection", (e) => nlog("PROMISE REJECT:", (e && e.reason && e.reason.message) || (e && e.reason)));
nlog("main.js start; HOLY_PARAMS =", (window.HOLY_PARAMS || []).length);

const W = 700, H = 928;
const NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const BLACK = new Set([1, 3, 6, 8, 10]);

const stage = document.createElement("div"); stage.id = "stage";
const scrim = document.createElement("div"); scrim.className = "scrim"; stage.append(scrim);
document.getElementById("app").replaceWith(stage);

// ---- helpers -------------------------------------------------------------
function pos(el, x, y, w, h) { el.style.left = x + "px"; el.style.top = y + "px";
  if (w != null) el.style.width = w + "px"; if (h != null) el.style.height = h + "px"; return el; }
function add(cls, x, y, w, h) { const e = document.createElement("div"); e.className = cls; pos(e, x, y, w, h); stage.append(e); return e; }
function text(cls, str, x, y, w, h, align) { const e = add("txt " + cls, x, y, w, h); e.textContent = str;
  if (align) e.style.textAlign = align; e.style.display = "flex";
  e.style.alignItems = "center"; e.style.justifyContent = align === "right" ? "flex-end" : align === "center" ? "center" : "flex-start";
  return e; }

function scaleStage() { stage.style.setProperty("--s", Math.min(window.innerWidth / W, window.innerHeight / H)); }
window.addEventListener("resize", scaleStage); scaleStage();

// Pointer X -> [0,1] across a track, CORRECT under CSS `zoom`. Under zoom, WebKit can report
// getBoundingClientRect() in unzoomed layout px while clientX is in zoomed/visual px — which
// made slider clicks land off by the scale factor. Detect (via offsetWidth) whether the rect
// already includes the zoom; if not, scale the rect into the pointer's (visual) space.
function trackFrac(trk, clientX) {
  const r = trk.getBoundingClientRect();
  const layout = trk.offsetWidth || r.width;
  const s = parseFloat(getComputedStyle(stage).getPropertyValue("--s")) || 1;
  const f = (Math.abs(r.width - layout * s) <= Math.abs(r.width - layout)) ? 1 : s;
  const w = r.width * f;
  return w > 0 ? Math.min(1, Math.max(0, (clientX - r.left * f) / w)) : 0;
}

// ---- static chrome -------------------------------------------------------
add("accent-line", 0, 0);
text("title", "H O L Y   S H I F T E R", 27, 13, 440, 31);
text("subtitle", "Frequency Shifter with Harmonic Quantisation", 29, 45, 400, 12);
const logo = document.createElement("img"); logo.className = "logo"; logo.src = "heathen-machines-logo.png";
pos(logo, 609, 18, 51, 53); stage.append(logo);
add("strip", 0, 94, null, 0);                              // preset separator line (border-top draws it)
add("strip", 0, 101, null, 36).style.background = "rgba(12,12,14,0.45)"; // mode strip bg

// section strips (bg + header)
function strip(label, y, h, bg) { const s = add("strip", 0, y, null, h); if (bg) s.style.background = bg;
  if (label) text("hdr", label, 14, y + 6, 180, 10); return s; }
strip("FREQ MODULATION", 405, 110, "rgba(14,14,16,0.30)");
strip("DELAY", 517, 134, "rgba(14,14,16,0.30)");
strip("DELAY MODULATION", 651, 92, "rgba(14,14,16,0.30)");
const maskStripEl = strip("MASK", 761, 90, "rgba(25,25,29,0.50)");
strip(null, 851, 72, "linear-gradient(rgba(15,15,17,0.60),rgba(10,10,12,0.60))"); // mix footer
add("divider", 0, 516); add("divider", 0, 760);
add("accent-line", 0, 926).style.opacity = ".5";

// spectral panel
const panel = add("panel", 245, 158, 430, 240);
const ph = document.createElement("div"); ph.className = "hairline"; panel.append(ph);
const spectralHdr = text("hdr", "SPECTRAL CONTROLS", 258, 167, 150, 10);

// labels (right-aligned, matching draw())
const L = (s, x, y, w, align = "right", cls = "lbl") => text(cls, s, x, y, w, 16, align);
const specLabels = [
  L("Quantize", 254, 240, 62), L("Envelope", 254, 270, 62), L("Transients", 254, 300, 62),
  L("Sens", 456, 300, 54), L("Smear", 270, 360, 40, "left"),
  L("Texture", 250, 342, 64), L("Density", 454, 342, 62),
  text("tip", "(Adjust with care while playing)", 387, 379, 250, 10, "left"),
];
const fmDepthLbl = L("Depth", 27, 434, 42), fmRateLbl = L("Rate", 27, 464, 42);
L("Time", 116, 545, 36); L("Feedback", 28, 579, 60); L("Damping", 348, 579, 58);
const slopeLbl = L("Slope", 28, 613, 42);
const dmDepthLbl = L("Depth", 28, 681, 42), dmRateLbl = L("Rate", 28, 711, 42);
const maskLabels = [L("Transition", 218, 789, 60), L("Low", 28, 821, 28), L("High", 369, 821, 30)];
L("DRY / WET", 28, 885, 72);

// ---- generic controls ----------------------------------------------------
// opts (match Visage HolySlider): { decimals=1, suffix="", adaptive, secondsAbove,
// sync:{ toggleId, divisionId, reversed } } — Sync on => division label + snap + reversed.
function slider(id, x, y, w, h, opts = {}) {
  const el = add("vslider", x, y, w, h);
  const trk = document.createElement("div"); trk.className = "strack";
  const bg = document.createElement("div"); bg.className = "sbg";
  const fill = document.createElement("div"); fill.className = "sfill";
  const dot = document.createElement("div"); dot.className = "sdot";
  const val = document.createElement("div"); val.className = "sval";
  trk.append(bg, fill, dot); el.append(trk, val);
  const st = Juce.getSliderState(id);
  const sync = opts.sync || null;
  const syncSt = sync ? Juce.getToggleState(sync.toggleId) : null;
  const divSt = sync ? Juce.getComboBoxState(sync.divisionId) : null;
  const synced = () => !!(syncSt && syncSt.getValue());
  const fmtFree = () => {
    let v = st.getScaledValue(); if (!isFinite(v)) v = 0;
    if (opts.secondsAbove != null && Math.abs(v) >= opts.secondsAbove) return (v / 1000).toFixed(2) + " s";
    if (opts.decimals === 0) return String(Math.trunc(v)) + (opts.suffix || "");
    let dec = opts.decimals != null ? opts.decimals : 1;
    if (opts.adaptive) { const a = Math.abs(v); if (a > 0 && a < 0.01) dec = Math.max(dec, 3); else if (a < 0.1) dec = Math.max(dec, 2); }
    return v.toFixed(dec) + (opts.suffix || "");
  };
  const divData = () => { const choices = (divSt.properties && divSt.properties.choices) || []; return { choices, idx: divSt.getChoiceIndex(), n: choices.length }; };
  // Optional geometric/log travel (rate sliders): slider position p maps to
  // value = start * (end/start)^p. JUCE's WebView bridge only transmits a single
  // skew (= 1 for our lambda-defined ranges), so the log curve is applied here in
  // the UI. Needs start > 0; until valid range props arrive we fall back to the
  // stock linear (skew = 1) mapping. Depth sliders omit opts.log and are unchanged.
  const logOK = () => opts.log && st.properties.start > 0 && st.properties.end > st.properties.start;
  const posFromVal = (v) => {
    const { start, end } = st.properties;
    return Math.min(1, Math.max(0, Math.log(v / start) / Math.log(end / start)));
  };
  const valFromPos = (p) => {
    const { start, end } = st.properties;
    return start * Math.pow(end / start, p);
  };
  const render = () => {
    if (synced()) {
      const { choices, idx, n } = divData();
      const norm = n > 1 ? idx / (n - 1) : 0, vis = sync.reversed ? 1 - norm : norm;
      fill.style.width = (vis * 100) + "%"; dot.style.left = (vis * 100) + "%";
      val.textContent = choices[idx] != null ? choices[idx] : "";
    } else {
      const n = logOK() ? posFromVal(st.getScaledValue()) : st.getNormalisedValue();
      fill.style.width = (n * 100) + "%"; dot.style.left = (n * 100) + "%"; val.textContent = fmtFree();
    }
  };
  const setFromX = (clientX) => {
    const vis = trackFrac(trk, clientX);   // zoom-correct fraction across the track
    if (synced()) { const { n } = divData(); const norm = sync.reversed ? 1 - vis : vis;
      divSt.setChoiceIndex(Math.max(0, Math.min(n - 1, Math.round(norm * (n - 1))))); render(); }
    else {
      // For a log slider, convert the geometric value back into the skew = 1
      // normalised space the host expects, so the parameter receives start*(end/start)^vis.
      let n = vis;
      if (logOK()) { const { start, end } = st.properties; n = (valFromPos(vis) - start) / (end - start); }
      st.setNormalisedValue(n);
      fill.style.width = (vis * 100) + "%"; dot.style.left = (vis * 100) + "%"; val.textContent = fmtFree();
    }
  };
  let dragging = false;
  trk.addEventListener("pointerdown", (e) => { dragging = true; trk.setPointerCapture(e.pointerId); if (!synced()) st.sliderDragStarted(); setFromX(e.clientX); });
  trk.addEventListener("pointermove", (e) => { if (dragging) setFromX(e.clientX); });
  trk.addEventListener("pointerup", (e) => { dragging = false; try { trk.releasePointerCapture(e.pointerId); } catch {} if (!synced()) st.sliderDragEnded(); render(); });
  st.valueChangedEvent.addListener(render); st.propertiesChangedEvent.addListener(render);
  if (syncSt) syncSt.valueChangedEvent.addListener(render);
  if (divSt) { divSt.valueChangedEvent.addListener(render); divSt.propertiesChangedEvent.addListener(render); }
  render();
  return el;
}

function toggleSwitch(id, x, y, w, h, label) {
  const el = add("switch", x, y, w, h);
  const trk = document.createElement("div"); trk.className = "track";
  const dot = document.createElement("div"); dot.className = "dot"; trk.append(dot);
  el.append(trk);
  if (label) { const lab = document.createElement("span"); lab.className = "swlabel"; lab.textContent = label; el.append(lab); }
  const st = Juce.getToggleState(id);
  const refresh = () => el.classList.toggle("on", st.getValue());
  el.addEventListener("click", () => st.setValue(!st.getValue()));
  st.valueChangedEvent.addListener(refresh); refresh();
  return el;
}

function combo(id, x, y, w, h) {
  const sel = document.createElement("select"); sel.className = "combo"; pos(sel, x, y, w, h); stage.append(sel);
  const st = Juce.getComboBoxState(id);
  const populate = () => { const ch = (st.properties && st.properties.choices) || []; sel.innerHTML = "";
    ch.forEach((c, i) => { const o = document.createElement("option"); o.value = i; o.textContent = c; sel.append(o); });
    sel.value = String(st.getChoiceIndex()); };
  sel.addEventListener("change", () => st.setChoiceIndex(parseInt(sel.value, 10)));
  st.propertiesChangedEvent.addListener(populate);
  st.valueChangedEvent.addListener(() => { sel.value = String(st.getChoiceIndex()); }); populate();
  return sel;
}

function segmented(id, x, y, w, h, segs, onChange) {
  const el = add("seg", x, y, w, h);
  const st = Juce.getComboBoxState(id);
  const opts = segs.map((s, i) => { const o = document.createElement("div"); o.className = "segopt"; o.textContent = s;
    o.addEventListener("click", () => st.setChoiceIndex(i)); el.append(o); return o; });
  const refresh = () => { const idx = st.getChoiceIndex(); opts.forEach((o, i) => o.classList.toggle("on", i === idx)); if (onChange) onChange(idx); };
  st.valueChangedEvent.addListener(refresh); st.propertiesChangedEvent.addListener(refresh); refresh();
  return el;
}

function piano(x, y, w, h) {
  const el = add("piano", x, y, w, h);
  const whiteIdx = [0, 2, 4, 5, 7, 9, 11];
  const ww = w / 7, bw = ww * 0.62, bh = h * 0.62;
  // white keys
  whiteIdx.forEach((pc, i) => makeKey(pc, i * ww, 0, ww - 1, h, "white"));
  // black keys positioned over the gaps after white indices 0,1,3,4,5
  const blackAfter = { 1: 0, 3: 1, 6: 3, 8: 4, 10: 5 };
  for (const pc of [1, 3, 6, 8, 10]) makeKey(pc, (blackAfter[pc] + 1) * ww - bw / 2, 0, bw, bh, "black");
  function makeKey(pc, kx, ky, kw, kh, kind) {
    const k = document.createElement("div"); k.className = "pk " + kind; k.textContent = NOTE[pc];
    k.style.left = kx + "px"; k.style.top = ky + "px"; k.style.width = kw + "px"; k.style.height = kh + "px";
    k.style.position = "absolute"; el.append(k);
    const st = Juce.getToggleState("scaleNote" + pc);
    const refresh = () => k.classList.toggle("on", st.getValue());
    k.addEventListener("click", () => st.setValue(!st.getValue()));
    st.valueChangedEvent.addListener(refresh); refresh();
  }
  el.style.position = "absolute"; return el;
}

// ---- rotary shift knob (bipolar, symmetric-log display) -------------------
function shiftKnob(x, y, w, h) {
  const el = add("knob", x, y, w, h);
  const cv = document.createElement("canvas"); el.append(cv);
  cv.style.width = w + "px"; cv.style.height = h + "px";
  const ctx = cv.getContext("2d");
  // Size the backing store to the REAL on-screen pixels (stage zoom × devicePixelRatio)
  // so the knob stays crisp when the window is resized; draw() works in design coords.
  function sizeCanvas() {
    const s = parseFloat(getComputedStyle(stage).getPropertyValue("--s")) || 1;
    const sf = Math.min(8, Math.max(1, s * (window.devicePixelRatio || 1)));
    cv.width = Math.round(w * sf); cv.height = Math.round(h * sf);
    ctx.setTransform(sf, 0, 0, sf, 0, 0);
  }
  sizeCanvas();
  const st = Juce.getSliderState("shiftHz");
  const A0 = -2.35619, A1 = 2.35619, LS = 10, LMAX = Math.log(1 + 5000 / LS);
  const dispFromKnob = (kn) => { const s = kn * 2 - 1, sg = s >= 0 ? 1 : -1, a = Math.abs(s); return sg * LS * (Math.exp(a * LMAX) - 1); };
  const pnFromKnob = (kn) => (dispFromKnob(kn) + 20000) / 40000;
  const knobFromPn = (pn) => { const pv = pn * 40000 - 20000, sg = pv >= 0 ? 1 : -1, a = Math.min(Math.abs(pv), 5000);
    return (sg * Math.log(1 + a / LS) / LMAX + 1) / 2; };

  function draw() {
    const kn = dragging ? dragKn : knobFromPn(st.getNormalisedValue());
    const ang = A0 + kn * (A1 - A0);
    const cx = w / 2, cy = h / 2, r = Math.min(w, h) * 0.36;
    ctx.clearRect(0, 0, w, h);
    const arc = (a0, a1, col, lw) => { ctx.beginPath(); ctx.lineWidth = lw; ctx.strokeStyle = col; ctx.lineCap = "round";
      ctx.arc(cx, cy, r, a0 - Math.PI / 2, a1 - Math.PI / 2); ctx.stroke(); };
    arc(A0, A1, "#252320", 2.5);                              // track
    const mid = A0 + 0.5 * (A1 - A0);                          // bipolar centre (top)
    if (Math.abs(ang - mid) > 0.01) arc(Math.min(mid, ang), Math.max(mid, ang), "#c9a96e", 2.5);
    // ticks
    for (let i = 0; i < 5; i++) { const t = A0 + (i / 4) * (A1 - A0);
      const ir = r + 7, or = r + 12;
      ctx.beginPath(); ctx.lineWidth = 1; ctx.strokeStyle = (i === 2) ? "#8a857d" : "#3e3a34";
      ctx.moveTo(cx + ir * Math.sin(t), cy - ir * Math.cos(t)); ctx.lineTo(cx + or * Math.sin(t), cy - or * Math.cos(t)); ctx.stroke(); }
    // indicator dot
    ctx.beginPath(); ctx.fillStyle = "#c9a96e"; ctx.arc(cx + r * Math.sin(ang), cy - r * Math.cos(ang), 6, 0, 2 * Math.PI); ctx.fill();
    // value text
    const v = dispFromKnob(kn);
    ctx.fillStyle = "#e8e4db"; ctx.textAlign = "center"; ctx.textBaseline = "middle";
    ctx.font = "300 32px 'Inter'";
    ctx.fillText(Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(1), cx, cy - 4);
    ctx.fillStyle = "#3e3a34"; ctx.font = "500 11px 'Inter'"; ctx.fillText("HZ", cx, cy + 22);
  }
  let dragging = false, dragKn = 0, startY = 0, startKn = 0, fine = false;
  el.addEventListener("pointerdown", (e) => { dragging = true; fine = e.shiftKey; el.setPointerCapture(e.pointerId);
    startY = e.clientY; startKn = knobFromPn(st.getNormalisedValue()); dragKn = startKn; st.sliderDragStarted(); });
  el.addEventListener("pointermove", (e) => { if (!dragging) return;
    const sens = fine ? 3000 : 300;   // match Visage: 300 px/sweep, Shift = 10x finer
    dragKn = Math.min(1, Math.max(0, startKn + (startY - e.clientY) / sens));
    st.setNormalisedValue(pnFromKnob(dragKn)); draw(); });
  el.addEventListener("pointerup", (e) => { dragging = false; try { el.releasePointerCapture(e.pointerId); } catch {} st.sliderDragEnded(); draw(); });
  el.addEventListener("dblclick", () => { st.setNormalisedValue(0.5); draw(); }); // reset to 0 Hz
  st.valueChangedEvent.addListener(draw); st.propertiesChangedEvent.addListener(draw);
  window.addEventListener("resize", () => { sizeCanvas(); draw(); });
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(draw);
  draw();
  return el;
}

// ---- preset bar ----------------------------------------------------------
function presetBar() {
  const mk = (cls, txt, x, y, w, h) => { const b = document.createElement("button"); b.className = cls; b.textContent = txt; pos(b, x, y, w, h); stage.append(b); return b; };
  const prev = mk("presetbtn", "‹", 36, 71, 20, 20);
  const next = mk("presetbtn", "›", 60, 71, 20, 20);
  const nameEl = add("preset-name", 91, 69, 300, 22);
  const dd = add("preset-dd", 91, 93, 250); dd.style.height = "auto"; dd.style.zIndex = "100";
  const save = mk("txtbtn primary", "SAVE", 452, 70, 58, 22);
  const del = mk("txtbtn outline", "DELETE", 518, 70, 72, 22);

  const fn = (n) => Juce.getNativeFunction(n);
  const list = fn("presetList"), cur = fn("presetCurrent"), load = fn("presetLoad"), saveP = fn("presetSave"), delP = fn("presetDelete");
  let names = [], current = "";
  const setName = (n) => { current = n || ""; nameEl.textContent = current || "—"; };

  async function refresh() {
    try { names = JSON.parse(await list()); } catch { names = []; }
    try { current = await cur(); } catch {}
    setName(current);
  }
  const closeDD = () => dd.classList.remove("open");
  function openDD() {
    dd.innerHTML = "";
    if (!names.length) { const e = document.createElement("div"); e.className = "preset-dd-item"; e.textContent = "(no presets)"; dd.append(e); }
    names.forEach((n) => {
      const it = document.createElement("div"); it.className = "preset-dd-item" + (n === current ? " current" : ""); it.textContent = n;
      it.addEventListener("click", async (e) => { e.stopPropagation(); await load(n); setName(n); closeDD(); });
      dd.append(it);
    });
    dd.classList.add("open");
  }
  nameEl.addEventListener("click", (e) => { e.stopPropagation(); dd.classList.contains("open") ? closeDD() : openDD(); });
  document.addEventListener("click", closeDD);

  async function step(d) { if (!names.length) return; let i = names.indexOf(current); if (i < 0) i = 0; i = (i + d + names.length) % names.length; await load(names[i]); setName(names[i]); }
  prev.addEventListener("click", (e) => { e.stopPropagation(); step(-1); });
  next.addEventListener("click", (e) => { e.stopPropagation(); step(1); });

  // SAVE -> inline rename field (the name label itself is a dropdown, not editable)
  save.addEventListener("click", (e) => {
    e.stopPropagation(); closeDD();
    nameEl.classList.add("editing"); nameEl.textContent = "";
    const inp = document.createElement("input"); inp.className = "preset-edit"; inp.value = current || "Untitled";
    nameEl.append(inp); inp.focus(); inp.select();
    let done = false;
    const finish = async (ok) => {
      if (done) return; done = true; nameEl.classList.remove("editing");
      const v = inp.value.trim(); setName(current);
      if (ok && v) { await saveP(v); await refresh(); setName(v); }
    };
    inp.addEventListener("click", (ev) => ev.stopPropagation());
    inp.addEventListener("keydown", (ev) => { if (ev.key === "Enter") { ev.preventDefault(); finish(true); } else if (ev.key === "Escape") finish(false); });
    inp.addEventListener("blur", () => finish(false));
  });
  del.addEventListener("click", async (e) => { e.stopPropagation(); if (current) { await delP(current); await refresh(); } });
  refresh();
}

// ---- L/R Decorrelate (native flag, not a relay) --------------------------
function decorrelateToggle(x, y, w, h) {
  const el = add("switch", x, y, w, h);
  const trk = document.createElement("div"); trk.className = "track"; const dot = document.createElement("div"); dot.className = "dot"; trk.append(dot);
  const lab = document.createElement("span"); lab.className = "swlabel"; lab.textContent = "L/R Decorr"; lab.style.color = "#c9a96e";
  el.append(trk, lab);
  const get = Juce.getNativeFunction("decorrelateGet"), set = Juce.getNativeFunction("decorrelateSet");
  let on = false;
  const refresh = () => el.classList.toggle("on", on);
  get().then((v) => { on = !!v; refresh(); }).catch(() => {});
  el.addEventListener("click", async () => { on = !on; refresh(); await set(on); });
  return el;
}

// ---- dimming (Visage setDimmed): grey + disable a set of controls when a toggle is off
function dimEls(els, dim) { els.forEach((e) => e && e.classList.toggle("dimmed", dim)); }
function wireEnableDim(toggleId, els) {
  const st = Juce.getToggleState(toggleId);
  const apply = () => dimEls(els, !st.getValue());
  st.valueChangedEvent.addListener(apply); apply();
}

// ---- assemble ------------------------------------------------------------
presetBar();
toggleSwitch("warm", 591, 107, 80, 24, "WARM");
shiftKnob(27, 168, 210, 218);

// spectral panel controls (refs captured so mode-dimming can grey + DISABLE them in Classic —
// they are siblings of `panel`, not children, so dimming the panel background alone does nothing)
const pianoEl     = piano(258, 185, 400, 42);
const quantSld    = slider("quantizeStrength", 322, 237, 334, 20, { decimals: 1 });
const preserveSld = slider("preserve", 322, 267, 334, 20, { decimals: 1 });
const transSld    = slider("transients", 322, 297, 130, 18, { decimals: 1 });
const sensSld     = slider("sensitivity", 516, 297, 140, 18, { decimals: 0 });
const peakSnapEl  = toggleSwitch("peakSnap", 258, 319, 170, 16, "Tones Only");
const noiseSld    = slider("noiseMix", 322, 340, 130, 16, { decimals: 0 });
const peakSensSld = slider("peakSens", 520, 340, 136, 16, { decimals: 0 });
const smearSld    = slider("smear", 322, 358, 343, 20, { decimals: 1, suffix: " ms" });
wireEnableDim("peakSnap", [noiseSld, peakSensSld]);   // Texture/Density active only with Tones Only

// Every spectral-only control (Classic mode uses the Hilbert path and none of these apply).
// Mode-dimming uses a SEPARATE class (.mode-off) from the enable-dim (.dimmed) so the two
// sources never fight over the same class on Texture/Density.
const spectralOnlyCtrls = [pianoEl, quantSld, preserveSld, transSld, sensSld,
                           peakSnapEl, noiseSld, peakSensSld, smearSld];

// freq modulation  (lfoRate -> tempo divisions when Sync on)
toggleSwitch("lfoEnabled", 119, 411, 34, 15, "");
const lfoDepthSld = slider("lfoDepth", 75, 431, 430, 22, { adaptive: true });
const lfoRateSld = slider("lfoRate", 75, 462, 356, 22, { adaptive: true, suffix: " Hz", log: true,
  sync: { toggleId: "lfoSync", divisionId: "lfoDivision", reversed: true } });
const lfoSyncTgl = toggleSwitch("lfoSync", 451, 462, 75, 24, "Sync");
const lfoShapeCmb = combo("lfoShape", 579, 463, 76, 22);
wireEnableDim("lfoEnabled", [lfoDepthSld, lfoRateSld, lfoSyncTgl, lfoShapeCmb, fmDepthLbl, fmRateLbl]);

// delay  (delayTime -> seconds above 1000 ms; tempo divisions when Sync on)
toggleSwitch("delayEnabled", 55, 523, 34, 15, "");
slider("delayTime", 158, 543, 346, 22, { decimals: 1, suffix: " ms", secondsAbove: 1000,
  sync: { toggleId: "delaySync", divisionId: "delayDivision", reversed: true } });
toggleSwitch("delaySync", 523, 543, 80, 24, "Sync");
slider("delayFeedback", 94, 577, 238, 22, { decimals: 1 });
slider("delayDamping", 416, 577, 260, 22, { decimals: 1 });
const slopeSlider = slider("delaySlope", 76, 611, 256, 22, { decimals: 1 });
decorrelateToggle(582, 631, 110, 18);

// delay modulation  (dlyLfoRate -> tempo divisions when Sync on)
toggleSwitch("dlyLfoEnabled", 120, 657, 34, 15, "");
const dlyDepthSld = slider("dlyLfoDepth", 76, 679, 448, 22, { adaptive: true, suffix: " ms" });
const dlyRateSld = slider("dlyLfoRate", 76, 709, 356, 22, { adaptive: true, suffix: " Hz", log: true,
  sync: { toggleId: "dlyLfoSync", divisionId: "dlyLfoDivision", reversed: true } });
const dlySyncTgl = toggleSwitch("dlyLfoSync", 452, 709, 75, 24, "Sync");
const dlyShapeCmb = combo("dlyLfoShape", 584, 707, 76, 22);
wireEnableDim("dlyLfoEnabled", [dlyDepthSld, dlyRateSld, dlySyncTgl, dlyShapeCmb, dmDepthLbl, dmRateLbl]);

// mask
const maskEnEl = toggleSwitch("maskEnabled", 56, 767, 34, 15, "");
const maskModeEl = combo("maskMode", 88, 784, 90, 22); maskModeEl.classList.add("tan");
const maskT = slider("maskTransition", 284, 787, 191, 22, { decimals: 2, suffix: " oct" });
const maskLo = slider("maskLowFreq", 62, 819, 301, 22, { decimals: 0 });
const maskHi = slider("maskHighFreq", 405, 819, 261, 22, { decimals: 0 });

// mix
slider("dryWet", 108, 879, 545, 22, { decimals: 1 });

// ---- mode dimming (Spectral-only controls greyed in Classic) -------------
function updateMode(idx) {
  const classic = (idx === 0);
  // spectral panel + its labels + mask + slope dim in Classic mode (mirrors Visage)
  panel.classList.toggle("dimmed", classic);
  spectralHdr.classList.toggle("dim", classic);
  specLabels.forEach((l) => l.classList.toggle("dim", classic));
  // The actual spectral controls sit ON the panel (not inside it), so grey + disable each one.
  spectralOnlyCtrls.forEach((e) => e && e.classList.toggle("mode-off", classic));
  // mask section
  maskStripEl.style.opacity = classic ? ".5" : "1";
  [maskEnEl, maskModeEl, maskT, maskLo, maskHi, ...maskLabels].forEach((e) => e && e.classList.toggle("dimmed", classic));
  // delay slope is spectral-only
  if (slopeSlider) slopeSlider.classList.toggle("dimmed", classic);
  if (slopeLbl) slopeLbl.classList.toggle("dim", classic);
}

// Mode selector created LAST so its initial dimming pass sees every control above.
segmented("processingMode", 28, 106, 220, 26, ["CLASSIC", "SPECTRAL"], updateMode);

// ---- resize grip (bottom-right) -----------------------------------------
// Editor-initiated resize so it works in EVERY format/host — AU included — where the
// host frame won't resize the window and the native web view hides JUCE's corner handle.
// Drag changes the editor width; height + clamping follow the 700:928 aspect natively.
function resizeGrip() {
  const g = add("resize-grip", W - 20, H - 20, 18, 18);
  g.style.zIndex = "60";
  const fn = Juce.getNativeFunction("resizeEditor");
  let dragging = false, startX = 0, startW = 0;
  g.addEventListener("pointerdown", (e) => { e.stopPropagation(); dragging = true; g.setPointerCapture(e.pointerId);
    startX = e.screenX; startW = window.innerWidth; });
  g.addEventListener("pointermove", (e) => { if (!dragging) return;
    const newW = Math.max(420, Math.min(1750, Math.round(startW + (e.screenX - startX))));
    try { fn(newW); } catch {} });
  const end = (e) => { dragging = false; try { g.releasePointerCapture(e.pointerId); } catch {} };
  g.addEventListener("pointerup", end); g.addEventListener("pointercancel", end);
}
resizeGrip();

nlog("UI assembled OK; stage children =", stage.childElementCount);
