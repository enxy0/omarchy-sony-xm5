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
  check("SUPPORTED_SCHEMA is 3", Model.SUPPORTED_SCHEMA, 3);
  const r2 = parse({ schema_version: 1, connected: true });
  check("Schema 1 accepted", [r2.ok, r2.schemaTooNew, r2.schemaVersion], [true, false, 1]);
  const r3 = parse({ schema_version: 2, connected: true });
  check("Schema 2 accepted", [r3.ok, r3.schemaTooNew, r3.schemaVersion], [true, false, 2]);
  const r3b = parse({ schema_version: 3, connected: false, primary: null, headsets: [] });
  check("Schema 3 accepted", [r3b.ok, r3b.schemaTooNew, r3b.schemaVersion], [true, false, 3]);
  const r4 = parse({ schema_version: 4, connected: true, device_name: "x" });
  check("Schema 4 is too new", [r4.ok, r4.schemaTooNew, r4.schemaVersion, r4.connected], [false, true, 4, false]);
  check("Schema 4 message", r4.lastError, "sony daemon speaks status schema 4, this panel reads 3");
  check("Schema 4: no headsets", [r4.headsets, r4.primary], [[], null]);
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
    battery: true, battery_lr: false, battery_case: false, noise_control: true, eq_preset: true,
    eq_custom: true, dsee: true, speak_to_chat: true, voice_guidance: false, voice_guidance_volume: false,
    auto_power_off: false, nc_amb_button: false, button_function: false, touch_function: false,
    ear_detection: true, worn: false, volume: false, connection_quality: false, codec: true,
    multipoint: true, devices: false, source_switch: false, touch_panel: false, sidetone: false,
    quick_access: false
  });
  check("v1 is one headphones entry", [r.headsets.length, r.id, r.model, r.kind, r.primary], [1, "unknown", "wh-1000xm5", "headphones", "unknown"]);
  check("v1 command has no --headset", Model.buildCommand("noise", "off", r).args, ["noise", "off"]);
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
    check(`${f}: tooltip`, Model.barTooltip(r), "Sony headphones: disconnected");
    check(`${f}: hero meta`, Model.heroMeta(r), "Disconnected");
    check(`${f}: bar has no headset`, Model.barNoHeadset(r), true);
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
  const v3Keys = ["battery_lr", "battery_case", "touch_function", "sidetone"];
  check("Every v2 feature supported", Model.FEATURE_KEYS.filter((k) => !v3Keys.includes(k)).every((k) => s.supported[k] === true), true);
  check("v3 keys read as false", v3Keys.map((k) => s.supported[k]), [false, false, false, false]);
  check("One entry built from the top level", [s.headsets.length, s.id, s.model, s.kind, s.connectedSince, s.primary],
    [1, "AC:80:0A:12:34:56", "wh-1000xm5", "headphones", s.lastUpdated, "AC:80:0A:12:34:56"]);
  check("Earbuds fields null", [s.batteryLeft, s.batteryCase, s.touchFunctionLeft, s.wornLeft, s.sidetone], [null, null, null, null, null]);
  check("v2 command has no --headset", Model.buildCommand("volume", 3, s).args, ["volume", "3"]);
  check("v2 command still names its headset", Model.buildCommand("volume", 3, s).headset, "AC:80:0A:12:34:56");
  check("v2: new targets hidden", ["batteryLr", "batteryCase", "touchFunctionLeft", "touchFunctionRight", "sidetone"].map((id) => Model.targetState(s, id)),
    ["hidden", "hidden", "hidden", "hidden", "hidden"]);
  check("v2: no switcher", [Model.switcherVisible(s), Model.switcherTabs(s, s.id)], [false, []]);
  check("Noise", [s.noiseMode, s.ambientSoundLevel, s.focusOnVoice], ["ambient", 14, true]);
  check("EQ", [s.eqPreset, s.eqPresets.length, s.eqCustomBands, s.clearBass], ["custom", 10, [2, 1, -1, 3, 0], 4]);
  check("Sound", [s.dseeExtreme, s.connectionQuality, s.codec, s.volume], [true, "sound", "LDAC", 12]);
  check("Speak-to-Chat", [s.speakToChat, s.stcSensitivity, s.stcTimeout], [true, "auto", "standard"]);
  check("Voice guidance", [s.voiceGuidance, s.voiceGuidanceVolume], [true, 0]);
  check("Power", [s.autoPowerOff, s.autoPowerOffOptions.length], ["when_taken_off", 2]);
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
  check("Tooltip", Model.barTooltip(s), "WH-1000XM5\nAmbient Sound · LDAC\nBattery 80%");
  check("Bar has a headset", Model.barNoHeadset(s), false);
  check("Bar keeps the button without status", Model.barNoHeadset(null), false);
  check("Bar keeps the button on a status error", Model.barNoHeadset({ ok: false, connected: false }), false);
  check("Hero meta", Model.heroMeta(s), "Connected · LDAC · On head");
  check("Footer", Model.footerLines(s), ["WH-1000XM5 · firmware 2.3.1 · AC:80:0A:12:34:56", "Old daemon (status schema 2)"]);
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
  check("Auto power off grid preferred", [byId.autoPowerOff.count, byId.autoPowerOff.preferred], [2, 1]);
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
  check("Footer without firmware", Model.footerLines(s), ["WH-1000XM5 · AC:80:0A:12:34:56", "Old daemon (status schema 2)"]);
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
  check("Tooltip", Model.barTooltip(s), "WH-1000XM5: reading headset settings…");
  check("Hero meta", Model.heroMeta(s), "Reading settings");
  check("Commands refused", Model.buildCommand("noise", "anc", s), { error: "Reading headset settings…" });
  check("Cursor still lists headers", ids(Model.cursorSections(s, {})),
    ["noise", "ambient", "focusVoice", "sound", "customEq", "speakToChatHeader", "headset", "multipointHeader"]);
  const known = parse({ schema_version: 2, connected: true, supported: null, battery_level: 40, device_name: "x" });
  check("Battery shown while reading", Model.barBatteryLevel(known), 40);
});

