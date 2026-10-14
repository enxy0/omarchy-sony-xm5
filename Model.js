// Model.js
// Pure ECMAScript model for the Sony WH-1000XM5 bar widget.
// No QML dependencies; runs in QML, Deno and Node (tests/model.test.js).
// Contract: docs/backend-contract.md (status schema 2). UI: docs/ui-spec.md.

var SUPPORTED_SCHEMA = 2;
var LEVEL_UNKNOWN = -1;

var NOISE_ANC = "anc";
var NOISE_AMBIENT = "ambient";
var NOISE_WIND = "wind";
var NOISE_OFF = "off";
var NOISE_UNKNOWN = "unknown";

var EQ_OFF = "off";
var EQ_BRIGHT = "bright";
var EQ_EXCITED = "excited";
var EQ_MELLOW = "mellow";
var EQ_RELAXED = "relaxed";
var EQ_VOCAL = "vocal";
var EQ_TREBLE = "treble";
var EQ_BASS = "bass";
var EQ_SPEECH = "speech";
var EQ_CUSTOM = "custom";
var EQ_UNKNOWN = "unknown";

var MAX_ERROR_CHARS = 140;
var ELIDED_ERROR_CHARS = 137;

var NOISE_MODES = [NOISE_ANC, NOISE_AMBIENT, NOISE_OFF];
// v1 preset list; also the fallback while eq_presets is null.
var EQ_PRESETS = [
  EQ_OFF, EQ_BRIGHT, EQ_EXCITED, EQ_MELLOW, EQ_RELAXED,
  EQ_VOCAL, EQ_TREBLE, EQ_BASS, EQ_SPEECH, EQ_CUSTOM
];
var EQ_PRESET_VALUES = EQ_PRESETS.concat(["user1", "user2", "user3", "user4", "user5"]);
var EQ_BAND_LABELS = ["400 Hz", "1 kHz", "2.5 kHz", "6.3 kHz", "16 kHz"];
var EQ_LEVEL_MIN = -10;
var EQ_LEVEL_MAX = 10;
var AMBIENT_MIN = 1;
var AMBIENT_MAX = 20;
var VOLUME_MIN = 0;
var VOLUME_MAX = 30;
var GUIDANCE_VOLUME_MIN = -2;
var GUIDANCE_VOLUME_MAX = 2;

var CONNECTION_QUALITY_OPTIONS = ["sound", "connection"];
var CODECS = ["SBC", "AAC", "LDAC", "APTX", "APTX_HD", "LC3", "OTHER", "NONE"];
var STC_SENSITIVITY_OPTIONS = ["auto", "high", "low"];
var STC_TIMEOUT_OPTIONS = ["short", "standard", "long", "off"];
var AUTO_POWER_OFF_VALUES = ["off", "5min", "15min", "30min", "60min", "180min", "when_taken_off"];
var NC_AMB_BUTTON_OPTIONS = ["nc_ambient_off", "nc_ambient", "nc_off", "ambient_off"];
var BUTTON_FUNCTION_VALUES = [
  "ambient_sound_control", "voice_recognition", "google_assistant",
  "amazon_alexa", "quick_access", "none"
];

// Every `supported` key of schema 2 (contract 2.4).
var FEATURE_KEYS = [
  "battery", "noise_control", "eq_preset", "eq_custom", "dsee", "speak_to_chat",
  "voice_guidance", "voice_guidance_volume", "auto_power_off", "nc_amb_button",
  "button_function", "ear_detection", "worn", "volume", "connection_quality",
  "codec", "multipoint", "devices", "source_switch"
];
// Contract 2.5: how a schema 1 file is read.
var V1_SUPPORTED_KEYS = [
  "battery", "noise_control", "eq_preset", "eq_custom", "dsee",
  "speak_to_chat", "ear_detection", "multipoint", "codec"
];

// Parsed field -> supported key that gates it.
var FIELD_FEATURES = {
  batteryLevel: "battery",
  batteryCharging: "battery",
  noiseMode: "noise_control",
  ambientSoundLevel: "noise_control",
  focusOnVoice: "noise_control",
  eqPreset: "eq_preset",
  eqPresets: "eq_preset",
  eqCustomBands: "eq_custom",
  clearBass: "eq_custom",
  dseeExtreme: "dsee",
  connectionQuality: "connection_quality",
  codec: "codec",
  volume: "volume",
  speakToChat: "speak_to_chat",
  stcSensitivity: "speak_to_chat",
  stcTimeout: "speak_to_chat",
  voiceGuidance: "voice_guidance",
  voiceGuidanceVolume: "voice_guidance_volume",
  autoPowerOff: "auto_power_off",
  autoPowerOffOptions: "auto_power_off",
  ncAmbButton: "nc_amb_button",
  buttonFunction: "button_function",
  buttonFunctions: "button_function",
  earDetection: "ear_detection",
  worn: "worn",
  multipoint: "multipoint",
  devices: "devices"
};

// Popup sections and the supported keys of their controls.
var SECTION_FEATURES = {
  noise: ["noise_control"],
  sound: ["eq_preset", "dsee", "connection_quality", "codec"],
  customEq: ["eq_custom"],
  speakToChat: ["speak_to_chat"],
  headset: ["volume", "voice_guidance", "voice_guidance_volume", "auto_power_off",
    "nc_amb_button", "button_function", "ear_detection"],
  multipoint: ["multipoint", "devices"]
};

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

function defaultStatus() {
  return {
    ok: false,
    lastError: "",
    schemaVersion: 0,
    schemaTooNew: false,
    connected: false,
    lastUpdated: 0,
    deviceName: "",
    address: null,
    modelName: null,
    firmwareVersion: null,
    supported: null,
    supportedSource: null,
    batteryLevel: LEVEL_UNKNOWN,
    batteryCharging: null,
    noiseMode: null,
    ambientSoundLevel: null,
    focusOnVoice: null,
    eqPreset: null,
    eqPresets: null,
    eqCustomBands: null,
    clearBass: null,
    dseeExtreme: null,
    connectionQuality: null,
    codec: null,
    volume: null,
    speakToChat: null,
    stcSensitivity: null,
    stcTimeout: null,
    voiceGuidance: null,
    voiceGuidanceVolume: null,
    autoPowerOff: null,
    autoPowerOffOptions: null,
    ncAmbButton: null,
    buttonFunction: null,
    buttonFunctions: null,
    earDetection: null,
    worn: null,
    multipoint: null,
    devices: null
  };
}

function clamp(val, min, max, def) {
  if (val === null || val === undefined || val === "" || typeof val === "boolean") return def;
  var n = Number(val);
  if (isNaN(n)) return def;
  return Math.max(min, Math.min(max, Math.round(n)));
}

function _bool(v) {
  return v === true ? true : (v === false ? false : null);
}

