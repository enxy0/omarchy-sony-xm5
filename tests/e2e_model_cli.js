// tests/e2e_model_cli.js
// End-to-end test: Model.js against the real sony-ctl and the real daemon
// running its simulated headsets (--mock). No Bluetooth. Status schema 3.
// Command: node tests/e2e_model_cli.js
// Env: DAEMON_BIN, CLI_BIN (default: build/...), MODEL_JS (default: Model.js
// of this repository), SANDBOX_PARENT (default: $XDG_RUNTIME_DIR; keep it
// short, a socket path has at most 107 bytes).
// Every run uses its own XDG_STATE_HOME, XDG_RUNTIME_DIR and HOME.
// Output: one "GROUP <id> PASS|FAIL <checks> <title>" line per group.

import fs from "fs";
import net from "net";
import os from "os";
import path from "path";
import { spawn, spawnSync } from "child_process";

const testsDir = path.dirname(decodeURIComponent(new URL(import.meta.url).pathname));
const repoRoot = path.resolve(testsDir, "..");
const DAEMON_BIN = process.env.DAEMON_BIN || path.join(repoRoot, "build/daemon/sony-headphones-daemon");
const CLI_BIN = process.env.CLI_BIN || path.join(repoRoot, "build/cli/sony-ctl");
const MODEL_JS = process.env.MODEL_JS || path.join(repoRoot, "Model.js");
const SANDBOX_PARENT = process.env.SANDBOX_PARENT || process.env.XDG_RUNTIME_DIR || os.tmpdir();

const WH_ID = "AC:80:0A:12:34:56";
const WF_ID = "AC:80:0A:65:43:21";

// Model.js is a QML JavaScript library; load it as tests/model.test.js does.
const modelSource = fs.readFileSync(MODEL_JS, "utf-8");
const names = [...modelSource.matchAll(/^(?:function|var)\s+([A-Za-z_$][\w$]*)/gm)].map((m) => m[1]);
const M = new Function(modelSource + "\n; return { " + names.join(", ") + " };")();

// Every status.json key Model.parseStatus reads.
const MODEL_KEYS = [...new Set([...modelSource.matchAll(/\bparsed\.([a-z_]+)/g)].map((m) => m[1]))].sort();

// Contract 3.2: the top-level keys.
const TOP_KEYS = ["schema_version", "daemon_version", "connected", "last_updated", "primary", "headsets"];
// Removed in schema 3; Model reads them only from schema 1 and 2 files.
const LEGACY_KEYS = ["charging", "ambient_level", "eq_bands", "dsee"];

