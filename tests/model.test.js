// tests/model.test.js
// Standalone unit tests for Model.js.
// Command: node tests/model.test.js
//       or: deno run --allow-read tests/model.test.js
// Works from any working directory: paths resolve against this file.

let readFileSync;
if (typeof Deno !== "undefined") {
  readFileSync = (p) => Deno.readTextFileSync(p);
} else if (typeof process !== "undefined") {
  const fs = await import("fs");
  readFileSync = (p) => fs.readFileSync(p, "utf-8");
} else {
  throw new Error("Unsupported runtime: expected Deno or Node.js");
}

const testsDir = new URL(".", import.meta.url);
const repoFile = (rel) => decodeURIComponent(new URL("../" + rel, testsDir).pathname);
const fixture = (name) => readFileSync(repoFile("tests/fixtures/" + name));

// Model.js is a QML JavaScript library: top-level `var` and `function`
// declarations, no module syntax. Export every top-level name.
const modelSource = readFileSync(repoFile("Model.js"));
const names = [...modelSource.matchAll(/^(?:function|var)\s+([A-Za-z_$][\w$]*)/gm)].map((m) => m[1]);
const Model = new Function(modelSource + "\n; return { " + names.join(", ") + " };")();

let passed = 0;
let failed = 0;

function check(name, actual, expected) {
  const actualStr = JSON.stringify(actual);
  const expectedStr = JSON.stringify(expected);
  if (actualStr === expectedStr) {
    passed++;
    console.log(`  ✓ ${name}`);
  } else {
    failed++;
    console.error(`  ✗ ${name}`);
    console.error(`    Expected: ${expectedStr}`);
    console.error(`    Actual:   ${actualStr}`);
  }
}

function suite(title, fn) {
  console.log(`\n[${title}]`);
  fn();
}

const parse = (obj) => Model.parseStatus(JSON.stringify(obj));
const full = () => Model.parseStatus(fixture("status_v2_full.json"));
const ids = (sections) => sections.map((s) => s.id);

console.log("=== Running Model.js Unit Tests ===");

suite("Suite 1: Empty & Malformed Inputs", () => {
  const r1 = Model.parseStatus("");
  check("Empty string returns ok: false", r1.ok, false);
  check("Empty string has lastError", r1.lastError.includes("empty"), true);
  check("null input returns ok: false", Model.parseStatus(null).ok, false);
  check("Whitespace string returns ok: false", Model.parseStatus("   \n\t  ").ok, false);
  const r3 = Model.parseStatus("invalid json {[[");
  check("Malformed JSON returns ok: false", r3.ok, false);
  check("Malformed JSON error message", r3.lastError.includes("Could not read"), true);
  check("JSON null returns ok: false", Model.parseStatus("null").ok, false);
  check("scalar number returns ok: false", Model.parseStatus("12345").ok, false);
  check("array JSON returns ok: false", Model.parseStatus("[]").ok, false);
  for (const f of ["status_empty.json", "status_whitespace.json", "status_corrupted.json", "status_non_object.json", "status_missing_schema.json"]) {
    const r = Model.parseStatus(fixture(f));
    check(`${f} is an error state`, [r.ok, r.connected, r.lastError !== ""], [false, false, true]);
  }
});

suite("Suite 2: Schema Version Handling", () => {
  const r1 = parse({ connected: true });
  check("Missing schema_version returns ok: false", r1.ok, false);
  check("Missing schema_version error text", r1.lastError.includes("no schema_version"), true);
  check("SUPPORTED_SCHEMA is 2", Model.SUPPORTED_SCHEMA, 2);
  const r2 = parse({ schema_version: 1, connected: true });
  check("Schema 1 accepted", [r2.ok, r2.schemaTooNew, r2.schemaVersion], [true, false, 1]);
  const r3 = parse({ schema_version: 2, connected: true });
  check("Schema 2 accepted", [r3.ok, r3.schemaTooNew, r3.schemaVersion], [true, false, 2]);
  const r4 = parse({ schema_version: 3, connected: true, device_name: "x" });
  check("Schema 3 is too new", [r4.ok, r4.schemaTooNew, r4.schemaVersion, r4.connected], [false, true, 3, false]);
  check("Schema 3 message", r4.lastError, "sony daemon speaks status schema 3, this panel reads 2");
  const r5 = parse({ schema_version: "two", connected: true });
  check("Non-numeric schema is too new", [r5.ok, r5.schemaTooNew], [false, true]);
  const r6 = Model.parseStatus(fixture("status_schema_too_new.json"));
  check("Fixture schema 99 is too new", [r6.schemaTooNew, r6.schemaVersion], [true, 99]);
  check("Too new: no controls", Model.controlState(r6, "noise_control", "anc"), "hidden");
  check("Too new: hero meta", Model.heroMeta(r6), "Update needed");
  check("Too new: empty state text", Model.emptyStateText(r6).includes("schema 99"), true);
});

suite("Suite 3: v1 Payload", () => {
  const r = parse({
    schema_version: 1, connected: true, device_name: "WH-1000XM5", battery_level: 78,
    battery_charging: false, noise_mode: "anc", ambient_sound_level: 3, eq_preset: "bright",
    eq_custom_bands: [1, 2, 0, -1, 3], clear_bass: 2, speak_to_chat: true, dsee_extreme: true,
    multipoint: true, codec: "LDAC", ear_detection: true
  });
  check("ok and connected", [r.ok, r.connected], [true, true]);
  check("Device name", r.deviceName, "WH-1000XM5");
  check("Battery level", r.batteryLevel, 78);
  check("Battery charging", r.batteryCharging, false);
  check("Noise mode", r.noiseMode, "anc");
  check("Ambient level", r.ambientSoundLevel, 3);
  check("EQ preset", r.eqPreset, "bright");
  check("EQ custom bands", r.eqCustomBands, [1, 2, 0, -1, 3]);
  check("Clear bass", r.clearBass, 2);
  check("Speak-to-chat", r.speakToChat, true);
  check("DSEE extreme", r.dseeExtreme, true);
  check("Multipoint", r.multipoint, true);
  check("Codec", r.codec, "LDAC");
  check("Ear detection", r.earDetection, true);
  check("v1 supported table", r.supported, {
    battery: true, noise_control: true, eq_preset: true, eq_custom: true, dsee: true,
    speak_to_chat: true, voice_guidance: false, voice_guidance_volume: false, auto_power_off: false,
    nc_amb_button: false, button_function: false, ear_detection: true, worn: false, volume: false,
    connection_quality: false, codec: true, multipoint: true, devices: false, source_switch: false,
    touch_panel: false, quick_access: false
  });
  check("v1 supported_source is null", r.supportedSource, null);
  check("v1 volume control hidden", Model.controlState(r, "volume", r.volume), "hidden");
  check("v1 footer mentions old daemon", Model.footerLines(r), ["Old daemon (status schema 1)"]);

  const unknown = parse({ schema_version: 1, connected: true, battery_level: -1 });
  check("v1 battery -1 is unknown", unknown.batteryLevel, -1);
  check("v1 battery -1 control disabled", Model.controlState(unknown, "battery", unknown.batteryLevel), "disabled");

  const wind = Model.parseStatus(fixture("status_connected_wind.json"));
  check("v1 wind mode kept", wind.noiseMode, "wind");
  check("v1 wind name", Model.noiseModeName(wind.noiseMode), "Wind Noise Reduction");
  check("v2 wind mode rejected", parse({ schema_version: 2, connected: true, supported: null, noise_mode: "wind" }).noiseMode, null);

  const aliases = parse({ schema_version: 1, connected: true, charging: true, ambient_level: 7, eq_bands: [1, 1, 1, 1, 1], dsee: true });
  check("Deprecated aliases read", [aliases.batteryCharging, aliases.ambientSoundLevel, aliases.eqCustomBands, aliases.dseeExtreme], [true, 7, [1, 1, 1, 1, 1], true]);

  for (const f of ["status_connected_ambient.json", "status_connected_anc.json", "status_connected_charging.json",
    "status_connected_custom_eq.json", "status_connected_low_battery.json", "status_connected_off.json"]) {
    const s = Model.parseStatus(fixture(f));
    check(`${f} parses as v1 connected`, [s.ok, s.connected, s.schemaVersion], [true, true, 1]);
  }
  const charging = Model.parseStatus(fixture("status_connected_charging.json"));
  check("v1 charging fixture", [charging.batteryLevel, charging.batteryCharging, Model.isCharging(charging)], [92, true, true]);
});