function _firstDefined(a, b) {
  return (a !== undefined && a !== null) ? a : b;
}

function _enumValue(v, allowed) {
  if (typeof v !== "string") return null;
  var s = v.toLowerCase();
  return allowed.indexOf(s) !== -1 ? s : null;
}

function _string(v) {
  return typeof v === "string" ? v : null;
}

function _enumList(v, allowed) {
  if (!Array.isArray(v)) return null;
  var out = [];
  for (var i = 0; i < v.length; i++) {
    var s = _enumValue(v[i], allowed);
    if (s !== null && out.indexOf(s) === -1) out.push(s);
  }
  return out;
}

function _address(v) {
  if (typeof v !== "string") return null;
  var s = v.trim().toUpperCase();
  return /^[0-9A-F]{2}(:[0-9A-F]{2}){5}$/.test(s) ? s : null;
}

function _devices(v) {
  if (!Array.isArray(v)) return null;
  var out = [];
  var activeSeen = false;
  for (var i = 0; i < v.length; i++) {
    var d = v[i];
    if (!d || typeof d !== "object") continue;
    var addr = _address(d.address);
    if (addr === null) continue;
    var connected = d.connected === true;
    // `active` implies `connected`; at most one device is active.
    var active = d.active === true && connected && !activeSeen;
    if (active) activeSeen = true;
    out.push({ address: addr, name: typeof d.name === "string" ? d.name : "", connected: connected, active: active });
  }
  return out;
}

function _supported(raw) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;
  var out = {};
  for (var i = 0; i < FEATURE_KEYS.length; i++) out[FEATURE_KEYS[i]] = raw[FEATURE_KEYS[i]] === true;
  return out;
}

function v1Supported() {
  var out = {};
  for (var i = 0; i < FEATURE_KEYS.length; i++) out[FEATURE_KEYS[i]] = V1_SUPPORTED_KEYS.indexOf(FEATURE_KEYS[i]) !== -1;
  return out;
}

// An integer >= 1, given as a number or as a string of digits (v1 code read
// it with Number()). Anything else is null and counts as "too new"
// (contract 5).
function schemaVersionOf(raw) {
  var n = null;
  if (typeof raw === "number") n = raw;
  else if (typeof raw === "string" && /^[0-9]+$/.test(raw.trim())) n = Number(raw.trim());
  if (n === null || !isFinite(n) || Math.floor(n) !== n || n < 1) return null;
  return n;
}

function _errorStatus(message) {
  var res = defaultStatus();
  res.lastError = message;
  return res;
}

function parseStatus(raw) {
  if (raw === null || raw === undefined) return _errorStatus("The sony status file is empty");
  var text = String(raw).trim();
  if (!text) return _errorStatus("The sony status file is empty");

  var parsed;
  try {
    parsed = JSON.parse(text);
  } catch (_e) {
    return _errorStatus("Could not read the sony status file");
  }

  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
    return _errorStatus("The sony status file is invalid");
  }

  if (parsed.schema_version === undefined || parsed.schema_version === null) {
    return _errorStatus("The sony status file carried no schema_version");
  }

  var version = schemaVersionOf(parsed.schema_version);
  if (version === null || version > SUPPORTED_SCHEMA) {
    var tooNew = defaultStatus();
    tooNew.schemaTooNew = true;
    tooNew.schemaVersion = version === null ? 0 : version;
    tooNew.lastError = "sony daemon speaks status schema " + JSON.stringify(parsed.schema_version) + ", this panel reads " + SUPPORTED_SCHEMA;
    return tooNew;
  }

  var res = defaultStatus();
  res.ok = true;
  res.schemaVersion = version;
  res.connected = parsed.connected === true;
  res.lastUpdated = clamp(parsed.last_updated, 0, Number.MAX_SAFE_INTEGER, 0);
  if (!res.connected) return res;

  var v1 = version < 2;
  res.deviceName = typeof parsed.device_name === "string" ? parsed.device_name : "";
  res.address = _address(parsed.address);
  res.modelName = _string(parsed.model_name);
  res.firmwareVersion = _string(parsed.firmware_version);

  if (v1) {
    res.supported = v1Supported();
    res.supportedSource = null;
  } else {
    res.supported = _supported(parsed.supported);
    res.supportedSource = res.supported ? _enumValue(parsed.supported_source, ["device", "fallback"]) : null;
  }

  // Battery: v1 used -1 for unknown, v2 writes null.
  var bl = clamp(parsed.battery_level, -1, 100, LEVEL_UNKNOWN);
  res.batteryLevel = bl < 0 ? LEVEL_UNKNOWN : bl;
  res.batteryCharging = _bool(_firstDefined(parsed.battery_charging, parsed.charging));

  res.noiseMode = _enumValue(parsed.noise_mode, v1 ? [NOISE_ANC, NOISE_AMBIENT, NOISE_WIND, NOISE_OFF] : NOISE_MODES);
  res.ambientSoundLevel = clamp(_firstDefined(parsed.ambient_sound_level, parsed.ambient_level), AMBIENT_MIN, AMBIENT_MAX, null);
  res.focusOnVoice = _bool(parsed.voice_passthrough);

  res.eqPreset = _enumValue(parsed.eq_preset, EQ_PRESET_VALUES.concat([EQ_UNKNOWN]));
  res.eqPresets = _enumList(parsed.eq_presets, EQ_PRESET_VALUES);
  var rawBands = _firstDefined(parsed.eq_custom_bands, parsed.eq_bands);
  if (Array.isArray(rawBands)) {
    var bands = [];
    for (var i = 0; i < 5; i++) bands.push(clamp(rawBands[i], EQ_LEVEL_MIN, EQ_LEVEL_MAX, 0));
    res.eqCustomBands = bands;
  }
  res.clearBass = clamp(parsed.clear_bass, EQ_LEVEL_MIN, EQ_LEVEL_MAX, null);

  res.dseeExtreme = _bool(_firstDefined(parsed.dsee_extreme, parsed.dsee));
  res.connectionQuality = _enumValue(parsed.connection_quality, CONNECTION_QUALITY_OPTIONS.concat(["other"]));
  if (typeof parsed.codec === "string" && parsed.codec !== "") {
    var codec = parsed.codec.toUpperCase();
    res.codec = CODECS.indexOf(codec) !== -1 ? codec : "OTHER";
  }
  res.volume = clamp(parsed.volume, VOLUME_MIN, VOLUME_MAX, null);

  res.speakToChat = _bool(parsed.speak_to_chat);
  res.stcSensitivity = _enumValue(parsed.speak_to_chat_sensitivity, STC_SENSITIVITY_OPTIONS);
  res.stcTimeout = _enumValue(parsed.speak_to_chat_timeout, STC_TIMEOUT_OPTIONS);

  res.voiceGuidance = _bool(parsed.voice_guidance);
  res.voiceGuidanceVolume = clamp(parsed.voice_guidance_volume, GUIDANCE_VOLUME_MIN, GUIDANCE_VOLUME_MAX, null);

  res.autoPowerOff = _enumValue(parsed.auto_power_off, AUTO_POWER_OFF_VALUES);
  res.autoPowerOffOptions = _enumList(parsed.auto_power_off_options, AUTO_POWER_OFF_VALUES);
  res.ncAmbButton = _enumValue(parsed.nc_amb_button, NC_AMB_BUTTON_OPTIONS);
  res.buttonFunction = _enumValue(parsed.button_function, BUTTON_FUNCTION_VALUES.concat(["other"]));
  res.buttonFunctions = _enumList(parsed.button_functions, BUTTON_FUNCTION_VALUES);

  res.earDetection = _bool(parsed.ear_detection);
  res.worn = _bool(parsed.worn);
  res.multipoint = _bool(parsed.multipoint);
  res.devices = _devices(parsed.devices);

  // A false supported key means the matching fields are null (contract 2.4).
  if (res.supported) {
    for (var field in FIELD_FEATURES) {
      if (res.supported[FIELD_FEATURES[field]] !== true) {
        res[field] = field === "batteryLevel" ? LEVEL_UNKNOWN : null;
      }
    }
  }
  return res;
}