// Contract 3.3 and 3.4: type, gating feature and the parsed field of each key
// of a headset entry.
const KEYS = {
  id: { type: "address", field: "id", nonNull: true },
  model: { type: "enum", values: ["wh-1000xm5", "wf-1000xm5", "other"], field: "model", nonNull: true },
  kind: { type: "enum", values: ["headphones", "earbuds"], field: "kind", nonNull: true },
  connected_since: { type: "int", field: "connectedSince", nonNull: true },
  device_name: { type: "string", field: "deviceName", nonNull: true },
  address: { type: "address", field: "address", nonNull: true },
  model_name: { type: "string", field: "modelName", nonNull: true },
  firmware_version: { type: "string", field: "firmwareVersion", nonNull: true },
  supported: { type: "supported", nonNull: true },
  supported_source: { type: "enum", values: ["device", "fallback"], field: "supportedSource", nonNull: true },
  battery_level: { type: "int", min: 0, max: 100, feature: "battery", field: "batteryLevel" },
  battery_charging: { type: "bool", feature: "battery", field: "batteryCharging" },
  battery_left: { type: "int", min: 0, max: 100, feature: "battery_lr", field: "batteryLeft" },
  battery_left_charging: { type: "bool", feature: "battery_lr", field: "batteryLeftCharging" },
  battery_right: { type: "int", min: 0, max: 100, feature: "battery_lr", field: "batteryRight" },
  battery_right_charging: { type: "bool", feature: "battery_lr", field: "batteryRightCharging" },
  battery_case: { type: "int", min: 0, max: 100, feature: "battery_case", field: "batteryCase" },
  battery_case_charging: { type: "bool", feature: "battery_case", field: "batteryCaseCharging" },
  noise_mode: { type: "enum", values: M.NOISE_MODES, feature: "noise_control", field: "noiseMode" },
  ambient_sound_level: { type: "int", min: M.AMBIENT_MIN, max: M.AMBIENT_MAX, feature: "noise_control", field: "ambientSoundLevel" },
  voice_passthrough: { type: "bool", feature: "noise_control", field: "focusOnVoice" },
  eq_preset: { type: "enum", values: M.EQ_PRESET_VALUES, feature: "eq_preset", field: "eqPreset" },
  eq_presets: { type: "enumList", values: M.EQ_PRESET_VALUES, feature: "eq_preset", field: "eqPresets" },
  eq_custom_bands: { type: "bands", feature: "eq_custom", field: "eqCustomBands" },
  clear_bass: { type: "int", min: M.EQ_LEVEL_MIN, max: M.EQ_LEVEL_MAX, feature: "eq_custom", field: "clearBass" },
  dsee_extreme: { type: "bool", feature: "dsee", field: "dseeExtreme" },
  connection_quality: { type: "enum", values: M.CONNECTION_QUALITY_OPTIONS, feature: "connection_quality", field: "connectionQuality" },
  codec: { type: "enum", values: M.CODECS.filter((c) => c !== "OTHER"), feature: "codec", field: "codec" },
  volume: { type: "int", min: M.VOLUME_MIN, max: M.VOLUME_MAX, feature: "volume", field: "volume" },
  speak_to_chat: { type: "bool", feature: "speak_to_chat", field: "speakToChat" },
  speak_to_chat_sensitivity: { type: "enum", values: M.STC_SENSITIVITY_OPTIONS, feature: "speak_to_chat", field: "stcSensitivity" },
  speak_to_chat_timeout: { type: "enum", values: M.STC_TIMEOUT_OPTIONS, feature: "speak_to_chat", field: "stcTimeout" },
  voice_guidance: { type: "bool", feature: "voice_guidance", field: "voiceGuidance" },
  voice_guidance_volume: { type: "int", min: M.GUIDANCE_VOLUME_MIN, max: M.GUIDANCE_VOLUME_MAX, feature: "voice_guidance_volume", field: "voiceGuidanceVolume" },
  auto_power_off: { type: "enum", values: M.AUTO_POWER_OFF_VALUES, feature: "auto_power_off", field: "autoPowerOff" },
  auto_power_off_options: { type: "enumList", values: M.AUTO_POWER_OFF_VALUES, feature: "auto_power_off", field: "autoPowerOffOptions" },
  nc_amb_button: { type: "enum", values: M.NC_AMB_BUTTON_OPTIONS, feature: "nc_amb_button", field: "ncAmbButton" },
  button_function: { type: "enum", values: M.BUTTON_FUNCTION_VALUES, feature: "button_function", field: "buttonFunction" },
  button_functions: { type: "enumList", values: M.BUTTON_FUNCTION_VALUES, feature: "button_function", field: "buttonFunctions" },
  touch_function_left: { type: "enum", values: M.TOUCH_FUNCTION_VALUES, feature: "touch_function", field: "touchFunctionLeft" },
  touch_function_right: { type: "enum", values: M.TOUCH_FUNCTION_VALUES, feature: "touch_function", field: "touchFunctionRight" },
  touch_function_left_options: { type: "enumList", values: M.TOUCH_FUNCTION_VALUES, feature: "touch_function", field: "touchFunctionLeftOptions" },
  touch_function_right_options: { type: "enumList", values: M.TOUCH_FUNCTION_VALUES, feature: "touch_function", field: "touchFunctionRightOptions" },
  ear_detection: { type: "bool", feature: "ear_detection", field: "earDetection" },
  worn: { type: "bool", feature: "worn", field: "worn" },
  worn_left: { type: "bool", feature: "worn", field: "wornLeft", earbudsOnly: true },
  worn_right: { type: "bool", feature: "worn", field: "wornRight", earbudsOnly: true },
  multipoint: { type: "bool", feature: "multipoint", field: "multipoint" },
  devices: { type: "devices", feature: "devices", field: "devices" },
  touch_panel: { type: "bool", feature: "touch_panel", field: "touchPanel" },
  sidetone: { type: "bool", feature: "sidetone", field: "sidetone" },
  quick_access_double_tap: { type: "enum", values: M.QUICK_ACCESS_VALUES, feature: "quick_access", field: "quickAccessDoubleTap" },
  quick_access_triple_tap: { type: "enum", values: M.QUICK_ACCESS_VALUES, feature: "quick_access", field: "quickAccessTripleTap" },
  quick_access_double_tap_options: { type: "enumList", values: M.QUICK_ACCESS_VALUES, feature: "quick_access", field: "quickAccessDoubleTapOptions" },
  quick_access_triple_tap_options: { type: "enumList", values: M.QUICK_ACCESS_VALUES, feature: "quick_access", field: "quickAccessTripleTapOptions" }
};

// ---------------------------------------------------------------------------
// Checks and groups
// ---------------------------------------------------------------------------

const groups = [];
let group = null;

function startGroup(id, title) {
  group = { id, title, passed: 0, failed: 0 };
  groups.push(group);
  console.log(`\n[${id}] ${title}`);
}

function ok(cond, name, detail) {
  if (cond) {
    group.passed++;
    return true;
  }
  group.failed++;
  console.log(`  ✗ ${name}`);
  if (detail !== undefined) console.log(`    ${typeof detail === "string" ? detail : JSON.stringify(detail)}`);
  return false;
}

function same(a, b) {
  return JSON.stringify(a) === JSON.stringify(b);
}