suite("Suite 4: Disconnected State", () => {
  for (const f of ["status_disconnected.json", "status_v2_disconnected.json"]) {
    const r = Model.parseStatus(fixture(f));
    check(`${f}: ok, not connected`, [r.ok, r.connected], [true, false]);
    check(`${f}: battery unknown`, r.batteryLevel, -1);
    check(`${f}: noise mode null`, r.noiseMode, null);
    check(`${f}: every control hidden`, Model.FEATURE_KEYS.every((k) => Model.controlState(r, k, true) === "hidden"), true);
    check(`${f}: no cursor targets`, Model.cursorSections(r, { sound: true }), []);
    check(`${f}: bar text`, Model.barBatteryText(r), "—");
    check(`${f}: tooltip`, Model.barTooltip(r), "Sony headphones: disconnected");
    check(`${f}: hero meta`, Model.heroMeta(r), "Disconnected");
  }
  const v2 = Model.parseStatus(fixture("status_v2_disconnected.json"));
  check("v2 disconnected last_updated", v2.lastUpdated, 1760438400);
  check("Disconnected empty state text", Model.emptyStateText(v2).includes("Bluetooth"), true);
});

suite("Suite 5: Boundaries & Clamping", () => {
  const r = parse({ schema_version: 1, connected: true, battery_level: 150, ambient_sound_level: 99, eq_custom_bands: [-20, 20, 0, 5, -8], clear_bass: 15 });
  check("Battery clamped to 100", r.batteryLevel, 100);
  check("Ambient level clamped to 20", r.ambientSoundLevel, 20);
  check("Custom bands clamped to [-10, 10]", r.eqCustomBands, [-10, 10, 0, 5, -8]);
  check("Clear bass clamped to 10", r.clearBass, 10);
  const r2 = parse({ schema_version: 1, connected: true, battery_level: -10, ambient_sound_level: -5, eq_custom_bands: [2, 3], clear_bass: -25 });
  check("Negative battery is unknown", r2.batteryLevel, -1);
  check("Ambient level clamped to 1", r2.ambientSoundLevel, 1);
  check("Short EQ bands padded with 0", r2.eqCustomBands, [2, 3, 0, 0, 0]);
  check("Clear bass clamped to -10", r2.clearBass, -10);
  const ex = Model.parseStatus(fixture("status_extreme_values.json"));
  check("Extreme fixture: invalid enums become null", [ex.noiseMode, ex.eqPreset], [null, null]);
  check("Extreme fixture: clamped numbers", [ex.batteryLevel, ex.ambientSoundLevel, ex.eqCustomBands, ex.clearBass], [100, 20, [-10, 10, 0, 5, -8], 10]);
  const v2 = parse({ schema_version: 2, connected: true, supported: null, volume: 99, voice_guidance_volume: -9, codec: "opus", eq_preset: "UNKNOWN" });
  check("Volume clamped to 30", v2.volume, 30);
  check("Guidance volume clamped to -2", v2.voiceGuidanceVolume, -2);
  check("Unknown codec becomes OTHER", v2.codec, "OTHER");
  check("eq_preset unknown kept", v2.eqPreset, "unknown");
  const bools = parse({ schema_version: 2, connected: true, supported: null, battery_level: true, dsee_extreme: "yes" });
  check("Boolean battery is unknown", bools.batteryLevel, -1);
  check("Non-boolean flag is null", bools.dseeExtreme, null);
});

suite("Suite 6: v2 Full Fixture", () => {
  const s = full();
  check("ok, connected, schema 2", [s.ok, s.connected, s.schemaVersion], [true, true, 2]);
  check("Identity", [s.deviceName, s.address, s.modelName, s.firmwareVersion], ["WH-1000XM5", "AC:80:0A:12:34:56", "WH-1000XM5", "2.3.1"]);
  check("supported_source device", s.supportedSource, "device");
  check("Every feature supported", Model.FEATURE_KEYS.every((k) => s.supported[k] === true), true);
  check("Noise", [s.noiseMode, s.ambientSoundLevel, s.focusOnVoice], ["ambient", 14, true]);
  check("EQ", [s.eqPreset, s.eqPresets.length, s.eqCustomBands, s.clearBass], ["custom", 10, [2, 1, -1, 3, 0], 4]);
  check("Sound", [s.dseeExtreme, s.connectionQuality, s.codec, s.volume], [true, "sound", "LDAC", 12]);
  check("Speak-to-Chat", [s.speakToChat, s.stcSensitivity, s.stcTimeout], [true, "auto", "standard"]);
  check("Voice guidance", [s.voiceGuidance, s.voiceGuidanceVolume], [true, 0]);
  check("Power", [s.autoPowerOff, s.autoPowerOffOptions.length], ["when_taken_off", 7]);
  check("Buttons", [s.ncAmbButton, s.buttonFunction, s.buttonFunctions], ["nc_ambient_off", "ambient_sound_control", ["ambient_sound_control", "google_assistant", "amazon_alexa", "quick_access"]]);
  check("Wearing", [s.earDetection, s.worn], [true, true]);
  check("Multipoint", [s.multipoint, s.devices], [false, [{ address: "F0:1D:BC:AA:00:01", name: "laptop", connected: true, active: true }]]);
  check("Every control enabled", [
    ["noise_control", s.noiseMode], ["eq_preset", s.eqPreset], ["eq_custom", s.eqCustomBands], ["dsee", s.dseeExtreme],
    ["speak_to_chat", s.speakToChat], ["voice_guidance", s.voiceGuidance], ["voice_guidance_volume", s.voiceGuidanceVolume],
    ["auto_power_off", s.autoPowerOff], ["nc_amb_button", s.ncAmbButton], ["button_function", s.buttonFunction],
    ["ear_detection", s.earDetection], ["volume", s.volume], ["connection_quality", s.connectionQuality],
    ["multipoint", s.multipoint], ["devices", s.devices]
  ].every(([k, v]) => Model.controlState(s, k, v) === "enabled"), true);
  check("All sections visible", ["noise", "sound", "customEq", "speakToChat", "headset", "multipoint"].map((n) => Model.sectionVisible(s, n)), [true, true, true, true, true, true]);
  check("Bar text", Model.barBatteryText(s), "80%");
  check("Tooltip", Model.barTooltip(s), "WH-1000XM5\nAmbient Sound · LDAC\nBattery 80%");
  check("Hero meta", Model.heroMeta(s), "Connected · LDAC · On head");
  check("Footer", Model.footerLines(s), ["WH-1000XM5 · firmware 2.3.1 · AC:80:0A:12:34:56"]);
  check("Not reading", Model.isReading(s), false);
  check("Collapsed cursor sections", ids(Model.cursorSections(s, {})),
    ["noise", "ambient", "focusVoice", "sound", "customEq", "speakToChatHeader", "headset", "multipointHeader"]);
  check("Expanded cursor sections", ids(Model.cursorSections(s, { sound: true, customEq: true, speakToChat: true, headset: true, multipoint: true })),
    ["noise", "ambient", "focusVoice", "sound", "eqPreset", "dsee", "connectionQuality", "customEq", "eqBands",
      "speakToChatHeader", "speakToChat", "stcSensitivity", "stcTimeout", "headset", "volume", "voiceGuidance",
      "voiceGuidanceVolume", "autoPowerOff", "ncAmbButton", "buttonFunction", "quickAccessDouble", "quickAccessTriple",
      "earDetection", "touchPanel", "multipointHeader", "multipoint", "devices"]);
  const sec = Model.cursorSections(s, { sound: true, headset: true });
  const byId = Object.fromEntries(sec.map((x) => [x.id, x]));
  check("Noise preferred is current mode", byId.noise.preferred, 1);
  check("EQ grid", [byId.eqPreset.kind, byId.eqPreset.count, byId.eqPreset.columns, byId.eqPreset.preferred], ["grid", 10, 5, 9]);
  check("Auto power off grid preferred", [byId.autoPowerOff.count, byId.autoPowerOff.preferred], [7, 6]);
  check("Button function grid", [byId.buttonFunction.count, byId.buttonFunction.preferred], [4, 0]);
});