// ---------------------------------------------------------------------------
// Gating
// ---------------------------------------------------------------------------

var STATE_HIDDEN = "hidden";
var STATE_DISABLED = "disabled";
var STATE_ENABLED = "enabled";

function isLive(status) {
  return !!status && status.ok === true && status.connected === true;
}

function isReading(status) {
  return isLive(status) && status.supported === null;
}

function isSupported(status, key) {
  return isLive(status) && !!status.supported && status.supported[key] === true;
}

// "hidden", "disabled" or "enabled" for a control gated by `key` that
// shows `value` (contract 2.5, ui-spec 4).
function controlState(status, key, value) {
  if (!isLive(status)) return STATE_HIDDEN;
  if (status.supported === null || status.supported === undefined) return STATE_DISABLED;
  if (status.supported[key] !== true) return STATE_HIDDEN;
  if (value === null || value === undefined) return STATE_DISABLED;
  if (key === "battery" && value === LEVEL_UNKNOWN) return STATE_DISABLED;
  return STATE_ENABLED;
}

function isVisible(status, key, value) {
  return controlState(status, key, value) !== STATE_HIDDEN;
}

function isEnabled(status, key, value) {
  return controlState(status, key, value) === STATE_ENABLED;
}

function sectionVisible(status, section) {
  var keys = SECTION_FEATURES[section];
  if (!keys || !isLive(status)) return false;
  if (status.supported === null) return true;
  for (var i = 0; i < keys.length; i++) {
    if (status.supported[keys[i]] === true) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Option lists
// ---------------------------------------------------------------------------

function eqPresetOptions(status) {
  var list = status && Array.isArray(status.eqPresets) ? status.eqPresets : null;
  return list && list.length > 0 ? list.slice() : EQ_PRESETS.slice();
}

// ui-spec 7.1: without a list, offer the timed values and keep
// when_taken_off only when it is the current value.
function autoPowerOffOptions(status) {
  var list = status && Array.isArray(status.autoPowerOffOptions) ? status.autoPowerOffOptions : null;
  if (list && list.length > 0) return list.slice();
  var out = AUTO_POWER_OFF_VALUES.slice(0, 6);
  if (status && status.autoPowerOff === "when_taken_off") out.push("when_taken_off");
  return out;
}

// ui-spec 7.2: without a list, offer every contract value.
function buttonFunctionOptions(status) {
  var list = status && Array.isArray(status.buttonFunctions) ? status.buttonFunctions : null;
  return list && list.length > 0 ? list.slice() : BUTTON_FUNCTION_VALUES.slice();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

function _onOff(v) {
  return v ? "on" : "off";
}

function _int(v, min, max) {
  if (typeof v !== "number" || isNaN(v) || Math.round(v) !== v) return null;
  return v >= min && v <= max ? v : null;
}

function _fail(message) {
  return { error: message };
}

// Action table: feature key, value check and command shape.
// Returns { args, pending, feature } or { error }.
function buildCommand(action, value, status) {
  if (!status || status.ok !== true) return _fail("No headset status");
  if (status.connected !== true) return _fail("Headset not connected");
  if (status.supported === null || status.supported === undefined) return _fail("Reading headset settings…");

  var spec = _commandSpec(action, value, status);
  if (!spec) return _fail("Unknown action " + action);
  // Same order as the daemon (contract 3.1): feature first, then the value.
  if (status.supported[spec.feature] !== true) return _fail("Not supported by this headset");
  if (spec.error) return _fail(spec.error);
  if (spec.check) {
    var msg = spec.check();
    if (msg) return _fail(msg);
  }
  if (status.schemaVersion < 2 && !isV1Command(spec.args)) return _fail("Needs a newer sony-headphones daemon");
  return { args: spec.args, pending: spec.pending, feature: spec.feature };
}

// Verbs and arguments the schema 1 CLI accepts (cli/src/main.cpp before
// schema 2). Bare `eq custom`, user presets and every new verb are v2 only.
var V1_VERBS = ["noise", "ambient-level", "eq", "dsee", "speak-to-chat", "multipoint", "ear-detect"];
var V1_EQ_PRESETS = EQ_PRESETS.slice(0, 9);

function isV1Command(args) {
  if (!Array.isArray(args) || V1_VERBS.indexOf(args[0]) === -1) return false;
  if (args[0] !== "eq") return true;
  if (args[1] === EQ_CUSTOM) return args.length === 8;
  return args.length === 2 && V1_EQ_PRESETS.indexOf(args[1]) !== -1;
}

function _boolSpec(feature, verb, field, value) {
  if (typeof value !== "boolean") return { feature: feature, error: "Invalid value" };
  var pending = {};
  pending[field] = value;
  return { feature: feature, args: [verb, _onOff(value)], pending: pending };
}

function _enumSpec(feature, verb, field, value, options) {
  if (typeof value !== "string" || options.indexOf(value) === -1) return { feature: feature, error: "Not supported by this headset" };
  var pending = {};
  pending[field] = value;
  return { feature: feature, args: [verb, value], pending: pending };
}

function _intSpec(feature, verb, field, value, min, max) {
  var n = _int(value, min, max);
  if (n === null) return { feature: feature, error: "Value out of range" };
  var pending = {};
  pending[field] = n;
  return { feature: feature, args: [verb, String(n)], pending: pending };
}

function _eqCustomSpec(bands, clearBass) {
  var bad = { feature: "eq_custom", error: "Value out of range" };
  if (!Array.isArray(bands) || bands.length !== 5) return bad;
  var out = [];
  for (var i = 0; i < 5; i++) {
    var b = _int(bands[i], EQ_LEVEL_MIN, EQ_LEVEL_MAX);
    if (b === null) return bad;
    out.push(b);
  }
  var cb = _int(clearBass, EQ_LEVEL_MIN, EQ_LEVEL_MAX);
  if (cb === null) return bad;
  var args = ["eq", "custom"];
  for (var j = 0; j < 5; j++) args.push(String(out[j]));
  args.push(String(cb));
  return {
    feature: "eq_custom",
    args: args,
    pending: { eqPreset: EQ_CUSTOM, eqCustomBands: out, clearBass: cb }
  };
}

function _stcConfigSpec(sensitivity, timeout) {
  if (STC_SENSITIVITY_OPTIONS.indexOf(sensitivity) === -1 || STC_TIMEOUT_OPTIONS.indexOf(timeout) === -1) {
    return { feature: "speak_to_chat", error: "Speak-to-Chat settings are not known yet" };
  }
  return {
    feature: "speak_to_chat",
    args: ["speak-to-chat-config", sensitivity, timeout],
    pending: { stcSensitivity: sensitivity, stcTimeout: timeout }
  };
}

function _commandSpec(action, value, status) {
  switch (action) {
  case "noise":
    return _enumSpec("noise_control", "noise", "noiseMode", value, NOISE_MODES);
  case "ambientLevel": {
    var spec = _intSpec("noise_control", "ambient-level", "ambientSoundLevel", value, AMBIENT_MIN, AMBIENT_MAX);
    if (spec.pending) spec.pending.noiseMode = NOISE_AMBIENT;
    return spec;
  }
  case "focusVoice":
    return _boolSpec("noise_control", "focus-voice", "focusOnVoice", value);
  case "eqPreset":
    // With known custom values, the Custom chip sends the full form so the
    // headset gets exactly what the panel shows (and a v1 daemon accepts it).
    if (value === EQ_CUSTOM && eqPresetOptions(status).indexOf(EQ_CUSTOM) !== -1 &&
        Array.isArray(status.eqCustomBands) && status.clearBass !== null && status.supported.eq_custom === true) {
      var full = _eqCustomSpec(status.eqCustomBands, status.clearBass);
      full.feature = "eq_preset";
      return full;
    }
    return _enumSpec("eq_preset", "eq", "eqPreset", value, eqPresetOptions(status));
  case "eqCustom":
    return _eqCustomSpec(value ? value.bands : null, value ? value.clearBass : null);
  case "eqBand": {
    // value: { index: 0..4 band, 5 = Clear Bass; level }
    if (!Array.isArray(status.eqCustomBands) || status.clearBass === null) {
      return { feature: "eq_custom", error: "Custom EQ values are not known yet" };
    }
    var index = value ? value.index : -1;
    if (_int(index, 0, 5) === null) return { feature: "eq_custom", error: "Value out of range" };
    var bands = status.eqCustomBands.slice();
    var cb = status.clearBass;
    if (index === 5) cb = value.level;
    else bands[index] = value.level;
    return _eqCustomSpec(bands, cb);
  }
  case "dsee":
    return _boolSpec("dsee", "dsee", "dseeExtreme", value);
  case "connectionQuality":
    return _enumSpec("connection_quality", "connection-quality", "connectionQuality", value, CONNECTION_QUALITY_OPTIONS);
  case "volume":
    return _intSpec("volume", "volume", "volume", value, VOLUME_MIN, VOLUME_MAX);
  case "speakToChat":
    return _boolSpec("speak_to_chat", "speak-to-chat", "speakToChat", value);
  case "stcSensitivity":
    if (STC_SENSITIVITY_OPTIONS.indexOf(value) === -1) return { feature: "speak_to_chat", error: "Invalid value" };
    return _stcConfigSpec(value, status.stcTimeout);
  case "stcTimeout":
    if (STC_TIMEOUT_OPTIONS.indexOf(value) === -1) return { feature: "speak_to_chat", error: "Invalid value" };
    return _stcConfigSpec(status.stcSensitivity, value);
  case "voiceGuidance":
    return _boolSpec("voice_guidance", "voice-guidance", "voiceGuidance", value);
  case "voiceGuidanceVolume":
    return _intSpec("voice_guidance_volume", "voice-guidance-volume", "voiceGuidanceVolume", value, GUIDANCE_VOLUME_MIN, GUIDANCE_VOLUME_MAX);
  case "autoPowerOff":
    return _enumSpec("auto_power_off", "auto-power-off", "autoPowerOff", value, autoPowerOffOptions(status));
  case "ncAmbButton":
    return _enumSpec("nc_amb_button", "nc-amb-button", "ncAmbButton", value, NC_AMB_BUTTON_OPTIONS);
  case "buttonFunction":
    return _enumSpec("button_function", "button-function", "buttonFunction", value, buttonFunctionOptions(status));
  case "earDetection":
    return _boolSpec("ear_detection", "ear-detect", "earDetection", value);
  case "multipoint":
    return _boolSpec("multipoint", "multipoint", "multipoint", value);
  case "source": {
    var addr = _address(value);
    if (addr === null) return { feature: "source_switch", error: "Invalid address" };
    var devices = Array.isArray(status.devices) ? status.devices : [];
    var found = false;
    var next = [];
    for (var i = 0; i < devices.length; i++) {
      var d = devices[i];
      if (d.address === addr && d.connected) found = true;
      next.push({ address: d.address, name: d.name, connected: d.connected, active: d.address === addr });
    }
    return {
      feature: "source_switch",
      args: ["source", addr],
      pending: { devices: next },
      check: function() { return found ? "" : "Device is not connected"; }
    };
  }
  default:
    return null;
  }
}

// One-line error text for a failed sony-ctl run.
function commandErrorText(exitCode, stderr, stdout) {
  if (exitCode === 2) return "Daemon not running (sony-ctl could not connect)";
  if (exitCode === 127 && !stderr && !stdout) return "sony-ctl did not start (is ~/.local/bin/sony-ctl installed?)";
  var text = String(stderr || stdout || "").trim();
  var lines = text.split("\n");
  text = lines[lines.length - 1].trim();
  text = text.replace(/^Error from daemon:\s*/, "").replace(/^Error:\s*/, "");
  if (!text) text = "sony-ctl failed (exit " + exitCode + ")";
  return elideError(text);
}

// ---------------------------------------------------------------------------
// Optimistic values. `pending` maps a parsed field to { value, id }; `id` is
// the command that set it.
// ---------------------------------------------------------------------------

function _same(a, b) {
  return JSON.stringify(a) === JSON.stringify(b);
}

function addPending(pending, fields, id) {
  var out = {};
  for (var k in pending) out[k] = pending[k];
  for (var f in fields) out[f] = { value: fields[f], id: id };
  return out;
}

function dropPending(pending, id) {
  var out = {};
  for (var k in pending) if (pending[k].id !== id) out[k] = pending[k];
  return out;
}

// Removes every entry that the daemon now reports.
function reconcilePending(pending, status) {
  if (!isLive(status)) return pending;
  var out = {};
  var changed = false;
  for (var k in pending) {
    if (_same(status[k], pending[k].value)) changed = true;
    else out[k] = pending[k];
  }
  return changed ? out : pending;
}

function hasPending(pending) {
  for (var k in pending) return true;
  return false;
}

function applyPending(status, pending) {
  if (!isLive(status) || !hasPending(pending)) return status;
  var out = {};
  for (var k in status) out[k] = status[k];
  for (var f in pending) out[f] = pending[f].value;
  return out;
}

// ---------------------------------------------------------------------------
// Command queue. Items are { args, id }. A queued command with the same verb
// is dropped and the new one goes to the end, so the last intent wins.
// Returns { queue, dropped } where `dropped` lists the removed ids.
// ---------------------------------------------------------------------------

function enqueueCommand(queue, item) {
  var out = [];
  var dropped = [];
  for (var i = 0; i < queue.length; i++) {
    if (queue[i].args[0] === item.args[0]) dropped.push(queue[i].id);
    else out.push(queue[i]);
  }
  out.push(item);
  return { queue: out, dropped: dropped };
}

// ---------------------------------------------------------------------------
// Labels
// ---------------------------------------------------------------------------

function noiseModeName(mode) {
  switch (mode) {
    case NOISE_ANC: return "Noise Cancelling";
    case NOISE_AMBIENT: return "Ambient Sound";
    case NOISE_WIND: return "Wind Noise Reduction";
    case NOISE_OFF: return "Off";
    default: return "Unknown";
  }
}

function noiseModeLabel(mode) {
  switch (mode) {
    case NOISE_ANC: return "ANC";
    case NOISE_AMBIENT: return "Ambient";
    case NOISE_OFF: return "Off";
    default: return noiseModeName(mode);
  }
}

function noiseModeIcon(mode) {
  switch (mode) {
    case NOISE_ANC: return "󰍋";
    case NOISE_AMBIENT: return "󰈦";
    case NOISE_WIND: return "󰒚";
    case NOISE_OFF: return "󰍌";
    default: return "󰍋";
  }
}

function eqPresetName(preset) {
  switch (preset) {
    case EQ_OFF: return "Off";
    case EQ_BRIGHT: return "Bright";
    case EQ_EXCITED: return "Excited";
    case EQ_MELLOW: return "Mellow";
    case EQ_RELAXED: return "Relaxed";
    case EQ_VOCAL: return "Vocal";
    case EQ_TREBLE: return "Treble Boost";
    case EQ_BASS: return "Bass Boost";
    case EQ_SPEECH: return "Speech";
    case EQ_CUSTOM: return "Custom";
    default:
      if (/^user[1-5]$/.test(String(preset))) return "User " + String(preset).charAt(4);
      return "Unknown";
  }
}

function eqPresetButtonLabel(preset) {
  switch (preset) {
    case EQ_TREBLE: return "Treble";
    case EQ_BASS: return "Bass";
    default: return eqPresetName(preset);
  }
}

function connectionQualityLabel(v) {
  switch (v) {
    case "sound": return "Sound quality";
    case "connection": return "Stable connection";
    case "other": return "Other";
    default: return "Unknown";
  }
}

function codecLabel(codec) {
  switch (codec) {
    case "APTX": return "aptX";
    case "APTX_HD": return "aptX HD";
    case "NONE": return "No stream";
    case "OTHER": return "Other";
    case null: case undefined: case "": return "Unknown";
    default: return String(codec);
  }
}

function stcSensitivityLabel(v) {
  switch (v) {
    case "auto": return "Auto";
    case "high": return "High";
    case "low": return "Low";
    default: return "Unknown";
  }
}

function stcTimeoutLabel(v) {
  switch (v) {
    case "short": return "Short";
    case "standard": return "Standard";
    case "long": return "Long";
    case "off": return "Off";
    default: return "Unknown";
  }
}

function autoPowerOffLabel(v) {
  switch (v) {
    case "off": return "Off";
    case "5min": return "5 min";
    case "15min": return "15 min";
    case "30min": return "30 min";
    case "60min": return "1 h";
    case "180min": return "3 h";
    case "when_taken_off": return "Taken off";
    default: return "Unknown";
  }
}

function autoPowerOffTooltip(v) {
  return v === "when_taken_off" ? "Power off when the headset is taken off" : "";
}

function ncAmbButtonLabel(v) {
  switch (v) {
    case "nc_ambient_off": return "NC·AMB·Off";
    case "nc_ambient": return "NC·AMB";
    case "nc_off": return "NC·Off";
    case "ambient_off": return "AMB·Off";
    default: return "Unknown";
  }
}

function ncAmbButtonTooltip(v) {
  switch (v) {
    case "nc_ambient_off": return "Noise Cancelling → Ambient Sound → Off";
    case "nc_ambient": return "Noise Cancelling → Ambient Sound";
    case "nc_off": return "Noise Cancelling → Off";
    case "ambient_off": return "Ambient Sound → Off";
    default: return "";
  }
}

function buttonFunctionLabel(v) {
  switch (v) {
    case "ambient_sound_control": return "Noise control";
    case "voice_recognition": return "Voice assist";
    case "google_assistant": return "Google";
    case "amazon_alexa": return "Alexa";
    case "quick_access": return "Quick access";
    case "none": return "None";
    case "other": return "Other";
    default: return "Unknown";
  }
}

function formatSigned(n) {
  if (n === null || n === undefined || isNaN(Number(n))) return "—";
  var v = Math.round(Number(n));
  return v > 0 ? "+" + v : String(v);
}

function formatValue(n, suffix) {
  if (n === null || n === undefined) return "—";
  return String(n) + (suffix || "");
}

function onOffLabel(v) {
  return v === true ? "On" : (v === false ? "Off" : "Unknown");
}

function deviceLabel(device) {
  if (!device) return "";
  return device.name !== "" ? device.name : device.address;
}

function deviceStateLabel(device) {
  if (!device) return "";
  if (device.active) return "Playing";
  return device.connected ? "Connected" : "Not connected";
}

function canSwitchTo(status, device) {
  return !!device && device.connected && !device.active && isSupported(status, "source_switch");
}

function eqBandsSummary(status) {
  if (!status || !Array.isArray(status.eqCustomBands)) return "";
  var parts = [];
  for (var i = 0; i < status.eqCustomBands.length; i++) parts.push(formatSigned(status.eqCustomBands[i]));
  return parts.join(" ");
}

// ---------------------------------------------------------------------------
// Bar button and hero texts
// ---------------------------------------------------------------------------

function formatBattery(level) {
  if (level === undefined || level === null || level < 0) return "—";
  return Math.round(level) + "%";
}

function levelFraction(level) {
  if (level === undefined || level === null || level < 0) return 0.0;
  return Math.max(0.0, Math.min(1.0, level / 100.0));
}

function batteryIcon(level, charging) {
  if (charging) return "󰂄";
  if (level === null || level === undefined || level < 0) return "󰂃";
  if (level >= 95) return "󰁹";
  if (level >= 85) return "󰂂";
  if (level >= 75) return "󰂁";
  if (level >= 65) return "󰂀";
  if (level >= 55) return "󰁿";
  if (level >= 45) return "󰁾";
  if (level >= 35) return "󰁽";
  if (level >= 25) return "󰁼";
  if (level >= 15) return "󰁻";
  return "󰂎";
}

function deviceTitle(status) {
  if (status && status.deviceName) return status.deviceName;
  if (status && status.modelName) return status.modelName;
  return "WH-1000XM5";
}

function showBattery(status) {
  return isLive(status) && (status.supported === null || status.supported.battery === true);
}

// Bar text: "80%", "—" when unknown or disconnected, "" when unsupported.
function barBatteryText(status) {
  if (!isLive(status)) return "—";
  if (!showBattery(status)) return "";
  return formatBattery(status.batteryLevel);
}

function isCharging(status) {
  return showBattery(status) && status.batteryCharging === true;
}

// Text beside the bar icon. Empty in icon-only mode and on a vertical bar,
// as the first-party Power widget does; the tooltip keeps the battery.
function barText(status, showPercentage, vertical) {
  if (showPercentage !== true || vertical === true) return "";
  return barBatteryText(status);
}

// The charging bolt goes with the percentage text.
function barChargingHint(status, showPercentage, vertical) {
  return barText(status, showPercentage, vertical) !== "" && isCharging(status);
}

function barTooltip(status) {
  if (!status || status.ok !== true) {
    if (status && status.schemaTooNew) return "Sony headphones: update the widget\n" + status.lastError;
    return "Sony headphones: " + (status && status.lastError ? status.lastError : "no status");
  }
  if (!status.connected) return "Sony headphones: disconnected";
  var lines = [deviceTitle(status)];
  if (isReading(status)) {
    lines.push("Reading headset settings…");
  } else {
    var parts = [];
    if (isSupported(status, "noise_control") && status.noiseMode) parts.push(noiseModeName(status.noiseMode));
    if (isSupported(status, "codec") && status.codec && status.codec !== "NONE") parts.push(codecLabel(status.codec));
    if (parts.length > 0) lines.push(parts.join(" · "));
  }
  if (showBattery(status) && status.batteryLevel >= 0) {
    lines.push("Battery " + formatBattery(status.batteryLevel) + (status.batteryCharging === true ? " (charging)" : ""));
  }
  return lines.join("\n");
}

function heroMeta(status) {
  if (!status || status.ok !== true) return status && status.schemaTooNew ? "Update needed" : "No status";
  if (!status.connected) return "Disconnected";
  if (isReading(status)) return "Reading settings";
  var parts = ["Connected"];
  if (isSupported(status, "codec") && status.codec && status.codec !== "NONE") parts.push(codecLabel(status.codec));
  if (isSupported(status, "worn") && status.worn !== null) parts.push(status.worn ? "On head" : "Off head");
  return parts.join(" · ");
}

// Body text for the states without controls.
function emptyStateText(status) {
  if (!status || status.ok !== true) {
    if (status && status.schemaTooNew) {
      var which = status.schemaVersion > 0 ? "status schema " + status.schemaVersion : "an unknown status schema";
      return "The daemon speaks " + which + ", this panel reads " + SUPPORTED_SCHEMA + ". Update the widget.";
    }
    return "The sony-headphones daemon wrote no usable status.";
  }
  if (!status.connected) return "Turn the headset on or connect it in Bluetooth settings.";
  return "";
}

function footerLines(status) {
  if (!isLive(status)) return [];
  var lines = [];
  var id = [];
  if (status.modelName) id.push(status.modelName);
  if (status.firmwareVersion) id.push("firmware " + status.firmwareVersion);
  if (status.address) id.push(status.address);
  if (id.length > 0) lines.push(id.join(" · "));
  if (status.supportedSource === "fallback") lines.push("Limited: headset did not report its features");
  if (status.schemaVersion === 1) lines.push("Old daemon (status schema 1)");
  return lines;
}

function elideError(text) {
  if (!text) return "";
  var cleaned = String(text).replace(/\s+/g, " ").trim();
  if (cleaned.length > MAX_ERROR_CHARS) {
    return cleaned.substring(0, ELIDED_ERROR_CHARS) + "…";
  }
  return cleaned;
}

function cycleNoiseMode(currentMode) {
  if (currentMode === NOISE_ANC) return NOISE_AMBIENT;
  if (currentMode === NOISE_AMBIENT) return NOISE_OFF;
  return NOISE_ANC;
}

// ---------------------------------------------------------------------------
// Panel cursor (the first-party recipe). A section is
// { id, kind, count, preferred, columns }:
//   walk      h/l walk the chips, j/k leave
//   grid      h/l walk, j/k move by `columns` and leave at the edges
//   rows      j/k walk the rows and leave at the ends, h/l adjust the row
//   adjust    one target, h/l adjust its value
//   collapse  one target, l expands, h collapses
//   single    one target, h/l do nothing
// ---------------------------------------------------------------------------

function _sectionIndex(sections, id) {
  for (var i = 0; i < sections.length; i++) if (sections[i].id === id) return i;
  return -1;
}

function _entryIndex(section, fromAbove) {
  var count = Math.max(1, section.count || 1);
  if (section.kind === "rows") return fromAbove ? 0 : count - 1;
  var preferred = Number(section.preferred);
  if (preferred >= 0 && preferred < count) return preferred;
  if (section.kind === "grid" && !fromAbove) {
    var cols = Math.max(1, section.columns || 1);
    return Math.floor((count - 1) / cols) * cols;
  }
  return 0;
}

function cursorClamp(cursor, sections) {
  if (!sections || sections.length === 0) return { section: "", index: 0 };
  var i = _sectionIndex(sections, cursor ? cursor.section : "");
  if (i < 0) return { section: sections[0].id, index: _entryIndex(sections[0], true) };
  var count = Math.max(1, sections[i].count || 1);
  var index = Math.max(0, Math.min(count - 1, Number(cursor.index) || 0));
  return { section: sections[i].id, index: index };
}

// Returns { section, index, action }; action is null,
// { type: "adjust", delta } or { type: "expand", value }.
function cursorMove(cursor, dx, dy, sections) {
  var current = cursorClamp(cursor, sections);
  current.action = null;
  if (current.section === "") return current;
  var si = _sectionIndex(sections, current.section);
  var section = sections[si];
  var count = Math.max(1, section.count || 1);

  if (dy !== 0) {
    if (section.kind === "rows") {
      var nextRow = current.index + dy;
      if (nextRow >= 0 && nextRow < count) return { section: section.id, index: nextRow, action: null };
    } else if (section.kind === "grid") {
      var cols = Math.max(1, section.columns || 1);
      var nextCell = current.index + dy * cols;
      if (nextCell >= 0 && nextCell < count) return { section: section.id, index: nextCell, action: null };
      var lastRowStart = Math.floor((count - 1) / cols) * cols;
      if (dy > 0 && current.index < lastRowStart) return { section: section.id, index: count - 1, action: null };
    }
    var ti = si + (dy > 0 ? 1 : -1);
    if (ti < 0 || ti >= sections.length) return current;
    var target = sections[ti];
    return { section: target.id, index: _entryIndex(target, dy > 0), action: null };
  }

  if (dx !== 0) {
    switch (section.kind) {
    case "walk":
    case "grid":
      return { section: section.id, index: Math.max(0, Math.min(count - 1, current.index + dx)), action: null };
    case "rows":
    case "adjust":
      current.action = { type: "adjust", delta: dx };
      return current;
    case "collapse":
      current.action = { type: "expand", value: dx > 0 };
      return current;
    default:
      return current;
    }
  }
  return current;
}

// ---------------------------------------------------------------------------
// Panel targets: one table for gating, chips, toggles, adjusters and cursor
// order (ui-spec 3 and 6). Fields:
//   kind      cursor kind (see cursorMove)
//   section   collapsible section that holds the target ("" = always open);
//             for a "collapse" target, the section it opens
//   feature   supported key; field / extra: status fields it needs
//   action    buildCommand action; toggle: flips `field`
//   v2        the verb does not exist for a schema 1 daemon
// ---------------------------------------------------------------------------

var TARGETS = {
  noise: { kind: "walk", section: "", feature: "noise_control", field: "noiseMode", action: "noise" },
  ambient: { kind: "adjust", section: "", feature: "noise_control", field: "ambientSoundLevel", action: "ambientLevel" },
  focusVoice: { kind: "single", section: "", feature: "noise_control", field: "focusOnVoice", action: "focusVoice", toggle: true, v2: true },
  sound: { kind: "collapse", section: "sound" },
  eqPreset: { kind: "grid", columns: 5, section: "sound", feature: "eq_preset", field: "eqPreset", action: "eqPreset" },
  dsee: { kind: "single", section: "sound", feature: "dsee", field: "dseeExtreme", action: "dsee", toggle: true },
  connectionQuality: { kind: "walk", section: "sound", feature: "connection_quality", field: "connectionQuality", action: "connectionQuality" },
  customEq: { kind: "collapse", section: "customEq" },
  eqBands: { kind: "rows", section: "customEq", feature: "eq_custom", field: "eqCustomBands", extra: "clearBass", action: "eqBand" },
  speakToChatHeader: { kind: "collapse", section: "speakToChat" },
  speakToChat: { kind: "single", section: "speakToChat", feature: "speak_to_chat", field: "speakToChat", action: "speakToChat", toggle: true },
  stcSensitivity: { kind: "walk", section: "speakToChat", feature: "speak_to_chat", field: "stcSensitivity", extra: "stcTimeout", action: "stcSensitivity", v2: true },
  stcTimeout: { kind: "walk", section: "speakToChat", feature: "speak_to_chat", field: "stcTimeout", extra: "stcSensitivity", action: "stcTimeout", v2: true },
  headset: { kind: "collapse", section: "headset" },
  volume: { kind: "adjust", section: "headset", feature: "volume", field: "volume", action: "volume" },
  voiceGuidance: { kind: "single", section: "headset", feature: "voice_guidance", field: "voiceGuidance", action: "voiceGuidance", toggle: true },
  voiceGuidanceVolume: { kind: "adjust", section: "headset", feature: "voice_guidance_volume", field: "voiceGuidanceVolume", action: "voiceGuidanceVolume" },
  autoPowerOff: { kind: "grid", columns: 4, section: "headset", feature: "auto_power_off", field: "autoPowerOff", action: "autoPowerOff" },
  ncAmbButton: { kind: "walk", section: "headset", feature: "nc_amb_button", field: "ncAmbButton", action: "ncAmbButton" },
  buttonFunction: { kind: "grid", columns: 3, section: "headset", feature: "button_function", field: "buttonFunction", action: "buttonFunction" },
  earDetection: { kind: "single", section: "headset", feature: "ear_detection", field: "earDetection", action: "earDetection", toggle: true },
  multipointHeader: { kind: "collapse", section: "multipoint" },
  multipoint: { kind: "single", section: "multipoint", feature: "multipoint", field: "multipoint", action: "multipoint", toggle: true },
  devices: { kind: "rows", section: "multipoint", feature: "devices", field: "devices", action: "source" }
};

var TARGET_ORDER = [
  "noise", "ambient", "focusVoice",
  "sound", "eqPreset", "dsee", "connectionQuality",
  "customEq", "eqBands",
  "speakToChatHeader", "speakToChat", "stcSensitivity", "stcTimeout",
  "headset", "volume", "voiceGuidance", "voiceGuidanceVolume", "autoPowerOff", "ncAmbButton", "buttonFunction", "earDetection",
  "multipointHeader", "multipoint", "devices"
];

// "hidden", "disabled" or "enabled" for a panel target.
function targetState(status, id) {
  var t = TARGETS[id];
  if (!t || !isLive(status)) return STATE_HIDDEN;
  if (t.kind === "collapse") return sectionVisible(status, t.section) ? STATE_ENABLED : STATE_HIDDEN;
  if (t.v2 && status.schemaVersion < 2) return STATE_HIDDEN;
  var state = controlState(status, t.feature, status[t.field]);
  if (state === STATE_ENABLED && t.extra && (status[t.extra] === null || status[t.extra] === undefined)) return STATE_DISABLED;
  return state;
}

function targetVisible(status, id) {
  return targetState(status, id) !== STATE_HIDDEN;
}

function targetEnabled(status, id) {
  return targetState(status, id) === STATE_ENABLED;
}

// Raw option values of a chip target.
function targetOptions(status, id) {
  switch (id) {
  case "noise": return NOISE_MODES.slice();
  case "eqPreset": return eqPresetOptions(status);
  case "connectionQuality": return CONNECTION_QUALITY_OPTIONS.slice();
  case "stcSensitivity": return STC_SENSITIVITY_OPTIONS.slice();
  case "stcTimeout": return STC_TIMEOUT_OPTIONS.slice();
  case "autoPowerOff": return autoPowerOffOptions(status);
  case "ncAmbButton": return NC_AMB_BUTTON_OPTIONS.slice();
  case "buttonFunction": return buttonFunctionOptions(status);
  default: return [];
  }
}

// Chips for a target: [{ value, label, tooltip }].
function targetChoices(status, id) {
  var label = {
    noise: noiseModeLabel, eqPreset: eqPresetButtonLabel, connectionQuality: connectionQualityLabel,
    stcSensitivity: stcSensitivityLabel, stcTimeout: stcTimeoutLabel, autoPowerOff: autoPowerOffLabel,
    ncAmbButton: ncAmbButtonLabel, buttonFunction: buttonFunctionLabel
  }[id];
  var tip = { autoPowerOff: autoPowerOffTooltip, ncAmbButton: ncAmbButtonTooltip }[id];
  var values = targetOptions(status, id);
  var out = [];
  for (var i = 0; i < values.length; i++) {
    out.push({ value: values[i], label: label(values[i]), tooltip: tip ? tip(values[i]) : "" });
  }
  return out;
}

function _targetCount(status, id) {
  var t = TARGETS[id];
  if (t.kind === "walk" || t.kind === "grid") return targetOptions(status, id).length;
  if (id === "eqBands") return 6;
  if (id === "devices") return Array.isArray(status.devices) ? status.devices.length : 0;
  return 1;
}

// Cursor targets in layout order (ui-spec 6). `expanded` maps a collapsible
// section name to a bool.
function cursorSections(status, expanded) {
  var list = [];
  if (!isLive(status)) return list;
  var ex = expanded || {};
  for (var i = 0; i < TARGET_ORDER.length; i++) {
    var id = TARGET_ORDER[i];
    var t = TARGETS[id];
    if (t.kind !== "collapse" && t.section !== "" && ex[t.section] !== true) continue;
    if (!targetVisible(status, id)) continue;
    var count = _targetCount(status, id);
    if (count < 1) continue;
    var entry = { id: id, kind: t.kind, count: count };
    if (t.columns) entry.columns = t.columns;
    if (t.kind === "walk" || t.kind === "grid") entry.preferred = targetOptions(status, id).indexOf(status[t.field]);
    list.push(entry);
  }
  return list;
}

// What Enter/Space or a click does on a target:
// { expand: section } for a header, { action, value } for a command, or null.
function targetActivation(status, id, index) {
  var t = TARGETS[id];
  if (!t) return null;
  if (t.kind === "collapse") return targetVisible(status, id) ? { expand: t.section } : null;
  if (!targetEnabled(status, id)) return null;
  if (t.toggle) return { action: t.action, value: status[t.field] !== true };
  if (t.kind === "walk" || t.kind === "grid") {
    var options = targetOptions(status, id);
    return index >= 0 && index < options.length ? { action: t.action, value: options[index] } : null;
  }
  if (id === "devices") {
    var d = Array.isArray(status.devices) ? status.devices[index] : null;
    return canSwitchTo(status, d) ? { action: "source", value: d.address } : null;
  }
  return null;
}

// What h/l does on an adjustable target: { action, value } or null.
function targetAdjustment(status, id, index, delta) {
  var t = TARGETS[id];
  if (!t || !targetEnabled(status, id)) return null;
  var value = adjustedValue(status, id, index, delta);
  return value === null ? null : { action: t.action, value: value };
}

// Value after a keyboard step on an adjustable target, or null.
function adjustedValue(status, id, index, delta) {
  if (!isLive(status)) return null;
  function step(v, min, max) {
    if (v === null || v === undefined) return null;
    var n = Math.max(min, Math.min(max, v + delta));
    return n === v ? null : n;
  }
  switch (id) {
  case "ambient":
    return step(status.ambientSoundLevel, AMBIENT_MIN, AMBIENT_MAX);
  case "volume":
    return step(status.volume, VOLUME_MIN, VOLUME_MAX);
  case "voiceGuidanceVolume":
    return step(status.voiceGuidanceVolume, GUIDANCE_VOLUME_MIN, GUIDANCE_VOLUME_MAX);
  case "eqBands": {
    if (!Array.isArray(status.eqCustomBands) || status.clearBass === null) return null;
    var cur = index === 5 ? status.clearBass : status.eqCustomBands[index];
    var next = step(cur, EQ_LEVEL_MIN, EQ_LEVEL_MAX);
    return next === null ? null : { index: index, level: next };
  }
  default:
    return null;
  }
}

// ---------------------------------------------------------------------------
// Widget settings: inline fields of the plugin's shell.json bar entry, the
// same store first-party widgets use. manifest.json declares the defaults and
// the schema; `omarchy bar set <id> <key> <value>` writes them as strings
// unless given --json, so a value is read leniently.
// ---------------------------------------------------------------------------

var SETTING_SHOW_PERCENTAGE = "showPercentage";
// Icon only by default, like the first-party Power widget.
var SETTING_DEFAULTS = { showPercentage: false };

function settingBool(settings, key) {
  var fallback = SETTING_DEFAULTS[key] === true;
  var v = settings ? settings[key] : undefined;
  if (v === true || v === false) return v;
  if (typeof v === "number") return v !== 0;
  if (typeof v === "string") {
    var s = v.trim().toLowerCase();
    if (s === "true" || s === "on" || s === "yes" || s === "1") return true;
    if (s === "false" || s === "off" || s === "no" || s === "0") return false;
  }
  return fallback;
}

function showBarPercentage(settings) {
  return settingBool(settings, SETTING_SHOW_PERCENTAGE);
}

// The bar entry to persist: the current fields, `values` on top, `id` first.
function settingsEntry(moduleName, settings, values) {
  var entry = { id: moduleName };
  var key;
  for (key in settings || {}) if (key !== "id") entry[key] = settings[key];
  for (key in values || {}) if (key !== "id") entry[key] = values[key];
  return entry;
}

// Popup targets for widget settings. They do not depend on the headset, so
// they follow the status targets and stay reachable while disconnected.
var SETTING_TARGETS = {
  barPercentage: { kind: "single", setting: SETTING_SHOW_PERCENTAGE }
};
var SETTING_TARGET_ORDER = ["barPercentage"];

function isSettingTarget(id) {
  return SETTING_TARGETS.hasOwnProperty(id);
}

// Status targets, then setting targets: the cursor list of the popup.
function panelCursorSections(status, expanded) {
  var list = cursorSections(status, expanded);
  for (var i = 0; i < SETTING_TARGET_ORDER.length; i++) {
    var id = SETTING_TARGET_ORDER[i];
    list.push({ id: id, kind: SETTING_TARGETS[id].kind, count: 1 });
  }
  return list;
}

// Enter/Space or a click on a setting target: the fields to persist, or null.
function settingActivation(settings, id) {
  var t = SETTING_TARGETS[id];
  if (!t) return null;
  var values = {};
  values[t.setting] = !settingBool(settings, t.setting);
  return values;
}