function eq(actual, expected, name) {
  return ok(same(actual, expected), name, { expected, actual });
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// ---------------------------------------------------------------------------
// Daemon and CLI
// ---------------------------------------------------------------------------

const running = new Set();
process.on("exit", () => {
  for (const d of running) {
    try { d.child.kill("SIGKILL"); } catch (_e) { /* already gone */ }
    fs.rmSync(d.dir, { recursive: true, force: true });
  }
});

function unreadFields(h) {
  return Object.keys(M.FIELD_FEATURES).filter((f) =>
    h.supported[M.FIELD_FEATURES[f]] === true && (h[f] === null || h[f] === M.LEVEL_UNKNOWN) &&
    !((f === "wornLeft" || f === "wornRight") && h.kind !== "earbuds"));
}

// Every headset has its support list and every supported field a value.
function settled(s, count) {
  return M.isLive(s) && s.headsets.length === count &&
    s.headsets.every((h) => h.supported !== null && unreadFields(h).length === 0);
}

async function startDaemon(profile, count = 1) {
  const dir = fs.mkdtempSync(path.join(SANDBOX_PARENT, "e2e-"));
  const env = {
    ...process.env,
    HOME: dir,
    XDG_STATE_HOME: path.join(dir, "state"),
    XDG_RUNTIME_DIR: path.join(dir, "run"),
    XDG_CONFIG_HOME: path.join(dir, "config")
  };
  // No --state-dir/--runtime-dir: the daemon resolves the paths from the
  // environment, as under systemd.
  const child = spawn(DAEMON_BIN, ["--mock", "--mock-profile", profile], { env, stdio: ["ignore", "pipe", "pipe"] });
  const d = {
    profile, dir, env, child, log: "", exited: false,
    statusFile: path.join(env.XDG_STATE_HOME, "sony-headphones/status.json"),
    socket: path.join(env.XDG_RUNTIME_DIR, "sony-headphones.sock")
  };
  child.stdout.on("data", (b) => { d.log += b; });
  child.stderr.on("data", (b) => { d.log += b; });
  d.exit = new Promise((resolve) => child.on("exit", () => { d.exited = true; resolve(); }));
  running.add(d);
  for (let i = 0; i < 100 && !d.log.includes("Ready"); i++) {
    if (d.exited) break;
    await sleep(50);
  }
  if (!d.log.includes("Ready")) throw new Error(`daemon (${profile}) did not start:\n${d.log}`);
  await waitStatus(d, (s) => settled(s, count), 5000, "initial read");
  return d;
}

async function stopDaemon(d) {
  d.child.kill("SIGTERM");
  await d.exit;
  running.delete(d);
  fs.rmSync(d.dir, { recursive: true, force: true });
}

function readStatus(d) {
  let text = "";
  try { text = fs.readFileSync(d.statusFile, "utf-8"); } catch (_e) { /* not written yet */ }
  let raw = null;
  try { raw = JSON.parse(text); } catch (_e) { /* parseStatus reports it */ }
  return { raw, parsed: M.parseStatus(text) };
}

// The Model view of one headset (the primary one when `id` is null).
function view(d, id) {
  const parsed = readStatus(d).parsed;
  return id ? M.shownHeadset(parsed, id) : parsed;
}

async function waitStatus(d, pred, timeoutMs, what) {
  const until = Date.now() + timeoutMs;
  for (;;) {
    const st = readStatus(d);
    if (pred(st.parsed)) return st;
    if (Date.now() > until) throw new Error(`timeout waiting for ${what}; status: ${JSON.stringify(st.raw)}`);
    await sleep(20);
  }
}

// Runs sony-ctl as Service.qml does: the bare binary plus the Model argv,
// socket from $XDG_RUNTIME_DIR.
function cli(d, args) {
  const r = spawnSync(CLI_BIN, args, { env: d.env, encoding: "utf-8", timeout: 5000 });
  return { code: r.status, stdout: (r.stdout || "").trim(), stderr: (r.stderr || "").trim() };
}

// One raw request line on the daemon socket (sony-ctl refuses the test verbs).
function rawRequest(d, line) {
  return new Promise((resolve, reject) => {
    const sock = net.createConnection(d.socket);
    let out = "";
    sock.on("connect", () => sock.write(line + "\n"));
    sock.on("data", (b) => {
      out += b;
      if (out.includes("\n")) {
        sock.end();
        resolve(out.trim());
      }
    });
    sock.on("error", reject);
  });
}

// ---------------------------------------------------------------------------
// Reverse direction: daemon status.json against Model.parseStatus
// ---------------------------------------------------------------------------

function typeOk(spec, v) {
  switch (spec.type) {
  case "int": return Number.isInteger(v) && (spec.min === undefined || (v >= spec.min && v <= spec.max));
  case "bool": return typeof v === "boolean";
  case "string": return typeof v === "string";
  case "address": return typeof v === "string" && /^[0-9A-F]{2}(:[0-9A-F]{2}){5}$/.test(v);
  case "enum": return typeof v === "string" && spec.values.includes(v);
  case "enumList": return Array.isArray(v) && v.length > 0 && v.every((x) => spec.values.includes(x));
  case "bands": return Array.isArray(v) && v.length === 5 && v.every((x) => Number.isInteger(x) && x >= M.EQ_LEVEL_MIN && x <= M.EQ_LEVEL_MAX);
  case "supported": return v && typeof v === "object" && !Array.isArray(v) &&
      same(Object.keys(v).sort(), M.FEATURE_KEYS.slice().sort()) &&
      Object.values(v).every((x) => typeof x === "boolean");
  case "devices": return Array.isArray(v) && v.every((x) => x && typeof x === "object" &&
      /^[0-9A-F]{2}(:[0-9A-F]{2}){5}$/.test(x.address) && typeof x.name === "string" &&
      typeof x.connected === "boolean" && typeof x.active === "boolean" && (!x.active || x.connected)) &&
      v.filter((x) => x.active).length <= 1;
  default: return false;
  }
}

// Checks one headset entry against the contract and Model's view of it.
function checkEntry(entry, parsed, where) {
  const v = M.shownHeadset(parsed, entry.id);
  eq(v.id, entry.id, `${where}: Model shows headset ${entry.id}`);
  ok(Object.keys(entry).every((k) => k in KEYS), `${where}: no unknown or legacy key in the entry`,
    Object.keys(entry).filter((k) => !(k in KEYS)));
  for (const key of Object.keys(KEYS)) {
    const spec = KEYS[key];
    if (!ok(key in entry, `${where}: daemon writes '${key}'`)) continue;
    const value = entry[key];
    const gated = spec.feature && entry.supported && entry.supported[spec.feature] !== true;
    if (gated || (spec.earbudsOnly && entry.kind !== "earbuds")) {
      eq(value, null, `${where}: '${key}' is null`);
      if (spec.field) eq(v[spec.field], spec.field === "batteryLevel" ? M.LEVEL_UNKNOWN : null, `${where}: Model hides '${spec.field}'`);
      continue;
    }
    // The simulated headsets answer every request, so nothing stays unknown.
    if (!ok(value !== null, `${where}: '${key}' is not null`)) continue;
    ok(typeOk(spec, value), `${where}: '${key}' has type ${spec.type}`, value);
    // Lossless parse: no clamp, no "unknown"/"other" fallback, no dropped value.
    if (spec.field) eq(v[spec.field], value, `${where}: Model reads '${key}' unchanged`);
  }
  eq(v.supported, entry.supported, `${where}: Model reads 'supported' unchanged`);
  ok(v.eqPreset !== "unknown" && v.connectionQuality !== "other" && v.buttonFunction !== "other" &&
    v.codec !== "OTHER" && v.quickAccessDoubleTap !== "other" && v.quickAccessTripleTap !== "other" &&
    v.touchFunctionLeft !== "other" && v.touchFunctionRight !== "other", `${where}: no unknown-value fallback`);
}

// Checks one daemon-written status against the contract and Model's parse.
function checkSchema(raw, parsed, where) {
  const known = [...Object.keys(KEYS), ...TOP_KEYS, ...LEGACY_KEYS];
  ok(MODEL_KEYS.every((k) => known.includes(k)), `${where}: the test knows every key Model reads`,
    MODEL_KEYS.filter((k) => !known.includes(k)));
  ok(!parsed.schemaTooNew && parsed.lastError === "" && parsed.ok === true, `${where}: parse ok, no schemaTooNew`, parsed.lastError);
  if (!ok(raw && typeof raw === "object", `${where}: status.json is an object`)) return;
  eq(Object.keys(raw), TOP_KEYS, `${where}: the six top-level keys in order`);
  eq(raw.schema_version, 3, `${where}: schema_version 3`);
  const list = Array.isArray(raw.headsets) ? raw.headsets : [];
  eq(raw.connected, list.length > 0, `${where}: connected == headsets not empty`);
  eq(parsed.headsets.map((h) => h.id), list.map((h) => h.id), `${where}: Model reads every entry`);
  if (list.length === 0) {
    eq(raw.primary, null, `${where}: no primary without headsets`);
    eq(parsed.connected, false, `${where}: parsed as disconnected`);
    return;
  }
  ok(list.some((h) => h.id === raw.primary), `${where}: primary names an entry`, raw.primary);
  eq(parsed.id, raw.primary, `${where}: Model's default view is the primary headset`);
  const order = list.slice().sort((a, b) =>
    (a.kind === "headphones" ? 0 : 1) - (b.kind === "headphones" ? 0 : 1) || (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));
  eq(list.map((h) => h.id), order.map((h) => h.id), `${where}: headphones first, then id order`);
  for (const entry of list) checkEntry(entry, parsed, `${where} ${entry.model}`);
}

// ---------------------------------------------------------------------------
// Forward direction: Model builds the command, the real CLI runs it
// ---------------------------------------------------------------------------

const samples = {};
let commandCount = 0;

// Sends one UI action for headset `id` (null: the primary headset) and waits
// until the daemon reports every optimistic value. Returns the settled view.
async function send(d, action, value, label, id = null) {
  const before = view(d, id);
  const built = M.buildCommand(action, value, before);
  if (!ok(!built.error, `${label}: Model builds a command`, built.error)) return before;
  eq(built.args.slice(0, 2), ["--headset", before.id], `${label}: the command names its headset`);
  const pending = M.addPending({}, built.pending, 1, built.headset);
  const optimistic = M.applyPending(before, pending);
  const r = cli(d, built.args);
  commandCount++;
  const okReply = ok(r.code === 0 && r.stdout === "OK", `${label}: sony-ctl ${built.args.join(" ")} -> OK`, r);
  const verb = M.commandVerbArgs(built.args);
  if (!samples[action] && !(verb[0] === "eq" && verb[1] === "custom" && action === "eqPreset")) {
    samples[action] = { args: verb, value, feature: built.feature };
  }
  if (!okReply) return before;
  let st;
  try {
    st = await waitStatus(d, (s) => !M.hasPending(M.reconcilePending(pending, s)), 3000, `${label} to settle`);
  } catch (e) {
    ok(false, `${label}: status.json reflects ${JSON.stringify(built.pending)}`, e.message);
    return view(d, before.id);
  }
  const after = M.shownHeadset(st.parsed, before.id);
  for (const f of Object.keys(built.pending)) eq(after[f], optimistic[f], `${label}: optimistic ${f} == settled`);
  checkSchema(st.raw, st.parsed, label);
  console.log(`  ✓ ${label}: ${built.args.join(" ")}`);
  return after;
}

function allExpanded() {
  const out = {};
  for (const id of M.TARGET_ORDER) if (M.TARGETS[id].kind === "collapse") out[M.TARGETS[id].section] = true;
  return out;
}

const ADJUST_RANGES = {
  ambient: [M.AMBIENT_MIN, M.AMBIENT_MAX],
  volume: [M.VOLUME_MIN, M.VOLUME_MAX],
  voiceGuidanceVolume: [M.GUIDANCE_VOLUME_MIN, M.GUIDANCE_VOLUME_MAX]
};

// `args` builds the sony-ctl verb argv for a value; `refusal` is the ERR subject.
const LIST_TARGETS = {
  eqPreset: { args: (v) => ["eq", v], values: M.EQ_PRESET_VALUES },
  autoPowerOff: { args: (v) => ["auto-power-off", v], values: M.AUTO_POWER_OFF_VALUES },
  buttonFunction: { args: (v) => ["button-function", v], values: M.BUTTON_FUNCTION_VALUES },
  quickAccessDouble: { args: (v) => ["quick-access", "double", v], refusal: (v) => `quick_access double ${v}`, values: M.QUICK_ACCESS_VALUES },
  quickAccessTriple: { args: (v) => ["quick-access", "triple", v], refusal: (v) => `quick_access triple ${v}`, values: M.QUICK_ACCESS_VALUES },
  touchFunctionLeft: { args: (v) => ["touch-function", "left", v], refusal: (v) => `touch_function left ${v}`, values: M.TOUCH_FUNCTION_VALUES },
  touchFunctionRight: { args: (v) => ["touch-function", "right", v], refusal: (v) => `touch_function right ${v}`, values: M.TOUCH_FUNCTION_VALUES }
};

function rangeValues(min, max) {
  return [min, max, Math.round((min + max) / 2)];
}

// Contract 3.4: a target is shown exactly when its feature is supported.
function checkVisibility(s, where) {
  for (const id of M.TARGET_ORDER) {
    const t = M.TARGETS[id];
    const supported = t.kind === "collapse" ? M.sectionVisible(s, t.section) : s.supported[t.feature] === true;
    eq(M.targetVisible(s, id), supported, `${where}: target '${id}' visible only when supported`);
  }
}

async function exerciseTarget(d, id, headset = null) {
  const t = M.TARGETS[id];
  let s = view(d, headset);
  if (t.kind === "display") return;
  if (t.kind === "collapse") {
    eq(M.targetState(s, id), "enabled", `${id}: section header shown`);
    return;
  }
  if (!eq(M.targetState(s, id), "enabled", `${id}: target enabled`)) return;
  const hid = s.id;

  if (t.toggle) {
    for (let i = 0; i < 2; i++) {
      const act = M.targetActivation(s, id, 0);
      if (!ok(act !== null, `${id}: toggle activates`)) return;
      s = await send(d, act.action, act.value, `${id}=${act.value}`, hid);
    }
    return;
  }

  if (t.kind === "walk" || t.kind === "grid") {
    const count = M.targetOptions(s, id).length;
    for (let i = 0; i < count; i++) {
      const options = M.targetOptions(s, id);
      const act = M.targetActivation(s, id, i);
      if (!ok(act !== null && act.value === options[i], `${id}[${i}]: chip activates`, act)) continue;
      s = await send(d, act.action, act.value, `${id}=${act.value}`, hid);
      const chip = M.targetChoices(s, id).find((c) => c.value === s[t.field]);
      ok(chip && chip.label !== "Unknown", `${id}=${act.value}: settled value has a chip label`, s[t.field]);
      const section = M.cursorSections(s, allExpanded()).find((x) => x.id === id);
      eq(section ? M.targetOptions(s, id)[section.preferred] : null, act.value, `${id}=${act.value}: cursor marks the chip`);
    }
    // Contract values the headset list leaves out: no chip, and the daemon
    // refuses them (contract 4.1 step 7).
    const list = LIST_TARGETS[id];
    if (list) {
      for (const v of list.values.filter((x) => !M.targetOptions(s, id).includes(x))) {
        eq(M.buildCommand(t.action, v, s), { error: "Not supported by this headset" }, `${id}=${v}: Model refuses a value outside the list`);
        const args = ["--headset", hid, ...list.args(v)];
        const r = cli(d, args);
        commandCount++;
        const subject = list.refusal ? list.refusal(v) : `${t.feature} ${v}`;
        eq([r.code, r.stderr], [1, `Error from daemon: ERR unsupported ${subject}`], `${id}=${v}: sony-ctl ${args.join(" ")} -> ERR`);
      }
    }
    return;
  }

  if (t.kind === "adjust") {
    const [min, max] = ADJUST_RANGES[id];
    for (const v of rangeValues(min, max)) s = await send(d, t.action, v, `${id}=${v}`, hid);
    const step = M.targetAdjustment(s, id, 0, -1);
    if (ok(step !== null, `${id}: keyboard step adjusts`)) s = await send(d, step.action, step.value, `${id} step=${step.value}`, hid);
    return;
  }

  if (id === "eqBands") {
    for (let index = 0; index < 6; index++) {
      for (const level of rangeValues(M.EQ_LEVEL_MIN, M.EQ_LEVEL_MAX)) {
        s = await send(d, t.action, { index, level }, `eqBands[${index}]=${level}`, hid);
      }
      const step = M.targetAdjustment(s, id, index, 1);
      if (ok(step !== null, `eqBands[${index}]: keyboard step adjusts`)) {
        s = await send(d, step.action, step.value, `eqBands[${index}] step=${step.value.level}`, hid);
      }
    }
    return;
  }

  if (id === "devices") {
    const devices = s.devices.slice();
    for (let i = 0; i < devices.length; i++) {
      s = view(d, hid);
      const dev = s.devices[i];
      const act = M.targetActivation(s, id, i);
      if (dev.active || !dev.connected) {
        eq(act, null, `devices[${i}] (${M.deviceStateLabel(dev)}): no command`);
        if (!dev.connected) {
          eq(M.buildCommand("source", dev.address, s), { error: "Device is not connected" }, `devices[${i}]: Model refuses a disconnected device`);
          const r = cli(d, ["--headset", hid, "source", dev.address]);
          eq([r.code, r.stderr], [1, `Error from daemon: ERR unsupported source_switch ${dev.address}`], `devices[${i}]: daemon refuses a disconnected device`);
        }
        continue;
      }
      if (!ok(act !== null && act.action === "source", `devices[${i}]: switch activates`, act)) continue;
      s = await send(d, act.action, act.value, `devices[${i}] source`, hid);
      eq(s.devices[i].active, true, `devices[${i}]: now active`);
    }
    return;
  }

  ok(false, `${id}: the test does not know this target kind (${t.kind})`);
}

// Every visible target of one headset through the real CLI.
async function exerciseVisible(d, headset = null) {
  for (const id of M.TARGET_ORDER) {
    if (M.targetVisible(view(d, headset), id)) await exerciseTarget(d, id, headset);
  }
}

// ---------------------------------------------------------------------------
// Runs
// ---------------------------------------------------------------------------

async function runGroup(id, title, fn) {
  startGroup(id, title);
  try {
    await fn();
  } catch (e) {
    ok(false, "unexpected error", e.stack || String(e));
  }
}

for (const bin of [DAEMON_BIN, CLI_BIN]) {
  try {
    fs.accessSync(bin, fs.constants.X_OK);
  } catch (_e) {
    console.error(`Missing binary ${bin}; build the project first (cmake --build build)`);
    process.exit(1);
  }
}
console.log("=== Model.js <-> sony-ctl <-> sony-headphones-daemon --mock (schema 3) ===");
console.log(`daemon: ${DAEMON_BIN}\ncli:    ${CLI_BIN}\nmodel:  ${MODEL_JS}\nsandbox parent: ${SANDBOX_PARENT}`);

let full = null;
try {
  await runGroup("E1", "Daemon status.json matches what Model.js reads (full WH-1000XM5)", async () => {
    full = await startDaemon("xm5");
    const st = readStatus(full);
    checkSchema(st.raw, st.parsed, "xm5 initial");
    const entryKeys = st.raw.headsets.length ? Object.keys(st.raw.headsets[0]) : [];
    const missing = MODEL_KEYS.filter((k) => !TOP_KEYS.includes(k) && !LEGACY_KEYS.includes(k) && !entryKeys.includes(k));
    eq(missing, [], "every key Model reads is in the daemon output");
    const viaCli = cli(full, ["status"]);
    let cliRaw = null;
    try { cliRaw = JSON.parse(viaCli.stdout); } catch (_e) { /* checked below */ }
    ok(viaCli.code === 0 && cliRaw !== null, "sony-ctl status prints the status object", viaCli);
    if (cliRaw) {
      const strip = (o) => { const c = { ...o }; delete c.last_updated; return c; };
      eq(strip(cliRaw), strip(st.raw), "sony-ctl status equals status.json");
    }
    checkVisibility(st.parsed, "xm5");
    full.child.kill("SIGUSR1");
    const off = await waitStatus(full, (s) => s.ok && !s.connected, 3000, "disconnect");
    checkSchema(off.raw, off.parsed, "xm5 disconnected");
    eq(M.cursorSections(off.parsed, allExpanded()), [], "disconnected: no cursor targets");
    full.child.kill("SIGUSR2");
    await waitStatus(full, (s) => settled(s, 1), 5000, "reconnect");
  });

  await runGroup("E2", "Every target, option and slider value through the real CLI (full WH-1000XM5)", async () => {
    if (!full) throw new Error("no daemon");
    await exerciseVisible(full);

    // quick-access sets one press; the other keeps its function (contract 4.3).
    for (const [press, other] of [["double", "triple"], ["triple", "double"]]) {
      let s = view(full);
      const field = press === "double" ? "quickAccessDoubleTap" : "quickAccessTripleTap";
      const otherField = other === "double" ? "quickAccessDoubleTap" : "quickAccessTripleTap";
      const kept = s[otherField];
      const next = M.quickAccessOptions(s, press).find((v) => v !== s[field]);
      if (!ok(next !== undefined, `quick access ${press}: a second value exists`)) continue;
      const action = press === "double" ? "quickAccessDouble" : "quickAccessTriple";
      s = await send(full, action, next, `quick access ${press}=${next}`);
      eq(s[otherField], kept, `quick access ${press}=${next}: ${other} tap keeps ${kept}`);
    }

    // Argument errors: the CLI refuses them before it connects.
    const usage = {
      "touch-panel": "touch-panel on|off",
      "quick-access": "quick-access double|triple <value>",
      "touch-function": "touch-function left|right <value>",
      "sidetone": "sidetone on|off"
    };
    for (const args of [["touch-panel", "maybe"], ["touch-panel"], ["quick-access", "double"],
      ["quick-access", "quad", "off"], ["quick-access", "double", "other"], ["touch-function", "left"],
      ["touch-function", "middle", "none"], ["touch-function", "left", "other"], ["sidetone", "maybe"]]) {
      const r = cli(full, args);
      commandCount++;
      eq([r.code, r.stdout, r.stderr], [1, "", `Error: usage: ${usage[args[0]]}`], `sony-ctl ${args.join(" ")} -> usage error`);
    }
    const st = cli(full, ["--headset", WH_ID, "status"]);
    eq([st.code, st.stderr], [1, "Error: 'status' does not take --headset"], "sony-ctl --headset <id> status is refused");
    const none = cli(full, ["--headset", WF_ID, "volume", "3"]);
    eq([none.code, none.stderr], [1, `Error from daemon: ERR no headset ${WF_ID}`], "a headset that is not connected is refused");
  });
} finally {
  if (full) await stopDaemon(full);
}

let mp = null;
try {
  await runGroup("E3", "Source switching (multipoint, two connected sources)", async () => {
    mp = await startDaemon("multipoint");
    const st = readStatus(mp);
    checkSchema(st.raw, st.parsed, "multipoint initial");
    eq(st.parsed.devices.map((x) => [x.connected, x.active]), [[true, true], [true, false], [false, false]], "three devices: active, connected, paired");
    await exerciseTarget(mp, "devices");
    await exerciseTarget(mp, "devices");
    await exerciseTarget(mp, "multipoint");
  });
} finally {
  if (mp) await stopDaemon(mp);
}

let wf = null;
try {
  await runGroup("E5", "Earbuds: every WF-1000XM5 target through the real CLI", async () => {
    wf = await startDaemon("wf");
    const st = readStatus(wf);
    checkSchema(st.raw, st.parsed, "wf initial");
    eq([st.parsed.id, st.parsed.kind, st.parsed.model], [WF_ID, "earbuds", "wf-1000xm5"], "the earbuds are the primary view");
    checkVisibility(st.parsed, "wf");
    for (const id of ["batteryLr", "batteryCase", "wornSides", "touchFunctionLeft", "touchFunctionRight", "sidetone"]) {
      ok(M.targetVisible(st.parsed, id), `wf: earbud target '${id}' shown`);
    }
    for (const id of ["battery", "ncAmbButton", "buttonFunction", "touchPanel", "quickAccessDouble", "connectionQuality"]) {
      ok(!M.targetVisible(st.parsed, id), `wf: target '${id}' hidden`);
    }
    await exerciseVisible(wf);

    // touch-function sets one side; the other keeps its preset (contract 4.3).
    for (const [side, other] of [["Left", "Right"], ["Right", "Left"]]) {
      let s = view(wf);
      const kept = s[`touchFunction${other}`];
      const next = M.touchFunctionOptions(s, side.toLowerCase()).find((v) => v !== s[`touchFunction${side}`]);
      if (!ok(next !== undefined, `touch ${side}: a second value exists`)) continue;
      s = await send(wf, `touchFunction${side}`, next, `touch ${side}=${next}`);
      eq(s[`touchFunction${other}`], kept, `touch ${side}=${next}: ${other.toLowerCase()} keeps ${kept}`);
    }
  });
} finally {
  if (wf) await stopDaemon(wf);
}

let both = null;
try {
  await runGroup("E6", "Two headsets: views, routing by --headset, primary, detach and attach", async () => {
    both = await startDaemon("both", 2);
    let st = readStatus(both);
    checkSchema(st.raw, st.parsed, "both initial");
    eq(st.raw.headsets.map((h) => h.id), [WH_ID, WF_ID], "headphones first");
    eq(st.raw.primary, WF_ID, "the earbuds play from this computer and are primary");
    const wh = M.shownHeadset(st.parsed, WH_ID);
    eq([wh.id, wh.kind], [WH_ID, "headphones"], "shownHeadset gives the non-primary headset");
    eq(M.shownHeadset(st.parsed, "11:22:33:44:55:66").id, WF_ID, "an unknown selection shows the primary headset");
    checkVisibility(wh, "both WH");
    checkVisibility(st.parsed, "both WF");

    const wfVolume = st.parsed.volume;
    let s = await send(both, "volume", 7, "WH volume=7", WH_ID);
    eq(s.volume, 7, "WH volume changed");
    eq(view(both, WF_ID).volume, wfVolume, "WF volume kept");
    const whLevel = view(both, WH_ID).ambientSoundLevel;
    s = await send(both, "ambientLevel", 3, "WF ambient=3", WF_ID);
    eq(view(both, WH_ID).ambientSoundLevel, whLevel, "WH ambient level kept");
    s = await send(both, "touchFunctionRight", "none", "WF touch right=none", WF_ID);
    s = await send(both, "sidetone", false, "WF sidetone=off", WF_ID);
    eq(M.buildCommand("touchFunctionLeft", "none", view(both, WH_ID)), { error: "Not supported by this headset" }, "Model refuses touch-function on the WH");
    const r = cli(both, ["--headset", WH_ID, "touch-function", "left", "none"]);
    eq([r.code, r.stderr], [1, "Error from daemon: ERR unsupported touch_function"], "daemon refuses touch-function on the WH");
    const amb = cli(both, ["--headset", "wf-1000xm5", "volume", "4"]);
    eq([amb.code, amb.stdout], [0, "OK"], "a model selector works");

    eq(await rawRequest(both, `@${WF_ID} _disconnect`), "OK", "the WF leaves");
    st = await waitStatus(both, (x) => x.ok && x.headsets.length === 1, 3000, "WF detach");
    checkSchema(st.raw, st.parsed, "both after detach");
    eq(st.raw.primary, WH_ID, "the WH becomes primary");
    eq(M.shownHeadset(st.parsed, WF_ID).id, WH_ID, "the popup shows the primary headset while the WF is gone");
    const gone = cli(both, ["--headset", WF_ID, "volume", "4"]);
    eq([gone.code, gone.stderr], [1, `Error from daemon: ERR no headset ${WF_ID}`], "commands for the gone headset are refused");
    eq(await rawRequest(both, `@${WF_ID} _reconnect`), "OK", "the WF comes back");
    st = await waitStatus(both, (x) => settled(x, 2), 5000, "WF attach");
    checkSchema(st.raw, st.parsed, "both after attach");
    eq(st.raw.primary, WF_ID, "the WF is primary again");
  });
} finally {
  if (both) await stopDaemon(both);
}

let min = null;
try {
  await runGroup("E4", "Gating: reduced support list hides targets, daemon refuses their verbs", async () => {
    min = await startDaemon("minimal");
    const st = readStatus(min);
    const s = st.parsed;
    checkSchema(st.raw, s, "minimal initial");
    eq(Object.keys(s.supported).filter((k) => s.supported[k]), ["battery", "noise_control"], "minimal profile advertises battery and noise control");
    const cursorIds = M.cursorSections(s, allExpanded()).map((x) => x.id);
    eq(cursorIds, ["noise", "ambient", "focusVoice"], "cursor walks only the supported targets");
    for (const id of M.TARGET_ORDER) {
      const t = M.TARGETS[id];
      const visible = M.targetVisible(s, id);
      const supported = t.kind === "collapse" ? M.sectionVisible(s, t.section) : s.supported[t.feature] === true;
      eq(visible, supported, `${id}: visible only when supported`);
      if (visible) continue;
      eq(M.targetActivation(s, id, 0), null, `${id}: hidden target activates nothing`);
      eq([M.targetAdjustment(s, id, 0, 1), M.targetAdjustment(s, id, 0, -1)], [null, null], `${id}: hidden target adjusts nothing`);
      if (t.kind === "collapse" || !t.action) continue;
      const sample = samples[t.action];
      if (!ok(sample, `${id}: a command from the earlier runs exists`)) continue;
      eq(M.buildCommand(t.action, sample.value, s), { error: "Not supported by this headset" }, `${id}: Model refuses ${t.action}`);
      const args = ["--headset", s.id, ...sample.args];
      const r = cli(min, args);
      commandCount++;
      eq([r.code, r.stdout, r.stderr], [1, "", `Error from daemon: ERR unsupported ${sample.feature}`], `${id}: sony-ctl ${args.join(" ")} -> ERR`);
    }
    for (const id of cursorIds) await exerciseTarget(min, id);
  });
} finally {
  if (min) await stopDaemon(min);
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

console.log("");
let failed = 0;
let checks = 0;
for (const g of groups) {
  checks += g.passed + g.failed;
  failed += g.failed;
  console.log(`GROUP ${g.id} ${g.failed === 0 ? "PASS" : "FAIL"} ${g.passed}/${g.passed + g.failed} ${g.title}`);
}
console.log(`\nCommands run through sony-ctl: ${commandCount}`);
console.log(`Checks: ${checks - failed} passed, ${failed} failed`);
process.exit(failed === 0 && groups.length === 6 ? 0 : 1);