suite("Suite 7: v2 Minimal Fixture", () => {
  const s = Model.parseStatus(fixture("status_v2_minimal.json"));
  check("Battery and charging", [s.batteryLevel, s.batteryCharging], [45, true]);
  check("Noise enabled", Model.controlState(s, "noise_control", s.noiseMode), "enabled");
  check("EQ hidden", Model.controlState(s, "eq_preset", s.eqPreset), "hidden");
  check("Codec hidden", Model.controlState(s, "codec", s.codec), "hidden");
  check("Only noise section", ["noise", "sound", "customEq", "speakToChat", "headset", "multipoint"].map((n) => Model.sectionVisible(s, n)), [true, false, false, false, false, false]);
  check("Cursor sections", ids(Model.cursorSections(s, { sound: true, headset: true, multipoint: true })), ["noise", "ambient", "focusVoice"]);
  check("Charging", Model.isCharging(s), true);
  check("Tooltip without codec", Model.barTooltip(s), "WH-1000XM5\nNoise Cancelling\nBattery 45% (charging)");
  check("Hero meta without codec", Model.heroMeta(s), "Connected");
  check("Footer without firmware", Model.footerLines(s), ["WH-1000XM5 · AC:80:0A:12:34:56"]);
  check("EQ command refused", Model.buildCommand("eqPreset", "bass", s), { error: "Not supported by this headset" });
});

suite("Suite 8: v2 Multipoint Fixture", () => {
  const s = Model.parseStatus(fixture("status_v2_multipoint.json"));
  check("Device name", s.deviceName, "My XM5");
  check("Devices", s.devices.map((d) => [Model.deviceLabel(d), Model.deviceStateLabel(d)]),
    [["laptop", "Connected"], ["Pixel 8", "Playing"], ["3C:22:FB:CC:00:03", "Not connected"]]);
  check("Can switch to connected idle device", s.devices.map((d) => Model.canSwitchTo(s, d)), [true, false, false]);
  check("Button function hidden", Model.controlState(s, "button_function", s.buttonFunction), "hidden");
  check("Guidance volume hidden", Model.controlState(s, "voice_guidance_volume", s.voiceGuidanceVolume), "hidden");
  check("Worn hidden", Model.heroMeta(s), "Connected · AAC");
  const sec = Model.cursorSections(s, { multipoint: true, headset: true });
  check("Headset targets", ids(sec).filter((i) => ["volume", "voiceGuidance", "voiceGuidanceVolume", "autoPowerOff", "ncAmbButton", "buttonFunction", "earDetection"].includes(i)),
    ["volume", "voiceGuidance", "autoPowerOff", "ncAmbButton", "earDetection"]);
  const devices = sec.find((x) => x.id === "devices");
  check("Device rows", [devices.kind, devices.count], ["rows", 3]);
  const cmd = Model.buildCommand("source", "f0:1d:bc:aa:00:01", s);
  check("Source command normalizes address", cmd.args, ["source", "F0:1D:BC:AA:00:01"]);
  check("Source optimistic devices", cmd.pending.devices.map((d) => d.active), [true, false, false]);
  check("Source to disconnected device refused", Model.buildCommand("source", "3C:22:FB:CC:00:03", s), { error: "Device is not connected" });
  check("Source to unknown device refused", Model.buildCommand("source", "00:00:00:00:00:00", s), { error: "Device is not connected" });
  check("Source bad address refused", Model.buildCommand("source", "laptop", s), { error: "Invalid address" });
  const twoActive = parse({ schema_version: 2, connected: true, supported: null, devices: [
    { address: "aa:bb:cc:dd:ee:01", name: "a", connected: true, active: true },
    { address: "AA:BB:CC:DD:EE:02", name: "b", connected: true, active: true },
    { address: "AA:BB:CC:DD:EE:03", name: "c", connected: false, active: true },
    { address: "bad", name: "d", connected: true, active: false }
  ] });
  check("At most one active, active implies connected, bad address dropped", twoActive.devices.map((d) => [d.address, d.active]),
    [["AA:BB:CC:DD:EE:01", true], ["AA:BB:CC:DD:EE:02", false], ["AA:BB:CC:DD:EE:03", false]]);
});

suite("Suite 9: v2 Initializing Fixture", () => {
  const s = Model.parseStatus(fixture("status_v2_initializing.json"));
  check("Reading", Model.isReading(s), true);
  check("supported null", [s.supported, s.supportedSource], [null, null]);
  check("Every control disabled", Model.FEATURE_KEYS.every((k) => Model.controlState(s, k, "x") === "disabled"), true);
  check("Every section visible", ["noise", "sound", "customEq", "speakToChat", "headset", "multipoint"].every((n) => Model.sectionVisible(s, n)), true);
  check("Bar text unknown battery", Model.barBatteryText(s), "—");
  check("Tooltip", Model.barTooltip(s), "WH-1000XM5\nReading headset settings…");
  check("Hero meta", Model.heroMeta(s), "Reading settings");
  check("Commands refused", Model.buildCommand("noise", "anc", s), { error: "Reading headset settings…" });
  check("Cursor still lists headers", ids(Model.cursorSections(s, {})),
    ["noise", "ambient", "focusVoice", "sound", "customEq", "speakToChatHeader", "headset", "multipointHeader"]);
  const known = parse({ schema_version: 2, connected: true, supported: null, battery_level: 40, device_name: "x" });
  check("Battery shown while reading", Model.barBatteryText(known), "40%");
});

