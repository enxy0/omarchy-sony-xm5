// tests/e2e_model_cli.js
// End-to-end test: Model.js against the real sony-ctl and the real daemon
// running its simulated headset (--mock). No Bluetooth.
// Command: node tests/e2e_model_cli.js
// Env: DAEMON_BIN, CLI_BIN (default: build/...), SANDBOX_PARENT (default:
// $XDG_RUNTIME_DIR; keep it short, a socket path has at most 107 bytes).
// Every run uses its own XDG_STATE_HOME, XDG_RUNTIME_DIR and HOME.
// Output: one "GROUP <id> PASS|FAIL <checks> <title>" line per group.

import fs from "fs";
import os from "os";
import path from "path";
import { spawn, spawnSync } from "child_process";

const testsDir = path.dirname(decodeURIComponent(new URL(import.meta.url).pathname));
const repoRoot = path.resolve(testsDir, "..");
const DAEMON_BIN = process.env.DAEMON_BIN || path.join(repoRoot, "build/daemon/sony-headphones-daemon");
const CLI_BIN = process.env.CLI_BIN || path.join(repoRoot, "build/cli/sony-ctl");
const SANDBOX_PARENT = process.env.SANDBOX_PARENT || process.env.XDG_RUNTIME_DIR || os.tmpdir();

// Model.js is a QML JavaScript library; load it as tests/model.test.js does.
const modelSource = fs.readFileSync(path.join(repoRoot, "Model.js"), "utf-8");
const names = [...modelSource.matchAll(/^(?:function|var)\s+([A-Za-z_$][\w$]*)/gm)].map((m) => m[1]);
const M = new Function(modelSource + "\n; return { " + names.join(", ") + " };")();

// Every status.json key Model.parseStatus reads.
const MODEL_KEYS = [...new Set([...modelSource.matchAll(/\bparsed\.([a-z_]+)/g)].map((m) => m[1]))].sort();