suite("Suite 10: v2 Fallback Fixture", () => {
  const s = Model.parseStatus(fixture("status_v2_fallback.json"));
  check("supported_source fallback", s.supportedSource, "fallback");
  check("Footer hint", Model.footerLines(s), ["WH-1000XM5 · firmware 2.3.1 · AC:80:0A:12:34:56", "Limited: headset did not report its features", "Old daemon (status schema 2)"]);
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
  check("Battery hidden: no battery line", Model.barTooltip(noBattery), "WH-1000XM5");
});

suite("Suite 12: Command Building", () => {
  const s = full();
  const cmd = (a, v, st) => Model.buildCommand(a, v, st || s);
  const hid = "AC:80:0A:12:34:56";
  check("noise", cmd("noise", "anc"), { args: ["noise", "anc"], pending: { noiseMode: "anc" }, feature: "noise_control", headset: hid });
  check("noise wind refused", cmd("noise", "wind").error, "Not supported by this headset");
  check("ambient-level sets ambient mode", cmd("ambientLevel", 5), { args: ["ambient-level", "5"], pending: { ambientSoundLevel: 5, noiseMode: "ambient" }, feature: "noise_control", headset: hid });
  check("ambient-level 0 refused", cmd("ambientLevel", 0).error, "Value out of range");
  check("ambient-level 21 refused", cmd("ambientLevel", 21).error, "Value out of range");
  check("ambient-level fraction refused", cmd("ambientLevel", 2.5).error, "Value out of range");
  check("focus-voice", cmd("focusVoice", false).args, ["focus-voice", "off"]);
  check("focus-voice non-bool refused", cmd("focusVoice", "on").error, "Invalid value");
  check("eq preset", cmd("eqPreset", "bass").args, ["eq", "bass"]);
  check("eq preset not in list", cmd("eqPreset", "user2").error, "Not supported by this headset");
  check("eq custom", cmd("eqCustom", { bands: [1, 2, 3, 4, 5], clearBass: -3 }), {
    args: ["eq", "custom", "1", "2", "3", "4", "5", "-3"],
    pending: { eqPreset: "custom", eqCustomBands: [1, 2, 3, 4, 5], clearBass: -3 }, feature: "eq_custom", headset: hid
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
  check("stc sensitivity keeps timeout", cmd("stcSensitivity", "low"), { args: ["speak-to-chat-config", "low", "standard"], pending: { stcSensitivity: "low", stcTimeout: "standard" }, feature: "speak_to_chat", headset: hid });
  check("stc timeout keeps sensitivity", cmd("stcTimeout", "off").args, ["speak-to-chat-config", "auto", "off"]);
  check("stc config needs other value", cmd("stcTimeout", "off", Object.assign({}, s, { stcSensitivity: null })).error, "Speak-to-Chat settings are not known yet");
  check("stc bad value", cmd("stcSensitivity", "max").error, "Invalid value");
  check("voice-guidance", cmd("voiceGuidance", false).args, ["voice-guidance", "off"]);
  check("voice-guidance-volume", cmd("voiceGuidanceVolume", -2).args, ["voice-guidance-volume", "-2"]);
  check("voice-guidance-volume 3 refused", cmd("voiceGuidanceVolume", 3).error, "Value out of range");
  check("auto-power-off", cmd("autoPowerOff", "off").args, ["auto-power-off", "off"]);
  check("auto-power-off timer not offered with wearing detection", cmd("autoPowerOff", "60min").error, "Not supported by this headset");
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
  check("auto power off list from status", Model.autoPowerOffOptions(s), ["off", "when_taken_off"]);
  check("auto power off without list", Model.autoPowerOffOptions(Object.assign({}, s, { autoPowerOffOptions: null, autoPowerOff: "30min" })), ["off", "5min", "15min", "30min", "60min", "180min"]);
  check("auto power off without list keeps current when_taken_off", Model.autoPowerOffOptions(Object.assign({}, s, { autoPowerOffOptions: null })).includes("when_taken_off"), true);
  check("button functions without list", Model.buttonFunctionOptions(Object.assign({}, s, { buttonFunctions: null })), Model.BUTTON_FUNCTION_VALUES);
  check("eq presets with user presets", Model.eqPresetOptions(parse({ schema_version: 2, connected: true, supported: null, eq_presets: ["off", "user1", "unknown", "OFF"] })), ["off", "user1"]);
  check("empty eq presets list uses v1 list", Model.eqPresetOptions({ eqPresets: [] }), Model.EQ_PRESETS);
});

suite("Suite 14: Optimistic Values", () => {
  const s = full();
  const hid = s.id;
  let pending = {};
  pending = Model.addPending(pending, { noiseMode: "anc" }, 1, hid);
  pending = Model.addPending(pending, { volume: 20 }, 2, hid);
  check("hasPending", Model.hasPending(pending), true);
  check("Entries are keyed by headset and field", Object.keys(pending), [hid + "|noiseMode", hid + "|volume"]);
  const eff = Model.applyPending(s, pending);
  check("applyPending overrides", [eff.noiseMode, eff.volume, eff.dseeExtreme], ["anc", 20, true]);
  check("applyPending patches the headset entry", [eff.headsets[0].noiseMode, eff.id], ["anc", hid]);
  check("applyPending does not mutate", [s.noiseMode, s.headsets[0].noiseMode], ["ambient", "ambient"]);
  check("applyPending without pending returns same object", Model.applyPending(s, {}) === s, true);
  check("dropPending by id", Object.keys(Model.dropPending(pending, 1)), [hid + "|volume"]);
  const confirmed = Object.assign({}, s, { noiseMode: "anc" });
  check("reconcile drops confirmed", Object.keys(Model.reconcilePending(pending, confirmed)), [hid + "|volume"]);
  check("reconcile keeps object when nothing matches", Model.reconcilePending(pending, s) === pending, true);
  const disc = Model.parseStatus(fixture("status_v2_disconnected.json"));
  check("reconcile drops the values of a headset that left", Model.reconcilePending(pending, disc), {});
  check("reconcile keeps pending while the file is unreadable", Model.reconcilePending(pending, Model.parseStatus("")) === pending, true);
  check("applyPending ignores disconnected", Model.applyPending(disc, pending) === disc, true);
  const bands = Model.addPending({}, { eqCustomBands: [1, 2, 3, 4, 5] }, 3, hid);
  check("reconcile compares arrays by value", Model.hasPending(Model.reconcilePending(bands, Object.assign({}, s, { eqCustomBands: [1, 2, 3, 4, 5] }))), false);
  check("newer id owns the field", Model.dropPending(Model.addPending(pending, { noiseMode: "off" }, 4, hid), 1)[hid + "|noiseMode"].value, "off");
  // Without a headset id an entry is for the primary headset (tests/e2e_model_cli.js).
  const plain = Model.addPending({}, { volume: 5 }, 9);
  check("Headset-less entry applies to the primary", Model.applyPending(s, plain).volume, 5);
  check("Headset-less entry reconciles against the primary", Model.reconcilePending(plain, Object.assign({}, s, { volume: 5 })), {});
  check("Headset-less entry survives a disconnect", Model.reconcilePending(plain, disc) === plain, true);
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
  check("batteryIcon charging", Model.batteryIcon(50, true), "\u{F0084}");
  check("batteryIcon unknown", [Model.batteryIcon(null, false), Model.batteryIcon(-1, false)], ["\u{F0091}", "\u{F0091}"]);
  check("batteryIcon levels", [100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 4].map((l) => Model.batteryIcon(l, false).codePointAt(0).toString(16)),
    ["f0079", "f0082", "f0081", "f0080", "f007f", "f007e", "f007d", "f007c", "f007b", "f007a", "f008e"]);
  // Codepoints pinned against the JetBrainsMono Nerd Font names.
  check("noiseModeIcon codepoints", ["anc", "ambient", "wind", "off", null].map((m) => Model.noiseModeIcon(m).codePointAt(0).toString(16)),
    ["f0a45", "f07c5", "f059d", "f07ce", "f0a45"]);
  check("headset and bolt glyphs", [Model.GLYPH_HEADPHONES, Model.GLYPH_EARBUDS, Model.GLYPH_BOLT].map((g) => g.codePointAt(0).toString(16)),
    ["f02cb", "f184f", "f0241"]);
  check("cycleNoiseMode", ["anc", "ambient", "off", "wind", null].map(Model.cycleNoiseMode), ["ambient", "off", "anc", "anc", "anc"]);
  const elided = Model.elideError("Error: " + "a".repeat(200));
  check("elideError length <= 140", elided.length <= 140, true);
  check("elideError ends with ellipsis", elided.endsWith("…"), true);
  check("deviceTitle without a headset", Model.deviceTitle(Model.defaultStatus()), "Sony headset");
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
  check("Empty-string message", parse({ schema_version: "", connected: true }).lastError, "sony daemon speaks status schema \"\", this panel reads 3");
  check("Unknown schema empty state text", Model.emptyStateText(parse({ schema_version: [] })).includes("an unknown status schema"), true);
});

suite("Suite 21: Custom EQ Chip", () => {
  const s = full();
  check("Custom chip with known values sends the full form", Model.buildCommand("eqPreset", "custom", s), {
    args: ["eq", "custom", "2", "1", "-1", "3", "0", "4"],
    pending: { eqPreset: "custom", eqCustomBands: [2, 1, -1, 3, 0], clearBass: 4 }, feature: "eq_preset", headset: s.id
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
  check("Chip activation", Model.targetActivation(s, "autoPowerOff", 0), { action: "autoPowerOff", value: "off" });
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

suite("Suite 24: Bar Icon", () => {
  const s = full();
  const charging = Object.assign({}, s, { batteryCharging: true });
  check("Tooltip keeps the battery", Model.barTooltip(charging).split("\n").pop(), "Battery 80% (charging)");
  check("Charging state for the icon tint", Model.isCharging(charging), true);
  check("Popup cursor ends with the multipoint header", ids(Model.panelCursorSections(s, {})).slice(-1), ["multipointHeader"]);
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

  check("Touch panel command", Model.buildCommand("touchPanel", false, s), { args: ["touch-panel", "off"], pending: { touchPanel: false }, feature: "touch_panel", headset: s.id });
  check("Touch panel needs a bool", Model.buildCommand("touchPanel", "off", s).error, "Invalid value");
  check("Toggle activation flips the touch panel", Model.targetActivation(s, "touchPanel", 0), { action: "touchPanel", value: false });
  check("Double tap command", Model.buildCommand("quickAccessDouble", "spotify", s),
    { args: ["quick-access", "double", "spotify"], pending: { quickAccessDoubleTap: "spotify" }, feature: "quick_access", headset: s.id });
  check("Triple tap command", Model.buildCommand("quickAccessTriple", "off", s),
    { args: ["quick-access", "triple", "off"], pending: { quickAccessTripleTap: "off" }, feature: "quick_access", headset: s.id });
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

  const pend = Model.addPending({}, { quickAccessDoubleTap: "spotify" }, 7, s.id);
  check("Pending double tap shows at once", Model.applyPending(s, pend).quickAccessDoubleTap, "spotify");
  check("Pending clears when the daemon reports it", Model.reconcilePending(pend, Object.assign({}, s, { quickAccessDoubleTap: "spotify" })), {});
});

const WH = "AC:80:0A:12:34:56";
const WF = "AC:80:0A:65:43:21";
const v3 = (name) => Model.parseStatus(fixture(name));
const rawFixture = (name) => JSON.parse(fixture(name));

suite("Suite 26: v3 WH-1000XM5 Fixture", () => {
  const s = v3("status_v3_wh.json");
  check("ok, connected, schema 3", [s.ok, s.connected, s.schemaVersion], [true, true, 3]);
  check("Identity", [s.id, s.model, s.kind, s.address, s.deviceName, s.modelName, s.firmwareVersion, s.primary],
    [WH, "wh-1000xm5", "headphones", WH, "WH-1000XM5", "WH-1000XM5", "2.5.1", WH]);
  check("connected_since", s.connectedSince, 1760438380);
  check("One headset, no switcher", [s.headsets.length, Model.switcherVisible(s)], [1, false]);
  check("Headphones battery", [s.batteryLevel, s.batteryCharging, Model.barBattery(s), Model.barCharging(s)], [53, false, 53, false]);
  check("Earbuds fields null", [s.batteryLeft, s.batteryRight, s.batteryCase, s.wornLeft, s.touchFunctionLeft, s.sidetone], [null, null, null, null, null, null]);
  check("v3 keys", [s.supported.battery_lr, s.supported.battery_case, s.supported.touch_function, s.supported.sidetone], [false, false, false, false]);
  check("Tooltip", Model.barTooltip(s), "WH-1000XM5\nOff · LDAC\nBattery 53%");
  check("Hero meta: worn unsupported", Model.heroMeta(s), "Connected · LDAC");
  check("Footer: no old-daemon line", Model.footerLines(s), ["WH-1000XM5 · firmware 2.5.1 · AC:80:0A:12:34:56"]);
  check("Hero targets", ["battery", "batteryLr", "batteryCase", "wornSides"].map((id) => Model.targetState(s, id)), ["enabled", "hidden", "hidden", "hidden"]);
  check("Earbuds controls hidden", ["touchFunctionLeft", "touchFunctionRight", "sidetone"].map((id) => Model.targetState(s, id)), ["hidden", "hidden", "hidden"]);
  check("Headphones controls shown", ["touchPanel", "quickAccessDouble", "ncAmbButton", "connectionQuality"].map((id) => Model.targetState(s, id)),
    ["enabled", "enabled", "enabled", "enabled"]);
  check("Every command names the headset", Model.buildCommand("noise", "anc", s),
    { args: ["--headset", WH, "noise", "anc"], pending: { noiseMode: "anc" }, feature: "noise_control", headset: WH });
  check("Verb args without the selector", [Model.commandVerbArgs(Model.buildCommand("volume", 3, s).args), Model.commandHeadset(Model.buildCommand("volume", 3, s).args)],
    [["volume", "3"], WH]);
  check("touch-function refused on headphones", Model.buildCommand("touchFunctionLeft", "none", s).error, "Not supported by this headset");
  check("sidetone refused on headphones", Model.buildCommand("sidetone", true, s).error, "Not supported by this headset");
  check("Glyph", Model.headsetGlyph(s.kind), "\u{F02CB}");
  check("Icon tint input", [Model.barBatteryLevel(s), Model.isCharging(s)], [53, false]);
  check("Expanded headset section", ids(Model.cursorSections(s, { headset: true })).filter((i) => Model.TARGETS[i].section === "headset"),
    ["headset", "volume", "voiceGuidance", "autoPowerOff", "ncAmbButton", "quickAccessDouble", "quickAccessTriple", "earDetection", "touchPanel"]);
  check("Popup cursor has no switcher", ids(Model.panelCursorSections(s, {}))[0], "noise");
});

suite("Suite 27: v3 WF-1000XM5 Fixture", () => {
  const s = v3("status_v3_wf.json");
  check("Identity", [s.id, s.model, s.kind, s.deviceName, s.firmwareVersion, s.primary], [WF, "wf-1000xm5", "earbuds", "WF-1000XM5", "6.1.0", WF]);
  check("Earbuds battery", [s.batteryLeft, s.batteryLeftCharging, s.batteryRight, s.batteryRightCharging, s.batteryCase, s.batteryCaseCharging],
    [70, false, 65, false, 53, false]);
  check("Headphones battery unsupported", [s.batteryLevel, s.batteryCharging, s.supported.battery], [-1, null, false]);
  check("Bar battery is the lower earbud", [Model.barBattery(s), Model.barBatteryLevel(s)], [65, 65]);
  check("Bar not charging", [Model.barCharging(s), Model.isCharging(s)], [false, false]);
  check("Icon tint input", Model.barBatteryLevel(s), 65);
  check("Worn per side", [s.worn, s.wornLeft, s.wornRight], [true, true, true]);
  check("Hero meta", Model.heroMeta(s), "Connected · LDAC · In ears");
  check("Tooltip", Model.barTooltip(s), "WF-1000XM5\nOff · LDAC\nBattery L 70% · R 65% · case 53%");
  check("Hero battery parts", Model.earbudBatteryParts(s), [
    { side: "L", text: "L 70%", low: false, charging: false },
    { side: "R", text: "R 65%", low: false, charging: false }
  ]);
  check("Case text", [Model.caseBatteryText(s), Model.caseCharging(s)], ["Case 53%", false]);
  check("Hero targets", ["battery", "batteryLr", "batteryCase", "wornSides"].map((id) => Model.targetState(s, id)), ["hidden", "enabled", "enabled", "enabled"]);
  check("Touch functions", [s.touchFunctionLeft, s.touchFunctionRight, s.touchFunctionLeftOptions, s.touchFunctionRightOptions],
    ["playback_control", "playback_control", ["ambient_sound_control", "playback_control", "none"], ["ambient_sound_control", "playback_control", "none"]]);
  check("Sidetone", s.sidetone, true);
  check("WF hides headphones controls", ["connectionQuality", "ncAmbButton", "buttonFunction", "touchPanel", "quickAccessDouble"].map((id) => Model.targetState(s, id)),
    ["hidden", "hidden", "hidden", "hidden", "hidden"]);
  check("Expanded headset section", ids(Model.cursorSections(s, { headset: true })).filter((i) => Model.TARGETS[i].section === "headset"),
    ["headset", "volume", "voiceGuidance", "voiceGuidanceVolume", "autoPowerOff", "earDetection", "touchFunctionLeft", "touchFunctionRight", "sidetone"]);
  check("Display targets have no cursor", ids(Model.cursorSections(s, { sound: true, headset: true })).some((i) => Model.TARGETS[i].kind === "display"), false);
  const sec = Model.cursorSections(s, { headset: true });
  const left = sec.find((x) => x.id === "touchFunctionLeft");
  check("Left touch row", [left.kind, left.count, left.preferred], ["walk", 3, 1]);
  check("Touch chips", Model.targetChoices(s, "touchFunctionRight").map((c) => c.value + "|" + c.label),
    ["ambient_sound_control|Noise control", "playback_control|Playback", "none|Off"]);
  check("Touch chip tooltip", Model.targetChoices(s, "touchFunctionLeft")[2].tooltip, "Taps do nothing");
  check("touch-function left", Model.buildCommand("touchFunctionLeft", "none", s),
    { args: ["--headset", WF, "touch-function", "left", "none"], pending: { touchFunctionLeft: "none" }, feature: "touch_function", headset: WF });
  check("touch-function right", Model.buildCommand("touchFunctionRight", "ambient_sound_control", s).args,
    ["--headset", WF, "touch-function", "right", "ambient_sound_control"]);
  check("Value outside the side's list refused", Model.buildCommand("touchFunctionLeft", "volume_control", s).error, "Not supported by this headset");
  check("other cannot be sent", Model.buildCommand("touchFunctionLeft", "other", s).error, "Not supported by this headset");
  const ownLists = Object.assign({}, s, { touchFunctionLeftOptions: ["none"], touchFunctionRightOptions: null });
  check("Each side has its own list", [Model.touchFunctionOptions(ownLists, "left"), Model.touchFunctionOptions(ownLists, "right")],
    [["none"], ["ambient_sound_control", "playback_control", "none"]]);
  check("Null list offers the WF-1000XM5 list", Model.buildCommand("touchFunctionRight", "none", ownLists).args.slice(2), ["touch-function", "right", "none"]);
  check("Touch activation", Model.targetActivation(s, "touchFunctionLeft", 0), { action: "touchFunctionLeft", value: "ambient_sound_control" });
  check("sidetone command", Model.buildCommand("sidetone", false, s),
    { args: ["--headset", WF, "sidetone", "off"], pending: { sidetone: false }, feature: "sidetone", headset: WF });
  check("sidetone toggle flips", Model.targetActivation(s, "sidetone", 0), { action: "sidetone", value: false });
  check("sidetone needs a bool", Model.buildCommand("sidetone", "on", s).error, "Invalid value");
  check("Labels", Model.TOUCH_FUNCTION_VALUES.concat(["other", null]).map(Model.touchFunctionLabel),
    ["Noise control", "Playback", "Volume", "Voice assistant", "Google Assistant", "Alexa", "Quick Access", "Off", "Other", "Unknown"]);
  check("No other text", Model.touchFunctionOtherText(s), "");
  check("other text, one side", Model.touchFunctionOtherText(Object.assign({}, s, { touchFunctionLeft: "other" })), "Left earbud uses a function this panel does not name.");
  check("other text, both sides", Model.touchFunctionOtherText(Object.assign({}, s, { touchFunctionLeft: "other", touchFunctionRight: "other" })),
    "Left earbud and Right earbud use functions this panel does not name.");
  check("other selects no chip", Model.cursorSections(Object.assign({}, s, { touchFunctionRight: "other" }), { headset: true })
    .find((x) => x.id === "touchFunctionRight").preferred, -1);
  check("Glyph", Model.headsetGlyph(s.kind), "\u{F184F}");
  check("Headset section shows for touch functions alone", Model.sectionVisible(Object.assign({}, s, {
    supported: Object.assign({}, ...Model.FEATURE_KEYS.map((k) => ({ [k]: false })), { touch_function: true })
  }), "headset"), true);
  check("Missing sidetone key reads false", parse(Object.assign(rawFixture("status_v3_wf.json"), {
    headsets: [Object.assign(rawFixture("status_v3_wf.json").headsets[0], { supported: Object.assign({}, rawFixture("status_v3_wf.json").headsets[0].supported, { sidetone: undefined }) })]
  })).sidetone, null);
});

suite("Suite 28: v3 Both Headsets", () => {
  const s = v3("status_v3_both.json");
  check("Order kept, headphones first", s.headsets.map((h) => [h.id, h.kind]), [[WH, "headphones"], [WF, "earbuds"]]);
  check("parseStatus gives the primary view", [s.primary, s.id, s.kind, s.connected], [WF, WF, "earbuds", true]);
  check("Bar follows the primary", [Model.barBatteryLevel(s), Model.headsetGlyph(s.kind)], [65, "\u{F184F}"]);
  check("Tooltip, one line per headset", Model.barTooltip(s),
    "WH-1000XM5: Off · 53%\nWF-1000XM5: Off · L 70% R 65% case 53% (bar)");
  check("Switcher visible", [Model.switcherVisible(s), Model.headsetCount(s)], [true, 2]);
  check("Tabs", Model.switcherTabs(s, WF), [
    { id: WH, kind: "headphones", label: "WH-1000XM5", battery: "53%", charging: false, selected: false },
    { id: WF, kind: "earbuds", label: "WF-1000XM5", battery: "65%", charging: false, selected: true }
  ]);

  // Shown headset (ui-spec 8).
  check("No selection shows primary", Model.shownHeadsetId(s, null), WF);
  check("Selection shows that headset", [Model.shownHeadsetId(s, WH), Model.shownHeadset(s, WH).id, Model.shownHeadset(s, WH).kind], [WH, WH, "headphones"]);
  check("Unknown selection shows primary", Model.shownHeadsetId(s, "00:11:22:33:44:55"), WF);
  const wh = Model.shownHeadset(s, WH);
  check("View keeps the file keys", [wh.primary, wh.headsets.length, wh.schemaVersion, wh.connected], [WF, 2, 3, true]);
  check("View has its own fields", [wh.batteryLevel, wh.supported.touch_panel, wh.supported.sidetone], [53, true, false]);
  check("Selection on open: null -> primary", Model.selectionOnOpen(s, null), WF);
  check("Selection on open: kept while connected", Model.selectionOnOpen(s, WH), WH);
  check("Selection on open: gone -> primary", Model.selectionOnOpen(s, "00:11:22:33:44:55"), WF);
  const onlyWf = Model.parseStatus(fixture("status_v3_wf.json"));
  check("Shown headset disconnects: primary shown, selection kept", [Model.shownHeadsetId(onlyWf, WH), Model.selectionOnOpen(onlyWf, WH)], [WF, WF]);
  check("Same id back: shown again", Model.shownHeadsetId(s, WH), WH);
  check("[ and ]: no wrap", [Model.stepHeadset(s, WH, 1), Model.stepHeadset(s, WF, 1), Model.stepHeadset(s, WF, -1), Model.stepHeadset(s, WH, -1)],
    [WF, null, WH, null]);
  check("[ and ] need two headsets", Model.stepHeadset(onlyWf, WF, -1), null);

  // Cursor: the switcher is the first row.
  const secWh = Model.panelCursorSections(wh, {});
  check("Switcher row first, on the shown tab", secWh[0], { id: "switcher", kind: "walk", count: 2, preferred: 0 });
  check("Switcher row then the status targets", ids(secWh).slice(1, 3), ["noise", "ambient"]);
  check("Switcher preferred follows the shown headset", Model.panelCursorSections(s, {})[0].preferred, 1);
  check("Switcher activation", [Model.switcherActivation(s, 0), Model.switcherActivation(s, 1), Model.switcherActivation(s, 2)], [WH, WF, null]);
  check("No switcher target with one headset", ids(Model.panelCursorSections(onlyWf, {}))[0], "noise");
  const down = Model.cursorMove({ section: "switcher", index: 1 }, 0, 1, secWh);
  check("Down from the switcher", down.section, "noise");
  check("h/l walk the tabs", Model.cursorMove({ section: "switcher", index: 0 }, 1, 0, secWh).index, 1);

  // Commands per headset.
  check("Commands go to the shown headset", [Model.buildCommand("volume", 5, wh).args, Model.buildCommand("volume", 5, s).args],
    [["--headset", WH, "volume", "5"], ["--headset", WF, "volume", "5"]]);
  check("Gating per headset", [Model.buildCommand("touchPanel", false, wh).args.length, Model.buildCommand("touchPanel", false, s).error], [4, "Not supported by this headset"]);
  check("Command key includes the headset", [Model.commandKey(["--headset", WH, "volume", "5"]), Model.commandKey(["--headset", WF, "touch-function", "left", "none"]), Model.commandKey(["volume", "5"])],
    [WH + " volume", WF + " touch-function left", "volume"]);
  let q = Model.enqueueCommand([], { args: ["--headset", WF, "volume", "5"], id: 1, headset: WF });
  q = Model.enqueueCommand(q.queue, { args: ["--headset", WH, "volume", "9"], id: 2, headset: WH });
  check("Volume of one headset never replaces the other's", [q.queue.map((i) => i.id), q.dropped], [[1, 2], []]);
  q = Model.enqueueCommand(q.queue, { args: ["--headset", WF, "volume", "6"], id: 3, headset: WF });
  check("Same headset and verb replaces", [q.queue.map((i) => i.id), q.dropped], [[2, 3], [1]]);
  q = Model.enqueueCommand(q.queue, { args: ["--headset", WF, "touch-function", "left", "none"], id: 4, headset: WF });
  q = Model.enqueueCommand(q.queue, { args: ["--headset", WF, "touch-function", "right", "none"], id: 5, headset: WF });
  check("Sides queue separately", q.queue.map((i) => i.id), [2, 3, 4, 5]);
  const pruned = Model.pruneQueue(q.queue, onlyWf);
  check("Queue drops commands of a headset that left", [pruned.queue.map((i) => i.id), pruned.dropped], [[3, 4, 5], [2]]);
  check("Queue unchanged while both are connected", Model.pruneQueue(q.queue, s).queue === q.queue, true);
  check("Queue kept while the file is unreadable", Model.pruneQueue(q.queue, Model.parseStatus("")).dropped, []);

  // Optimistic values per headset.
  let p = Model.addPending({}, { volume: 3 }, 1, WH);
  p = Model.addPending(p, { volume: 25 }, 2, WF);
  const eff = Model.applyPending(s, p);
  check("Values go to their own headset", [eff.id, eff.volume, Model.shownHeadset(eff, WH).volume], [WF, 25, 3]);
  check("Unpatched entries are shared", Model.applyPending(s, Model.addPending({}, { volume: 1 }, 1, WF)).headsets[0] === s.headsets[0], true);
  check("Reconcile per headset", Object.keys(Model.reconcilePending(p, Model.applyPending(s, Model.addPending({}, { volume: 3 }, 9, WH)))), [WF + "|volume"]);
  check("Reconcile against another view", Object.keys(Model.reconcilePending(p, Model.shownHeadset(Model.applyPending(s, Model.addPending({}, { volume: 25 }, 9, WF)), WH))), [WH + "|volume"]);
  check("Values of a headset that left are cleared", Object.keys(Model.reconcilePending(p, onlyWf)), [WF + "|volume"]);
  check("Values for a missing headset are not applied", Model.applyPending(onlyWf, Model.addPending({}, { volume: 1 }, 1, WH)) === onlyWf, true);
});

suite("Suite 29: v3 Initializing and Disconnected Fixtures", () => {
  const s = v3("status_v3_initializing.json");
  check("Reading", [Model.isReading(s), s.supported, s.kind, s.id], [true, null, "earbuds", WF]);
  check("Every target disabled", Model.TARGET_ORDER.filter((id) => Model.TARGETS[id].kind !== "collapse").every((id) => Model.targetState(s, id) === "disabled"), true);
  check("Commands refused", Model.buildCommand("sidetone", true, s), { error: "Reading headset settings…" });
  check("Tooltip", Model.barTooltip(s), "WF-1000XM5: reading headset settings…");
  check("Hero meta", Model.heroMeta(s), "Reading settings");
  check("Bar battery level unknown", Model.barBatteryLevel(s), -1);
  check("Footer without identity", Model.footerLines(s), ["AC:80:0A:65:43:21"]);
  const both = rawFixture("status_v3_both.json");
  both.headsets[0] = rawFixture("status_v3_initializing.json").headsets[0];
  both.headsets[0].id = both.headsets[0].address = WH;
  both.headsets[0].model = "wh-1000xm5";
  both.headsets[0].kind = "headphones";
  both.headsets[0].device_name = "WH-1000XM5";
  const mixed = parse(both);
  check("Initializing tab shows …", Model.switcherTabs(mixed, WF).map((t) => t.battery), ["…", "65%"]);
  check("Initializing tooltip line", Model.barTooltip(mixed).split("\n")[0], "WH-1000XM5: reading headset settings…");

  const off = v3("status_v3_disconnected.json");
  check("Disconnected", [off.ok, off.connected, off.headsets, off.primary, off.id], [true, false, [], null, null]);
  check("No controls, no switcher", [Model.cursorSections(off, { headset: true }), Model.switcherVisible(off)], [[], false]);
  check("No cursor sections", ids(Model.panelCursorSections(off, {})), []);
  check("Headphones glyph when disconnected", Model.headsetGlyph(off.kind), "\u{F02CB}");
  check("Tooltip", Model.barTooltip(off), "Sony headphones: disconnected");
  check("Hero", [Model.heroMeta(off), Model.deviceTitle(off), Model.emptyStateText(off).includes("Bluetooth")], ["Disconnected", "Sony headset", true]);
  check("Commands refused", Model.buildCommand("noise", "anc", off).error, "Headset not connected");
  check("connected:true with no entries is disconnected", parse({ schema_version: 3, connected: true, primary: WH, headsets: [] }).connected, false);
});

suite("Suite 30: v3 Defensive Reading", () => {
  const base = () => rawFixture("status_v3_wf.json");
  const one = (patch) => { const r = base(); Object.assign(r.headsets[0], patch); return parse(r); };
  check("Unknown model reads as other", one({ model: "wf-2000xm9" }).model, "other");
  check("Unknown kind reads as headphones", one({ kind: "speaker" }).kind, "headphones");
  check("Case-folded model and kind", [one({ model: "WF-1000XM5", kind: "EARBUDS" }).model, one({ kind: "EARBUDS" }).kind], ["wf-1000xm5", "earbuds"]);
  check("Lower-case address normalized", one({ id: "ac:80:0a:65:43:21" }).id, WF);
  const noId = base();
  noId.headsets.push(Object.assign({}, noId.headsets[0], { id: "not-an-address" }));
  noId.headsets.push(Object.assign({}, noId.headsets[0]));
  noId.headsets.push("junk");
  check("Entries without a valid id, duplicates and junk are skipped", parse(noId).headsets.map((h) => h.id), [WF]);
  const badPrimary = rawFixture("status_v3_both.json");
  badPrimary.primary = "00:11:22:33:44:55";
  check("primary not in headsets reads as the first entry", [parse(badPrimary).primary, parse(badPrimary).id], [WH, WH]);
  check("Missing primary reads as the first entry", parse(Object.assign(rawFixture("status_v3_both.json"), { primary: null })).primary, WH);
  check("Aliases are not read in v3", one({ battery_charging: null, charging: true, ambient_sound_level: null, ambient_level: 5 }).ambientSoundLevel, null);
  check("Negative earbud levels are unknown", [one({ battery_left: -1 }).batteryLeft, one({ battery_case: 120 }).batteryCase], [null, 100]);
  check("Supported false nulls earbud fields", one({ supported: Object.assign({}, base().headsets[0].supported, { battery_lr: false, battery_case: false }) }).batteryLeft, null);
  check("Options list drops other and unknown values", one({ touch_function_left_options: ["none", "other", "x", "NONE", "volume_control"] }).touchFunctionLeftOptions, ["none", "volume_control"]);
  check("Unknown touch function is null, other kept", [one({ touch_function_left: "tap_dance" }).touchFunctionLeft, one({ touch_function_left: "OTHER" }).touchFunctionLeft], [null, "other"]);

  // Bar battery and charging rules (contract 3.3).
  const ear = (l, r, lc, rc) => ({ kind: "earbuds", batteryLeft: l, batteryRight: r, batteryLeftCharging: lc, batteryRightCharging: rc });
  check("Earbuds: lower side", Model.barBattery(ear(70, 65)), 65);
  check("Earbuds: one side unknown", [Model.barBattery(ear(null, 40)), Model.barBattery(ear(30, null))], [40, 30]);
  check("Earbuds: both unknown", Model.barBattery(ear(null, null)), null);
  check("Earbuds: 0 is a level", Model.barBattery(ear(0, 50)), 0);
  check("Charging: both true", Model.barCharging(ear(1, 1, true, true)), true);
  check("Charging: one known true", Model.barCharging(ear(1, 1, true, null)), true);
  check("Charging: one false", Model.barCharging(ear(1, 1, true, false)), false);
  check("Charging: none known", Model.barCharging(ear(1, 1, null, null)), false);
  check("Headphones: battery_level", [Model.barBattery({ kind: "headphones", batteryLevel: 80 }), Model.barBattery({ kind: "headphones", batteryLevel: -1 })], [80, null]);
  const wf = v3("status_v3_wf.json");
  const charging = Object.assign({}, wf, { batteryLeftCharging: true, batteryRightCharging: true, batteryCaseCharging: true });
  check("Earbuds charging tooltip", Model.barTooltip(charging).split("\n")[2], "Battery L 70% (charging) · R 65% (charging) · case 53% (charging)");
  check("Earbuds charging tint", [Model.isCharging(charging), Model.caseCharging(charging)], [true, true]);
  const low = Object.assign({}, wf, { batteryLeft: 15, batteryRight: null, batteryCase: 5 });
  check("Low side is urgent, unknown side is a dash", Model.earbudBatteryParts(low).map((p) => [p.text, p.low]), [["L 15%", true], ["R —", false]]);
  check("Low earbud drives the icon tint", Model.barBatteryLevel(low), 15);
  check("Unknown case", Model.caseBatteryText(Object.assign({}, wf, { batteryCase: null })), "Case —");
  check("No case part without battery_case", Model.barTooltip(Object.assign({}, wf, { supported: Object.assign({}, wf.supported, { battery_case: false }) })).split("\n")[2], "Battery L 70% · R 65%");
  check("Showing battery needs battery or battery_lr", Model.showBattery(Object.assign({}, wf, { supported: Object.assign({}, wf.supported, { battery_lr: false }) })), false);

  // Worn text per side (ui-spec 2, earbuds hero).
  const worn = (l, r) => Model.heroMeta(Object.assign({}, wf, { wornLeft: l, wornRight: r }));
  check("Worn texts", [worn(true, true), worn(true, false), worn(false, true), worn(false, false), worn(null, null), worn(true, null)],
    ["Connected · LDAC · In ears", "Connected · LDAC · Left in ear", "Connected · LDAC · Right in ear", "Connected · LDAC · Not in ears", "Connected · LDAC", "Connected · LDAC"]);
  check("Headphones keep On head", Model.heroMeta(Object.assign({}, v3("status_v3_wh.json"), { supported: Object.assign({}, wf.supported), worn: false })), "Connected · LDAC · Off head");

  check("headsetLabel fallbacks", [Model.headsetLabel({ deviceName: "", modelName: "X" }), Model.headsetLabel({ deviceName: "", modelName: null, model: "wf-1000xm5" }), Model.headsetLabel({})],
    ["X", "WF-1000XM5", "Sony headset"]);
  check("Every v3 fixture parses", ["status_v3_wh.json", "status_v3_wf.json", "status_v3_both.json", "status_v3_initializing.json", "status_v3_disconnected.json"]
    .map((f) => [v3(f).ok, v3(f).schemaVersion, v3(f).headsets.length]), [[true, 3, 1], [true, 3, 1], [true, 3, 2], [true, 3, 1], [true, 3, 0]]);
});

suite("Suite 31: Daemon Install State", () => {
  const ok = Model.parseStatus(fixture("status_v3_wh.json"));
  const old = Model.parseStatus(fixture("status_v3_wh.json").replace(/\s*"daemon_version": "[^"]*",/, ""));
  const none = Model.parseStatus("");
  const local = { path: "/home/u/.local/bin/sony-ctl", version: "0.1.0" };
  const packaged = { path: "/usr/bin/sony-ctl", version: "0.1.0" };
  const missing = { path: null, version: null };
  const dir = "/home/u/.config/omarchy/plugins/io.github.enxy0.sonyxm5";

  check("daemon_version is read", [ok.daemonVersion, old.daemonVersion], ["0.1.0", null]);
  check("States", [
    Model.daemonState(ok, null, "0.1.0"),
    Model.daemonState(none, missing, "0.1.0"),
    Model.daemonState(ok, local, "0.1.0"),
    Model.daemonState(ok, local, "0.2.0"),
    Model.daemonState(old, local, "0.1.0"),
    Model.daemonState(none, local, "0.1.0"),
    Model.daemonState(ok, local, ""),
  ], ["unknown", "missing", "ok", "outdated", "outdated", "ok", "ok"]);
  check("Commands", [
    Model.daemonCommand("missing", missing, dir),
    Model.daemonCommand("outdated", local, dir),
    Model.daemonCommand("outdated", packaged, dir),
    Model.daemonCommand("ok", local, dir),
  ], [dir + "/setup", dir + "/setup", "git pull && makepkg -si", ""]);
  const card = (state, status, probe, version) => {
    const c = Model.daemonCard(state, status, probe, version);
    return c === null ? null : [c.title, c.versions];
  };
  check("Cards", [
    card("missing", none, missing, "0.1.0"),
    card("outdated", ok, local, "0.2.0"),
    card("outdated", old, packaged, "0.1.0"),
    card("ok", ok, local, "0.1.0"),
    card("unknown", ok, null, "0.1.0"),
  ], [["Install the daemon", ""], ["Update the daemon", "0.1.0 → 0.2.0"], ["Update the daemon", "older → 0.1.0"], null, null]);
  check("Packaged daemon: rebuild text", Model.daemonCard("outdated", ok, packaged, "0.2.0").text.indexOf("Arch package") >= 0, true);
  const actions = (state, probe) => Model.daemonActions(state, probe).map((a) => a.id + ":" + a.label);
  check("Actions", [actions("missing", missing), actions("outdated", local), actions("outdated", packaged), actions("ok", local)],
    [["run:Install", "copy:Copy command"], ["run:Update", "copy:Copy command"], ["copy:Copy command"], []]);
  check("Missing daemon: tooltip, hero and no empty-state text",
    [Model.barTooltip(none, "missing"), Model.heroMeta(none, "missing"), Model.emptyStateText(none, "missing")],
    ["Sony headphones: the daemon is not installed", "Daemon not installed", ""]);
  const ids = (sections) => sections.map((s) => s.id + (s.count ? ":" + s.count : ""));
  check("Daemon buttons are the first cursor row", ids(Model.panelCursorSections(ok, {}, 2)).slice(0, 2), ["daemon:2", "noise:3"]);
  check("No daemon row without buttons", ids(Model.panelCursorSections(ok, {}, 0))[0], "noise:3");
  check("Missing daemon: the buttons are the only row", ids(Model.panelCursorSections(none, {}, 2)), ["daemon:2"]);
});

console.log(`\nSummary: ${passed} passed, ${failed} failed`);
if (failed > 0) {
  if (typeof Deno !== "undefined") Deno.exit(1);
  if (typeof process !== "undefined") process.exit(1);
}