suite("Suite 10: v2 Fallback Fixture", () => {
  const s = Model.parseStatus(fixture("status_v2_fallback.json"));
  check("supported_source fallback", s.supportedSource, "fallback");
  check("Footer hint", Model.footerLines(s), ["WH-1000XM5 · firmware 2.3.1 · AC:80:0A:12:34:56", "Limited: headset did not report its features"]);
  check("DSEE hidden", Model.controlState(s, "dsee", s.dseeExtreme), "hidden");
  check("Codec hidden", Model.controlState(s, "codec", s.codec), "hidden");
  check("Volume enabled", Model.controlState(s, "volume", s.volume), "enabled");
  check("Guidance volume hidden", Model.controlState(s, "voice_guidance_volume", s.voiceGuidanceVolume), "hidden");
  check("Multipoint section hidden", Model.sectionVisible(s, "multipoint"), false);
  check("Sound section visible for EQ", Model.sectionVisible(s, "sound"), true);
  check("eq_presets null uses v1 list", Model.eqPresetOptions(s), Model.EQ_PRESETS);
  check("Preset accepted from v1 list", Model.buildCommand("eqPreset", "speech", s).args, ["eq", "speech"]);
  check("user preset refused without list", Model.buildCommand("eqPreset", "user1", s), { error: "Not supported by this headset" });
  check("Sound targets", ids(Model.cursorSections(s, { sound: true })).filter((i) => ["eqPreset", "dsee", "connectionQuality"].includes(i)), ["eqPreset"]);
  check("Low battery tooltip", Model.barTooltip(s), "WH-1000XM5\nOff\nBattery 15%");
});

suite("Suite 11: supported false nulls its fields", () => {
  const s = parse({ schema_version: 2, connected: true, supported: { battery: true, noise_control: false }, battery_level: 50, noise_mode: "anc", volume: 9 });
  check("Noise mode nulled", s.noiseMode, null);
  check("Volume nulled", s.volume, null);
  check("Battery kept", s.batteryLevel, 50);
  check("Missing supported keys read as false", s.supported.devices, false);
  const noBattery = parse({ schema_version: 2, connected: true, supported: { battery: false }, battery_level: 50 });
  check("Battery hidden: empty bar text", Model.barBatteryText(noBattery), "");
  check("Battery hidden: no battery line", Model.barTooltip(noBattery), "WH-1000XM5");
});

suite("Suite 12: Command Building", () => {
  const s = full();
  const cmd = (a, v, st) => Model.buildCommand(a, v, st || s);
  check("noise", cmd("noise", "anc"), { args: ["noise", "anc"], pending: { noiseMode: "anc" }, feature: "noise_control" });
  check("noise wind refused", cmd("noise", "wind").error, "Not supported by this headset");
  check("ambient-level sets ambient mode", cmd("ambientLevel", 5), { args: ["ambient-level", "5"], pending: { ambientSoundLevel: 5, noiseMode: "ambient" }, feature: "noise_control" });
  check("ambient-level 0 refused", cmd("ambientLevel", 0).error, "Value out of range");
  check("ambient-level 21 refused", cmd("ambientLevel", 21).error, "Value out of range");
  check("ambient-level fraction refused", cmd("ambientLevel", 2.5).error, "Value out of range");
  check("focus-voice", cmd("focusVoice", false).args, ["focus-voice", "off"]);
  check("focus-voice non-bool refused", cmd("focusVoice", "on").error, "Invalid value");
  check("eq preset", cmd("eqPreset", "bass").args, ["eq", "bass"]);
  check("eq preset not in list", cmd("eqPreset", "user2").error, "Not supported by this headset");
  check("eq custom", cmd("eqCustom", { bands: [1, 2, 3, 4, 5], clearBass: -3 }), {
    args: ["eq", "custom", "1", "2", "3", "4", "5", "-3"],
    pending: { eqPreset: "custom", eqCustomBands: [1, 2, 3, 4, 5], clearBass: -3 }, feature: "eq_custom"
  });
  check("eq custom out of range", cmd("eqCustom", { bands: [1, 2, 3, 4, 11], clearBass: 0 }).error, "Value out of range");
  check("eq custom short", cmd("eqCustom", { bands: [1, 2], clearBass: 0 }).error, "Value out of range");
  check("eq band replaces one value", cmd("eqBand", { index: 2, level: 7 }).args, ["eq", "custom", "2", "1", "7", "3", "0", "4"]);
  check("eq band 5 is clear bass", cmd("eqBand", { index: 5, level: -10 }).args, ["eq", "custom", "2", "1", "-1", "3", "0", "-10"]);
  check("eq band without values", cmd("eqBand", { index: 0, level: 1 }, Object.assign({}, s, { eqCustomBands: null })).error, "Custom EQ values are not known yet");
  check("eq band bad index", cmd("eqBand", { index: 6, level: 1 }).error, "Value out of range");
  check("dsee", cmd("dsee", false).args, ["dsee", "off"]);
  check("connection-quality", cmd("connectionQuality", "connection").args, ["connection-quality", "connection"]);
  check("connection-quality other refused", cmd("connectionQuality", "other").error, "Not supported by this headset");
  check("volume", cmd("volume", 30).args, ["volume", "30"]);
  check("volume 31 refused", cmd("volume", 31).error, "Value out of range");
  check("speak-to-chat", cmd("speakToChat", true).args, ["speak-to-chat", "on"]);
  check("stc sensitivity keeps timeout", cmd("stcSensitivity", "low"), { args: ["speak-to-chat-config", "low", "standard"], pending: { stcSensitivity: "low", stcTimeout: "standard" }, feature: "speak_to_chat" });
  check("stc timeout keeps sensitivity", cmd("stcTimeout", "off").args, ["speak-to-chat-config", "auto", "off"]);
  check("stc config needs other value", cmd("stcTimeout", "off", Object.assign({}, s, { stcSensitivity: null })).error, "Speak-to-Chat settings are not known yet");
  check("stc bad value", cmd("stcSensitivity", "max").error, "Invalid value");
  check("voice-guidance", cmd("voiceGuidance", false).args, ["voice-guidance", "off"]);
  check("voice-guidance-volume", cmd("voiceGuidanceVolume", -2).args, ["voice-guidance-volume", "-2"]);
  check("voice-guidance-volume 3 refused", cmd("voiceGuidanceVolume", 3).error, "Value out of range");
  check("auto-power-off", cmd("autoPowerOff", "60min").args, ["auto-power-off", "60min"]);
  check("auto-power-off not in list", cmd("autoPowerOff", "when_taken_off", Object.assign({}, s, { autoPowerOffOptions: ["off", "5min"] })).error, "Not supported by this headset");
  check("nc-amb-button", cmd("ncAmbButton", "nc_off").args, ["nc-amb-button", "nc_off"]);
  check("button-function", cmd("buttonFunction", "quick_access").args, ["button-function", "quick_access"]);
  check("button-function not in list", cmd("buttonFunction", "voice_recognition").error, "Not supported by this headset");
  check("ear-detect", cmd("earDetection", false).args, ["ear-detect", "off"]);
  check("multipoint", cmd("multipoint", true).args, ["multipoint", "on"]);
  check("unknown action", cmd("bogus", 1).error, "Unknown action bogus");
  check("disconnected refused", cmd("noise", "anc", Model.parseStatus(fixture("status_v2_disconnected.json"))).error, "Headset not connected");
  check("error state refused", cmd("noise", "anc", Model.parseStatus("")).error, "No headset status");
  const noDsee = Object.assign({}, s, { supported: Object.assign({}, s.supported, { dsee: false }) });
  check("unsupported before value check", cmd("dsee", "maybe", noDsee).error, "Not supported by this headset");
});

suite("Suite 13: Option Lists", () => {
  const s = full();
  check("auto power off list from status", Model.autoPowerOffOptions(s), ["off", "5min", "15min", "30min", "60min", "180min", "when_taken_off"]);
  check("auto power off without list", Model.autoPowerOffOptions(Object.assign({}, s, { autoPowerOffOptions: null, autoPowerOff: "30min" })), ["off", "5min", "15min", "30min", "60min", "180min"]);
  check("auto power off without list keeps current when_taken_off", Model.autoPowerOffOptions(Object.assign({}, s, { autoPowerOffOptions: null })).includes("when_taken_off"), true);
  check("button functions without list", Model.buttonFunctionOptions(Object.assign({}, s, { buttonFunctions: null })), Model.BUTTON_FUNCTION_VALUES);
  check("eq presets with user presets", Model.eqPresetOptions(parse({ schema_version: 2, connected: true, supported: null, eq_presets: ["off", "user1", "unknown", "OFF"] })), ["off", "user1"]);
  check("empty eq presets list uses v1 list", Model.eqPresetOptions({ eqPresets: [] }), Model.EQ_PRESETS);
});