// Contract 2.2 to 2.4: type, gating feature and the parsed field of each key.
// `alias` names the canonical key with the same value.
const KEYS = {
  schema_version: { type: "int", always: true },
  connected: { type: "bool", always: true },
  last_updated: { type: "int", always: true },
  device_name: { type: "string", field: "deviceName", nonNull: true },
  address: { type: "address", field: "address", nonNull: true },
  model_name: { type: "string", field: "modelName", nonNull: true },
  firmware_version: { type: "string", field: "firmwareVersion", nonNull: true },
  supported: { type: "supported", nonNull: true },
  supported_source: { type: "enum", values: ["device", "fallback"], field: "supportedSource", nonNull: true },
  battery_level: { type: "int", min: 0, max: 100, feature: "battery", field: "batteryLevel" },
  battery_charging: { type: "bool", feature: "battery", field: "batteryCharging" },
  charging: { type: "bool", feature: "battery", alias: "battery_charging" },
  noise_mode: { type: "enum", values: M.NOISE_MODES, feature: "noise_control", field: "noiseMode" },
  ambient_sound_level: { type: "int", min: M.AMBIENT_MIN, max: M.AMBIENT_MAX, feature: "noise_control", field: "ambientSoundLevel" },
  ambient_level: { type: "int", feature: "noise_control", alias: "ambient_sound_level" },
  voice_passthrough: { type: "bool", feature: "noise_control", field: "focusOnVoice" },
  eq_preset: { type: "enum", values: M.EQ_PRESET_VALUES, feature: "eq_preset", field: "eqPreset" },
  eq_presets: { type: "enumList", values: M.EQ_PRESET_VALUES, feature: "eq_preset", field: "eqPresets" },
  eq_custom_bands: { type: "bands", feature: "eq_custom", field: "eqCustomBands" },
  eq_bands: { type: "bands", feature: "eq_custom", alias: "eq_custom_bands" },
  clear_bass: { type: "int", min: M.EQ_LEVEL_MIN, max: M.EQ_LEVEL_MAX, feature: "eq_custom", field: "clearBass" },
  dsee_extreme: { type: "bool", feature: "dsee", field: "dseeExtreme" },
  dsee: { type: "bool", feature: "dsee", alias: "dsee_extreme" },
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
  ear_detection: { type: "bool", feature: "ear_detection", field: "earDetection" },
  worn: { type: "bool", feature: "worn", field: "worn" },
  multipoint: { type: "bool", feature: "multipoint", field: "multipoint" },
  devices: { type: "devices", feature: "devices", field: "devices" },
  touch_panel: { type: "bool", feature: "touch_panel", field: "touchPanel" },
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

async function startDaemon(profile) {
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
    statusFile: path.join(env.XDG_STATE_HOME, "sony-headphones/status.json")
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
  // Initial read done: every supported field has a value.
  await waitStatus(d, (s) => M.isLive(s) && s.supported !== null && unreadFields(s).length === 0, 5000, "initial read");
  return d;
}

async function stopDaemon(d) {
  d.child.kill("SIGTERM");
  await d.exit;
  running.delete(d);
  fs.rmSync(d.dir, { recursive: true, force: true });
}

function unreadFields(s) {
  return Object.keys(M.FIELD_FEATURES).filter((f) =>
    s.supported[M.FIELD_FEATURES[f]] === true && (s[f] === null || s[f] === M.LEVEL_UNKNOWN));
}

function readStatus(d) {
  let text = "";
  try { text = fs.readFileSync(d.statusFile, "utf-8"); } catch (_e) { /* not written yet */ }
  let raw = null;
  try { raw = JSON.parse(text); } catch (_e) { /* parseStatus reports it */ }
  return { raw, parsed: M.parseStatus(text) };
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

// Checks one daemon-written status against the contract and Model's parse.
function checkSchema(raw, parsed, where) {
  ok(MODEL_KEYS.every((k) => k in KEYS), `${where}: the test knows every key Model reads`,
    MODEL_KEYS.filter((k) => !(k in KEYS)));
  ok(!parsed.schemaTooNew && parsed.lastError === "" && parsed.ok === true, `${where}: parse ok, no schemaTooNew`, parsed.lastError);
  if (!raw || raw.connected !== true) {
    eq(raw ? Object.keys(raw).sort() : null, ["connected", "last_updated", "schema_version"], `${where}: disconnected file has three keys`);
    eq(parsed.connected, false, `${where}: parsed as disconnected`);
    return;
  }
  for (const key of Object.keys(KEYS)) {
    const spec = KEYS[key];
    if (!ok(key in raw, `${where}: daemon writes '${key}'`)) continue;
    const v = raw[key];
    const gated = spec.feature && raw.supported && raw.supported[spec.feature] !== true;
    if (gated) {
      eq(v, null, `${where}: '${key}' is null while '${spec.feature}' is unsupported`);
      if (spec.field) eq(parsed[spec.field], spec.field === "batteryLevel" ? M.LEVEL_UNKNOWN : null, `${where}: Model hides '${spec.field}'`);
      continue;
    }
    // The simulated headset answers every request, so nothing stays unknown.
    if (!ok(v !== null, `${where}: '${key}' is not null`)) continue;
    ok(typeOk(spec, v), `${where}: '${key}' has type ${spec.type}`, v);
    if (spec.alias) eq(v, raw[spec.alias], `${where}: alias '${key}' equals '${spec.alias}'`);
    // Lossless parse: no clamp, no "unknown"/"other" fallback, no dropped value.
    if (spec.field) eq(parsed[spec.field], v, `${where}: Model reads '${key}' unchanged`);
  }
  if (raw.supported) eq(parsed.supported, raw.supported, `${where}: Model reads 'supported' unchanged`);
  ok(parsed.eqPreset !== "unknown" && parsed.connectionQuality !== "other" &&
    parsed.buttonFunction !== "other" && parsed.codec !== "OTHER" &&
    parsed.quickAccessDoubleTap !== "other" && parsed.quickAccessTripleTap !== "other", `${where}: no unknown-value fallback`);
}

// ---------------------------------------------------------------------------
// Forward direction: Model builds the command, the real CLI runs it
// ---------------------------------------------------------------------------

const samples = {};
let commandCount = 0;

// Sends one UI action and waits until the daemon reports every optimistic
// value. Returns the settled status.
async function send(d, action, value, label) {
  const before = readStatus(d).parsed;
  const built = M.buildCommand(action, value, before);
  if (!ok(!built.error, `${label}: Model builds a command`, built.error)) return before;
  const pending = M.addPending({}, built.pending, 1);
  const optimistic = M.applyPending(before, pending);
  const r = cli(d, built.args);
  commandCount++;
  const okReply = ok(r.code === 0 && r.stdout === "OK", `${label}: sony-ctl ${built.args.join(" ")} -> OK`, r);
  if (!samples[action] && !(built.args[0] === "eq" && built.args[1] === "custom" && action === "eqPreset")) {
    samples[action] = { args: built.args, value, feature: built.feature };
  }
  if (!okReply) return before;
  let st;
  try {
    st = await waitStatus(d, (s) => !M.hasPending(M.reconcilePending(pending, s)), 3000, `${label} to settle`);
  } catch (e) {
    ok(false, `${label}: status.json reflects ${JSON.stringify(built.pending)}`, e.message);
    return readStatus(d).parsed;
  }
  for (const f of Object.keys(built.pending)) eq(st.parsed[f], optimistic[f], `${label}: optimistic ${f} == settled`);
  checkSchema(st.raw, st.parsed, label);
  console.log(`  ✓ ${label}: ${built.args.join(" ")}`);
  return st.parsed;
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

// `args` builds the sony-ctl argv for a value; `refusal` is the ERR subject.
const LIST_TARGETS = {
  eqPreset: { args: (v) => ["eq", v], values: M.EQ_PRESET_VALUES },
  autoPowerOff: { args: (v) => ["auto-power-off", v], values: M.AUTO_POWER_OFF_VALUES },
  buttonFunction: { args: (v) => ["button-function", v], values: M.BUTTON_FUNCTION_VALUES },
  quickAccessDouble: { args: (v) => ["quick-access", "double", v], refusal: (v) => `quick_access double ${v}`, values: M.QUICK_ACCESS_VALUES },
  quickAccessTriple: { args: (v) => ["quick-access", "triple", v], refusal: (v) => `quick_access triple ${v}`, values: M.QUICK_ACCESS_VALUES }
};

function rangeValues(min, max) {
  return [min, max, Math.round((min + max) / 2)];
}

async function exerciseTarget(d, id) {
  const t = M.TARGETS[id];
  let s = readStatus(d).parsed;
  if (t.kind === "collapse") {
    eq(M.targetState(s, id), "enabled", `${id}: section header shown`);
    return;
  }
  if (!eq(M.targetState(s, id), "enabled", `${id}: target enabled`)) return;

  if (t.toggle) {
    for (let i = 0; i < 2; i++) {
      const act = M.targetActivation(s, id, 0);
      if (!ok(act !== null, `${id}: toggle activates`)) return;
      s = await send(d, act.action, act.value, `${id}=${act.value}`);
    }
    return;
  }

  if (t.kind === "walk" || t.kind === "grid") {
    const count = M.targetOptions(s, id).length;
    for (let i = 0; i < count; i++) {
      const options = M.targetOptions(s, id);
      const act = M.targetActivation(s, id, i);
      if (!ok(act !== null && act.value === options[i], `${id}[${i}]: chip activates`, act)) continue;
      s = await send(d, act.action, act.value, `${id}=${act.value}`);
      const chip = M.targetChoices(s, id).find((c) => c.value === s[t.field]);
      ok(chip && chip.label !== "Unknown", `${id}=${act.value}: settled value has a chip label`, s[t.field]);
      const section = M.cursorSections(s, allExpanded()).find((x) => x.id === id);
      eq(section ? M.targetOptions(s, id)[section.preferred] : null, act.value, `${id}=${act.value}: cursor marks the chip`);
    }
    // Contract values the headset list leaves out: no chip, and the daemon
    // refuses them (contract 3.1 step 6).
    const list = LIST_TARGETS[id];
    if (list) {
      for (const v of list.values.filter((x) => !M.targetOptions(s, id).includes(x))) {
        eq(M.buildCommand(t.action, v, s), { error: "Not supported by this headset" }, `${id}=${v}: Model refuses a value outside the list`);
        const args = list.args(v);
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
    for (const v of rangeValues(min, max)) s = await send(d, t.action, v, `${id}=${v}`);
    const step = M.targetAdjustment(s, id, 0, -1);
    if (ok(step !== null, `${id}: keyboard step adjusts`)) s = await send(d, step.action, step.value, `${id} step=${step.value}`);
    return;
  }

  if (id === "eqBands") {
    for (let index = 0; index < 6; index++) {
      for (const level of rangeValues(M.EQ_LEVEL_MIN, M.EQ_LEVEL_MAX)) {
        s = await send(d, t.action, { index, level }, `eqBands[${index}]=${level}`);
      }
      const step = M.targetAdjustment(s, id, index, 1);
      if (ok(step !== null, `eqBands[${index}]: keyboard step adjusts`)) {
        s = await send(d, step.action, step.value, `eqBands[${index}] step=${step.value.level}`);
      }
    }
    return;
  }

  if (id === "devices") {
    const devices = s.devices.slice();
    for (let i = 0; i < devices.length; i++) {
      s = readStatus(d).parsed;
      const dev = s.devices[i];
      const act = M.targetActivation(s, id, i);
      if (dev.active || !dev.connected) {
        eq(act, null, `devices[${i}] (${M.deviceStateLabel(dev)}): no command`);
        if (!dev.connected) {
          eq(M.buildCommand("source", dev.address, s), { error: "Device is not connected" }, `devices[${i}]: Model refuses a disconnected device`);
          const r = cli(d, ["source", dev.address]);
          eq([r.code, r.stderr], [1, `Error from daemon: ERR unsupported source_switch ${dev.address}`], `devices[${i}]: daemon refuses a disconnected device`);
        }
        continue;
      }
      if (!ok(act !== null && act.action === "source", `devices[${i}]: switch activates`, act)) continue;
      s = await send(d, act.action, act.value, `devices[${i}] source`);
      eq(s.devices[i].active, true, `devices[${i}]: now active`);
    }
    return;
  }

  ok(false, `${id}: the test does not know this target kind (${t.kind})`);
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
console.log("=== Model.js <-> sony-ctl <-> sony-headphones-daemon --mock ===");
console.log(`daemon: ${DAEMON_BIN}\ncli:    ${CLI_BIN}\nsandbox parent: ${SANDBOX_PARENT}`);

let full = null;
try {
  await runGroup("E1", "Daemon status.json matches what Model.js reads (full XM5)", async () => {
    full = await startDaemon("xm5");
    const st = readStatus(full);
    checkSchema(st.raw, st.parsed, "xm5 initial");
    const missing = MODEL_KEYS.filter((k) => !(k in st.raw));
    eq(missing, [], "every key Model reads is in the daemon output");
    const viaCli = cli(full, ["status"]);
    let cliRaw = null;
    try { cliRaw = JSON.parse(viaCli.stdout); } catch (_e) { /* checked below */ }
    ok(viaCli.code === 0 && cliRaw !== null, "sony-ctl status prints the status object", viaCli);
    if (cliRaw) {
      const strip = (o) => { const c = { ...o }; delete c.last_updated; return c; };
      eq(strip(cliRaw), strip(st.raw), "sony-ctl status equals status.json");
    }
    for (const id of M.TARGET_ORDER) ok(M.targetVisible(st.parsed, id), `target '${id}' is visible`);
    full.child.kill("SIGUSR1");
    const off = await waitStatus(full, (s) => s.ok && !s.connected, 3000, "disconnect");
    checkSchema(off.raw, off.parsed, "xm5 disconnected");
    eq(M.cursorSections(off.parsed, allExpanded()), [], "disconnected: no cursor targets");
    full.child.kill("SIGUSR2");
    await waitStatus(full, (s) => M.isLive(s) && s.supported !== null && unreadFields(s).length === 0, 5000, "reconnect");
  });

  await runGroup("E2", "Every target, option and slider value through the real CLI (full XM5)", async () => {
    if (!full) throw new Error("no daemon");
    for (const id of M.TARGET_ORDER) await exerciseTarget(full, id);

    // quick-access sets one press; the other keeps its function (contract 3.3).
    for (const [press, other] of [["double", "triple"], ["triple", "double"]]) {
      let s = readStatus(full).parsed;
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
    for (const args of [["touch-panel", "maybe"], ["touch-panel"], ["quick-access", "double"],
      ["quick-access", "quad", "off"], ["quick-access", "double", "other"]]) {
      const r = cli(full, args);
      commandCount++;
      const usage = args[0] === "touch-panel" ? "touch-panel on|off" : "quick-access double|triple <value>";
      eq([r.code, r.stdout, r.stderr], [1, "", `Error: usage: ${usage}`], `sony-ctl ${args.join(" ")} -> usage error`);
    }
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
      if (t.kind === "collapse") continue;
      const sample = samples[t.action];
      if (!ok(sample, `${id}: a command from the full run exists`)) continue;
      eq(M.buildCommand(t.action, sample.value, s), { error: "Not supported by this headset" }, `${id}: Model refuses ${t.action}`);
      const r = cli(min, sample.args);
      commandCount++;
      eq([r.code, r.stdout, r.stderr], [1, "", `Error from daemon: ERR unsupported ${sample.feature}`], `${id}: sony-ctl ${sample.args.join(" ")} -> ERR`);
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
process.exit(failed === 0 && groups.length === 4 ? 0 : 1);