suite("Suite 14: Optimistic Values", () => {
  const s = full();
  let pending = {};
  pending = Model.addPending(pending, { noiseMode: "anc" }, 1);
  pending = Model.addPending(pending, { volume: 20 }, 2);
  check("hasPending", Model.hasPending(pending), true);
  const eff = Model.applyPending(s, pending);
  check("applyPending overrides", [eff.noiseMode, eff.volume, eff.dseeExtreme], ["anc", 20, true]);
  check("applyPending does not mutate", s.noiseMode, "ambient");
  check("applyPending without pending returns same object", Model.applyPending(s, {}) === s, true);
  check("dropPending by id", Object.keys(Model.dropPending(pending, 1)), ["volume"]);
  const confirmed = Object.assign({}, s, { noiseMode: "anc" });
  check("reconcile drops confirmed", Object.keys(Model.reconcilePending(pending, confirmed)), ["volume"]);
  check("reconcile keeps object when nothing matches", Model.reconcilePending(pending, s) === pending, true);
  const disc = Model.parseStatus(fixture("status_v2_disconnected.json"));
  check("reconcile keeps pending while disconnected", Model.reconcilePending(pending, disc) === pending, true);
  check("applyPending ignores disconnected", Model.applyPending(disc, pending) === disc, true);
  const bands = Model.addPending({}, { eqCustomBands: [1, 2, 3, 4, 5] }, 3);
  check("reconcile compares arrays by value", Model.hasPending(Model.reconcilePending(bands, Object.assign({}, s, { eqCustomBands: [1, 2, 3, 4, 5] }))), false);
  check("newer id owns the field", Model.dropPending(Model.addPending(pending, { noiseMode: "off" }, 4), 1).noiseMode.value, "off");
});

suite("Suite 15: Command Queue", () => {
  let r = Model.enqueueCommand([], { args: ["volume", "10"], id: 1 });
  r = Model.enqueueCommand(r.queue, { args: ["noise", "anc"], id: 2 });
  check("Append distinct verbs", r.queue.map((i) => i.id), [1, 2]);
  r = Model.enqueueCommand(r.queue, { args: ["volume", "12"], id: 3 });
  check("Same verb replaced and moved to end", r.queue.map((i) => i.id), [2, 3]);
  check("Dropped ids reported", r.dropped, [1]);
  r = Model.enqueueCommand(r.queue, { args: ["ambient-level", "5"], id: 4 });
  r = Model.enqueueCommand(r.queue, { args: ["noise", "off"], id: 5 });
  check("Last intent wins across noise and ambient-level", r.queue.map((i) => i.args.join(" ")), ["volume 12", "ambient-level 5", "noise off"]);
});

suite("Suite 16: Command Error Text", () => {
  check("Exit 2", Model.commandErrorText(2, "Error: Cannot connect to daemon socket (x)", ""), "Daemon not running (sony-ctl could not connect)");
  check("Daemon ERR", Model.commandErrorText(1, "Error from daemon: ERR unsupported dsee\n", ""), "ERR unsupported dsee");
  check("CLI error", Model.commandErrorText(1, "Error: 'volume' requires an integer", ""), "'volume' requires an integer");
  check("Empty output", Model.commandErrorText(1, "", ""), "sony-ctl failed (exit 1)");
  check("Did not start", Model.commandErrorText(127, "", ""), "sony-ctl did not start (is ~/.local/bin/sony-ctl installed?)");
  check("Long error elided", Model.commandErrorText(1, "Error: " + "a".repeat(300), "").length, 138);
});

suite("Suite 17: Cursor Movement", () => {
  const sections = [
    { id: "noise", kind: "walk", count: 3, preferred: 1 },
    { id: "ambient", kind: "adjust", count: 1 },
    { id: "sound", kind: "collapse", count: 1 },
    { id: "eqPreset", kind: "grid", count: 12, columns: 5, preferred: -1 },
    { id: "eqBands", kind: "rows", count: 6 },
    { id: "dsee", kind: "single", count: 1 }
  ];
  const mv = (section, index, dx, dy) => Model.cursorMove({ section, index }, dx, dy, sections);
  check("Clamp unknown section to first", Model.cursorClamp({ section: "x", index: 0 }, sections), { section: "noise", index: 1 });
  check("Clamp empty list", Model.cursorClamp(null, []), { section: "", index: 0 });
  check("Walk right", mv("noise", 1, 1, 0), { section: "noise", index: 2, action: null });
  check("Walk stops at end", mv("noise", 2, 1, 0).index, 2);
  check("Down enters adjust", mv("noise", 0, 0, 1), { section: "ambient", index: 0, action: null });
  check("Adjust emits delta", mv("ambient", 0, -1, 0).action, { type: "adjust", delta: -1 });
  check("Collapse l expands", mv("sound", 0, 1, 0).action, { type: "expand", value: true });
  check("Collapse h collapses", mv("sound", 0, -1, 0).action, { type: "expand", value: false });
  check("Enter grid from above lands at 0", mv("sound", 0, 0, 1), { section: "eqPreset", index: 0, action: null });
  check("Grid down moves by columns", mv("eqPreset", 1, 0, 1).index, 6);
  check("Grid down into short last row clamps", mv("eqPreset", 8, 0, 1), { section: "eqPreset", index: 11, action: null });
  check("Grid down from last row leaves", mv("eqPreset", 11, 0, 1), { section: "eqBands", index: 0, action: null });
  check("Grid up from first row leaves", mv("eqPreset", 3, 0, -1).section, "sound");
  check("Enter grid from below lands on last row start", mv("eqBands", 0, 0, -1), { section: "eqPreset", index: 10, action: null });
  check("Rows walk", mv("eqBands", 2, 0, 1).index, 3);
  check("Rows adjust", mv("eqBands", 2, 1, 0), { section: "eqBands", index: 2, action: { type: "adjust", delta: 1 } });
  check("Rows leave at end", mv("eqBands", 5, 0, 1).section, "dsee");
  check("Enter rows from below lands on last", mv("dsee", 0, 0, -1), { section: "eqBands", index: 5, action: null });
  check("Stop at bottom", mv("dsee", 0, 0, 1).section, "dsee");
  check("Stop at top", mv("noise", 1, 0, -1).section, "noise");
  check("Single ignores h/l", mv("dsee", 0, 1, 0).action, null);
});

suite("Suite 18: Keyboard Adjust Values", () => {
  const s = full();
  check("Ambient up", Model.adjustedValue(s, "ambient", 0, 1), 15);
  check("Ambient at max stays null", Model.adjustedValue(Object.assign({}, s, { ambientSoundLevel: 20 }), "ambient", 0, 1), null);
  check("Ambient unknown", Model.adjustedValue(Object.assign({}, s, { ambientSoundLevel: null }), "ambient", 0, 1), null);
  check("Volume down", Model.adjustedValue(s, "volume", 0, -1), 11);
  check("Guidance volume up", Model.adjustedValue(s, "voiceGuidanceVolume", 0, 1), 1);
  check("EQ band up", Model.adjustedValue(s, "eqBands", 3, 1), { index: 3, level: 4 });
  check("Clear Bass down", Model.adjustedValue(s, "eqBands", 5, -1), { index: 5, level: 3 });
  check("Unknown id", Model.adjustedValue(s, "noise", 0, 1), null);
});

suite("Suite 19: Display Formatters & Helpers", () => {
  check("noiseModeName(anc)", Model.noiseModeName("anc"), "Noise Cancelling");
  check("noiseModeName(ambient)", Model.noiseModeName("ambient"), "Ambient Sound");
  check("noiseModeName(off)", Model.noiseModeName("off"), "Off");
  check("noiseModeName(null)", Model.noiseModeName(null), "Unknown");
  check("noiseModeLabel", Model.NOISE_MODES.map(Model.noiseModeLabel), ["ANC", "Ambient", "Off"]);
  check("eqPresetName(treble)", Model.eqPresetName("treble"), "Treble Boost");
  check("eqPresetName(user3)", Model.eqPresetName("user3"), "User 3");
  check("eqPresetName(unknown)", Model.eqPresetName("unknown"), "Unknown");
  check("eqPresetButtonLabel(bass)", Model.eqPresetButtonLabel("bass"), "Bass");
  check("connectionQualityLabel", ["sound", "connection", "other"].map(Model.connectionQualityLabel), ["Sound quality", "Stable connection", "Other"]);
  check("codecLabel", ["LDAC", "APTX_HD", "NONE", "OTHER", null].map(Model.codecLabel), ["LDAC", "aptX HD", "No stream", "Other", "Unknown"]);
  check("stc labels", [Model.stcSensitivityLabel("auto"), Model.stcTimeoutLabel("long")], ["Auto", "Long"]);
  check("autoPowerOffLabel", Model.AUTO_POWER_OFF_VALUES.map(Model.autoPowerOffLabel), ["Off", "5 min", "15 min", "30 min", "1 h", "3 h", "Taken off"]);
  check("ncAmbButtonLabel", Model.NC_AMB_BUTTON_OPTIONS.map(Model.ncAmbButtonLabel), ["NC·AMB·Off", "NC·AMB", "NC·Off", "AMB·Off"]);
  check("buttonFunctionLabel", Model.buttonFunctionLabel("quick_access"), "Quick access");
  check("formatSigned", [3, 0, -2, null].map(Model.formatSigned), ["+3", "0", "-2", "—"]);
  check("onOffLabel", [true, false, null].map(Model.onOffLabel), ["On", "Off", "Unknown"]);
  check("eqBandsSummary", Model.eqBandsSummary(full()), "+2 +1 -1 +3 0");
  check("formatBattery(78)", Model.formatBattery(78), "78%");
  check("formatBattery(-1)", Model.formatBattery(-1), "—");
  check("levelFraction(78)", Model.levelFraction(78), 0.78);
  check("levelFraction(-1)", Model.levelFraction(-1), 0.0);
  check("batteryIcon charging", typeof Model.batteryIcon(50, true), "string");
  check("batteryIcon unknown", Model.batteryIcon(null, false), Model.batteryIcon(-1, false));
  check("cycleNoiseMode", ["anc", "ambient", "off", "wind", null].map(Model.cycleNoiseMode), ["ambient", "off", "anc", "anc", "anc"]);
  const elided = Model.elideError("Error: " + "a".repeat(200));
  check("elideError length <= 140", elided.length <= 140, true);
  check("elideError ends with ellipsis", elided.endsWith("…"), true);
  check("deviceTitle fallback", Model.deviceTitle(Model.defaultStatus()), "WH-1000XM5");
  check("Error state tooltip", Model.barTooltip(Model.parseStatus("")), "Sony headphones: The sony status file is empty");
  check("Error state hero meta", Model.heroMeta(Model.parseStatus("")), "No status");
});

suite("Suite 20: Schema Version Values", () => {
  for (const bad of ["", "  ", [], {}, "abc", "1.5", "-1", 0, -1, 1.5, true, false]) {
    const r = Model.parseStatus(JSON.stringify({ schema_version: bad, connected: true }));
    check(`schema_version ${JSON.stringify(bad)} is too new`, [r.ok, r.schemaTooNew, r.schemaVersion], [false, true, 0]);
  }
  check("schema_version 3 keeps its number", parse({ schema_version: 3 }).schemaVersion, 3);
  check("numeric string \"2\" accepted", parse({ schema_version: "2", connected: false }).schemaVersion, 2);
  check("numeric string \" 1 \" accepted", parse({ schema_version: " 1 ", connected: false }).schemaVersion, 1);
  check("schemaVersionOf", [1, 2, "7", "x", 2.5, 0].map(Model.schemaVersionOf), [1, 2, 7, null, null, null]);
  check("Empty-string message", parse({ schema_version: "", connected: true }).lastError, "sony daemon speaks status schema \"\", this panel reads 2");
  check("Unknown schema empty state text", Model.emptyStateText(parse({ schema_version: [] })).includes("an unknown status schema"), true);
});

suite("Suite 21: Custom EQ Chip", () => {
  const s = full();
  check("Custom chip with known values sends the full form", Model.buildCommand("eqPreset", "custom", s), {
    args: ["eq", "custom", "2", "1", "-1", "3", "0", "4"],
    pending: { eqPreset: "custom", eqCustomBands: [2, 1, -1, 3, 0], clearBass: 4 }, feature: "eq_preset"
  });
  check("Custom chip without bands sends the bare form", Model.buildCommand("eqPreset", "custom", Object.assign({}, s, { eqCustomBands: null })).args, ["eq", "custom"]);
  check("Custom chip without Clear Bass sends the bare form", Model.buildCommand("eqPreset", "custom", Object.assign({}, s, { clearBass: null })).args, ["eq", "custom"]);
  const noCustom = Object.assign({}, s, { supported: Object.assign({}, s.supported, { eq_custom: false }) });
  check("Custom chip without eq_custom sends the bare form", Model.buildCommand("eqPreset", "custom", noCustom).args, ["eq", "custom"]);
  check("Custom chip refused when not in eq_presets", Model.buildCommand("eqPreset", "custom", Object.assign({}, s, { eqPresets: ["off", "bass"] })).error, "Not supported by this headset");
});

suite("Suite 22: Schema 1 Daemon Verbs", () => {
  const v1 = Model.parseStatus(fixture("status_connected_custom_eq.json"));
  const err = "Needs a newer sony-headphones daemon";
  check("v1 noise", Model.buildCommand("noise", "off", v1).args, ["noise", "off"]);
  check("v1 ambient-level", Model.buildCommand("ambientLevel", 5, v1).args, ["ambient-level", "5"]);
  check("v1 eq preset", Model.buildCommand("eqPreset", "bright", v1).args, ["eq", "bright"]);
  check("v1 eq band uses the full form", Model.buildCommand("eqBand", { index: 0, level: 1 }, v1).args.length, 8);
  check("v1 custom chip with values uses the full form", Model.buildCommand("eqPreset", "custom", v1).args.length, 8);
  check("v1 bare eq custom refused", Model.buildCommand("eqPreset", "custom", Object.assign({}, v1, { eqCustomBands: null })).error, err);
  check("v1 dsee / speak-to-chat / multipoint / ear-detect",
    ["dsee", "speakToChat", "multipoint", "earDetection"].map((a) => Model.buildCommand(a, true, v1).args[0]),
    ["dsee", "speak-to-chat", "multipoint", "ear-detect"]);
  check("v1 focus-voice refused", Model.buildCommand("focusVoice", true, Object.assign({}, v1, { focusOnVoice: false })).error, err);
  check("v1 speak-to-chat-config refused", Model.buildCommand("stcTimeout", "off", Object.assign({}, v1, { stcSensitivity: "auto", stcTimeout: "short" })).error, err);
  check("v1 user preset refused", Model.buildCommand("eqPreset", "user1", Object.assign({}, v1, { eqPresets: ["off", "user1"] })).error, err);
  check("isV1Command", [["noise", "anc"], ["eq", "custom"], ["eq", "user2"], ["volume", "3"], ["source", "AA:BB:CC:DD:EE:FF"]].map(Model.isV1Command), [true, false, false, false, false]);
  check("v1 hides v2-only targets", ["focusVoice", "stcSensitivity", "stcTimeout"].map((id) => Model.targetState(v1, id)), ["hidden", "hidden", "hidden"]);
  check("v1 cursor sections", ids(Model.cursorSections(v1, { speakToChat: true })).filter((i) => ["focusVoice", "speakToChat", "stcSensitivity", "stcTimeout"].includes(i)), ["speakToChat"]);
  check("v2 keeps them", ["focusVoice", "stcSensitivity", "stcTimeout"].map((id) => Model.targetState(full(), id)), ["enabled", "enabled", "enabled"]);
});

suite("Suite 23: Target Table", () => {
  const s = full();
  check("Every ordered target is in the table", Model.TARGET_ORDER.every((id) => !!Model.TARGETS[id]), true);
  check("Every table target is ordered", Object.keys(Model.TARGETS).every((id) => Model.TARGET_ORDER.includes(id)), true);
  check("Every action is known to buildCommand", Object.values(Model.TARGETS).filter((t) => t.action).every((t) => Model.buildCommand(t.action, undefined, s).error !== "Unknown action " + t.action), true);
  check("Choices with labels", Model.targetChoices(s, "ncAmbButton")[1], { value: "nc_ambient", label: "NC·AMB", tooltip: "Noise Cancelling → Ambient Sound" });
  check("Choices without tooltips", Model.targetChoices(s, "noise").map((c) => c.label + "|" + c.tooltip), ["ANC|", "Ambient|", "Off|"]);
  check("Header activation", Model.targetActivation(s, "speakToChatHeader", 0), { expand: "speakToChat" });
  check("Toggle activation flips", Model.targetActivation(s, "dsee", 0), { action: "dsee", value: false });
  check("Chip activation", Model.targetActivation(s, "autoPowerOff", 2), { action: "autoPowerOff", value: "15min" });
  check("Chip activation out of range", Model.targetActivation(s, "autoPowerOff", 99), null);
  const mp = Model.parseStatus(fixture("status_v2_multipoint.json"));
  check("Device activation switches source", Model.targetActivation(mp, "devices", 0), { action: "source", value: "F0:1D:BC:AA:00:01" });
  check("Active device activation does nothing", Model.targetActivation(mp, "devices", 1), null);
  check("Disabled target activation does nothing", Model.targetActivation(Object.assign({}, s, { dseeExtreme: null }), "dsee", 0), null);
  check("Unknown target", [Model.targetActivation(s, "bogus", 0), Model.targetState(s, "bogus")], [null, "hidden"]);
  check("Adjustment", Model.targetAdjustment(s, "volume", 0, 1), { action: "volume", value: 13 });
  check("Band adjustment", Model.targetAdjustment(s, "eqBands", 5, -1), { action: "eqBand", value: { index: 5, level: 3 } });
  check("Adjustment on a disabled target", Model.targetAdjustment(Object.assign({}, s, { clearBass: null }), "eqBands", 0, 1), null);
  check("Missing extra value disables", Model.targetState(Object.assign({}, s, { stcTimeout: null }), "stcSensitivity"), "disabled");
  const init = Model.parseStatus(fixture("status_v2_initializing.json"));
  check("Initializing: targets disabled, headers enabled", [Model.targetState(init, "dsee"), Model.targetState(init, "sound")], ["disabled", "enabled"]);
  check("Minimal: headers hidden", Model.targetState(Model.parseStatus(fixture("status_v2_minimal.json")), "headset"), "hidden");
});

suite("Suite 24: Bar Display Setting", () => {
  const s = full();
  const charging = Object.assign({}, s, { batteryCharging: true });
  const off = Model.parseStatus(fixture("status_v2_disconnected.json"));
  check("Default is icon only", [Model.showBarPercentage({}), Model.showBarPercentage(null), Model.showBarPercentage(undefined)], [false, false, false]);
  check("Boolean values", [Model.showBarPercentage({ showPercentage: false }), Model.showBarPercentage({ showPercentage: true })], [false, true]);
  check("Strings from omarchy bar set", ["false", "False", " off ", "0", "no", "true", "on", "1", "yes"].map((v) => Model.showBarPercentage({ showPercentage: v })),
    [false, false, false, false, false, true, true, true, true]);
  check("Numbers", [Model.showBarPercentage({ showPercentage: 0 }), Model.showBarPercentage({ showPercentage: 1 })], [false, true]);
  check("Unreadable value falls back to the default", [Model.showBarPercentage({ showPercentage: "maybe" }), Model.showBarPercentage({ showPercentage: null })], [false, false]);

  check("Percentage mode text", Model.barText(s, true, false), "80%");
  check("Icon-only mode has no text", Model.barText(s, false, false), "");
  check("Vertical bar has no text", Model.barText(s, true, true), "");
  check("Disconnected percentage mode shows a dash", Model.barText(off, true, false), "—");
  check("Bolt with the percentage while charging", Model.barChargingHint(charging, true, false), true);
  check("No bolt in icon-only mode", Model.barChargingHint(charging, false, false), false);
  check("No bolt on a vertical bar", Model.barChargingHint(charging, true, true), false);
  check("No bolt while not charging", Model.barChargingHint(s, true, false), false);
  // Icon-only mode keeps the battery in the tooltip and the icon tint inputs.
  check("Tooltip keeps the battery", Model.barTooltip(charging).split("\n").pop(), "Battery 80% (charging)");
  check("Charging state for the icon tint", Model.isCharging(charging), true);

  check("Entry keeps other fields and puts id first",
    JSON.stringify(Model.settingsEntry("io.github.androydified.omasony", { id: "x", statusPath: "/tmp/s.json" }, { showPercentage: true })),
    JSON.stringify({ id: "io.github.androydified.omasony", statusPath: "/tmp/s.json", showPercentage: true }));
  check("Entry ignores an id in the new values", Model.settingsEntry("a", null, { id: "b", showPercentage: true }), { id: "a", showPercentage: true });

  check("Setting target", [Model.isSettingTarget("barPercentage"), Model.isSettingTarget("dsee"), Model.isSettingTarget("toString")], [true, false, false]);
  check("Activation flips the default", Model.settingActivation({}, "barPercentage"), { showPercentage: true });
  check("Activation flips a string value", Model.settingActivation({ showPercentage: "true" }, "barPercentage"), { showPercentage: false });
  check("Activation of an unknown target", Model.settingActivation({}, "dsee"), null);

  check("Popup cursor ends with the setting", ids(Model.panelCursorSections(s, {})).slice(-2), ["multipointHeader", "barPercentage"]);
  check("Status cursor list is unchanged", ids(Model.cursorSections(s, {})).includes("barPercentage"), false);
  check("Setting reachable while disconnected", Model.panelCursorSections(off, {}), [{ id: "barPercentage", kind: "single", count: 1 }]);
  check("Setting reachable without a status file", ids(Model.panelCursorSections(Model.parseStatus(""), {})), ["barPercentage"]);
  const down = Model.cursorMove({ section: "multipointHeader", index: 0 }, 0, 1, Model.panelCursorSections(s, {}));
  check("Cursor moves from the last section to the setting", [down.section, down.index, down.action], ["barPercentage", 0, null]);
  const side = Model.cursorMove({ section: "barPercentage", index: 0 }, 1, 0, Model.panelCursorSections(s, {}));
  check("h/l on the setting does nothing", [side.section, side.action], ["barPercentage", null]);
});

suite("Suite 25: Touch Panel and Quick Access", () => {
  const s = full();
  check("Parsed fields", [s.touchPanel, s.quickAccessDoubleTap, s.quickAccessTripleTap, s.quickAccessDoubleTapOptions, s.quickAccessTripleTapOptions],
    [true, "off", "spotify", ["off", "spotify"], ["off", "spotify"]]);
  check("Supported keys", [s.supported.touch_panel, s.supported.quick_access], [true, true]);
  const withRaw = (extra) => Model.parseStatus(JSON.stringify(Object.assign(JSON.parse(fixture("status_v2_full.json")), extra)));
  const other = withRaw({ quick_access_double_tap: "other", quick_access_triple_tap: "SPOTIFY", quick_access_double_tap_options: ["spotify", "other", "bogus", "off"] });
  check("other is kept, case folded, lists drop other and unknown values", [other.quickAccessDoubleTap, other.quickAccessTripleTap, other.quickAccessDoubleTapOptions],
    ["other", "spotify", ["spotify", "off"]]);
  check("Unknown value reads as null", withRaw({ quick_access_triple_tap: "tidal", touch_panel: "yes" }).quickAccessTripleTap, null);
  check("Non-bool touch panel reads as null", withRaw({ touch_panel: "yes" }).touchPanel, null);

  // A daemon older than the keys: no supported key means false, fields null.
  const old = JSON.parse(fixture("status_v2_full.json"));
  delete old.supported.touch_panel;
  delete old.supported.quick_access;
  const o = parse(old);
  check("Missing supported keys read as false", [o.supported.touch_panel, o.supported.quick_access], [false, false]);
  check("Their fields read as null", [o.touchPanel, o.quickAccessDoubleTap, o.quickAccessTripleTapOptions], [null, null, null]);
  check("Targets hidden for an older daemon", ["touchPanel", "quickAccessDouble", "quickAccessTriple"].map((id) => Model.targetState(o, id)),
    ["hidden", "hidden", "hidden"]);
  check("Supported false hides them", ["touchPanel", "quickAccessDouble"].map((id) => Model.targetState(Model.parseStatus(fixture("status_v2_minimal.json")), id)),
    ["hidden", "hidden"]);
  check("Fallback list hides them", ["touchPanel", "quickAccessTriple"].map((id) => Model.targetState(Model.parseStatus(fixture("status_v2_fallback.json")), id)),
    ["hidden", "hidden"]);
  const init = Model.parseStatus(fixture("status_v2_initializing.json"));
  check("Reading settings: disabled", ["touchPanel", "quickAccessDouble"].map((id) => Model.targetState(init, id)), ["disabled", "disabled"]);
  check("Supported with a null value: disabled", Model.targetState(Object.assign({}, s, { touchPanel: null }), "touchPanel"), "disabled");
  check("Headset section shows for touch panel alone",
    Model.sectionVisible(withRaw({ supported: Object.assign({}, JSON.parse(fixture("status_v2_minimal.json")).supported, { touch_panel: true }) }), "headset"), true);

  check("Touch panel command", Model.buildCommand("touchPanel", false, s), { args: ["touch-panel", "off"], pending: { touchPanel: false }, feature: "touch_panel" });
  check("Touch panel needs a bool", Model.buildCommand("touchPanel", "off", s).error, "Invalid value");
  check("Toggle activation flips the touch panel", Model.targetActivation(s, "touchPanel", 0), { action: "touchPanel", value: false });
  check("Double tap command", Model.buildCommand("quickAccessDouble", "spotify", s),
    { args: ["quick-access", "double", "spotify"], pending: { quickAccessDoubleTap: "spotify" }, feature: "quick_access" });
  check("Triple tap command", Model.buildCommand("quickAccessTriple", "off", s),
    { args: ["quick-access", "triple", "off"], pending: { quickAccessTripleTap: "off" }, feature: "quick_access" });
  check("other cannot be sent", Model.buildCommand("quickAccessDouble", "other", s).error, "Not supported by this headset");
  const onlyOff = Object.assign({}, s, { quickAccessTripleTapOptions: ["off"] });
  check("Value outside the press's list is refused", Model.buildCommand("quickAccessTriple", "spotify", onlyOff).error, "Not supported by this headset");
  check("The other press keeps its own list", Model.buildCommand("quickAccessDouble", "spotify", onlyOff).args, ["quick-access", "double", "spotify"]);
  check("Unsupported feature", Model.buildCommand("quickAccessDouble", "off", o).error, "Not supported by this headset");
  check("Schema 1 cannot send the new verbs", Model.isV1Command(["touch-panel", "on"]) || Model.isV1Command(["quick-access", "double", "off"]), false);

  check("Options from the lists", [Model.quickAccessOptions(onlyOff, "double"), Model.quickAccessOptions(onlyOff, "triple")], [["off", "spotify"], ["off"]]);
  check("Options without a list", Model.quickAccessOptions(Object.assign({}, s, { quickAccessDoubleTapOptions: null }), "double"), ["off", "spotify"]);
  check("Chips", Model.targetChoices(s, "quickAccessTriple").map((c) => c.value + "|" + c.label), ["off|Off", "spotify|Spotify"]);
  check("Chip activation", Model.targetActivation(s, "quickAccessTriple", 0), { action: "quickAccessTriple", value: "off" });
  check("other selects no chip", Model.cursorSections(Object.assign({}, s, { quickAccessDoubleTap: "other" }), { headset: true })
    .find((x) => x.id === "quickAccessDouble").preferred, -1);
  check("Labels", ["off", "spotify", "other", null].map(Model.quickAccessLabel), ["Off", "Spotify", "Other", "Unknown"]);
  check("No other text for named values", Model.quickAccessOtherText(s), "");
  check("other text for one press", Model.quickAccessOtherText(Object.assign({}, s, { quickAccessTripleTap: "other" })), "Triple tap uses a function this panel does not name.");
  check("other text for both presses", Model.quickAccessOtherText(Object.assign({}, s, { quickAccessDoubleTap: "other", quickAccessTripleTap: "other" })),
    "Double tap and Triple tap use functions this panel does not name.");

  let q = Model.enqueueCommand([], { args: ["quick-access", "double", "spotify"], id: 1 });
  q = Model.enqueueCommand(q.queue, { args: ["quick-access", "triple", "off"], id: 2 });
  check("Presses queue separately", [q.queue.map((i) => i.id), q.dropped], [[1, 2], []]);
  q = Model.enqueueCommand(q.queue, { args: ["quick-access", "double", "off"], id: 3 });
  check("Same press replaces the queued one", [q.queue.map((i) => i.id), q.dropped], [[2, 3], [1]]);

  const pend = Model.addPending({}, { quickAccessDoubleTap: "spotify" }, 7);
  check("Pending double tap shows at once", Model.applyPending(s, pend).quickAccessDoubleTap, "spotify");
  check("Pending clears when the daemon reports it", Model.reconcilePending(pend, Object.assign({}, s, { quickAccessDoubleTap: "spotify" })), {});
});

console.log(`\nSummary: ${passed} passed, ${failed} failed`);
if (failed > 0) {
  if (typeof Deno !== "undefined") Deno.exit(1);
  if (typeof process !== "undefined") process.exit(1);
}
