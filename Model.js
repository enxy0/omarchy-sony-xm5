// Model.js
// Pure ECMAScript model for the Sony WH-1000XM5 / WF-1000XM5 bar widget.
// No QML dependencies; runs in QML, Deno and Node (tests/model.test.js).
// Contract: docs/backend-contract.md (status schema 3). UI: docs/ui-spec.md.

var SUPPORTED_SCHEMA = 3;
var LEVEL_UNKNOWN = -1;

var KIND_HEADPHONES = "headphones";
var KIND_EARBUDS = "earbuds";
var KINDS = [KIND_HEADPHONES, KIND_EARBUDS];
var MODELS = ["wh-1000xm5", "wf-1000xm5", "other"];
// Material Design Icons in the Nerd Font, the set the first-party bar
// widgets use (Audio shows the same headphones glyph for a headphone sink).
var GLYPH_HEADPHONES = "\u{F02CB}";
var GLYPH_EARBUDS = "\u{F184F}";
var GLYPH_BOLT = "\u{F0241}";

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
// Quick Access functions for a double or triple press of a headset key
// (protocol.md 5.18).
var QUICK_ACCESS_VALUES = ["off", "spotify"];
var QUICK_ACCESS_PRESSES = ["double", "triple"];
// Earbud touch sensor presets (contract 3.3, "Touch sensor function").
var TOUCH_FUNCTION_VALUES = [
  "ambient_sound_control", "playback_control", "volume_control", "voice_recognition",
  "google_assistant", "amazon_alexa", "quick_access", "none"
];
// ui-spec 7.9: offered while the headset sent no list (WF-1000XM5 fw 6.1.0).
var TOUCH_FUNCTION_DEFAULTS = ["ambient_sound_control", "playback_control", "none"];
var TOUCH_SIDES = ["left", "right"];

// Every `supported` key of schema 3 (contract 3.4).
var FEATURE_KEYS = [
  "battery", "battery_lr", "battery_case", "noise_control", "eq_preset", "eq_custom",
  "dsee", "speak_to_chat", "voice_guidance", "voice_guidance_volume", "auto_power_off",
  "nc_amb_button", "button_function", "touch_function", "ear_detection", "worn", "volume",
  "connection_quality", "codec", "multipoint", "devices", "source_switch", "touch_panel",
  "sidetone", "quick_access"
];
// Contract 6, rule 2: how a schema 1 file is read.
var V1_SUPPORTED_KEYS = [
  "battery", "noise_control", "eq_preset", "eq_custom", "dsee",
  "speak_to_chat", "ear_detection", "multipoint", "codec"
];

// Parsed field -> supported key that gates it.
var FIELD_FEATURES = {
  batteryLevel: "battery",
  batteryCharging: "battery",
  batteryLeft: "battery_lr",
  batteryLeftCharging: "battery_lr",
  batteryRight: "battery_lr",
  batteryRightCharging: "battery_lr",
  batteryCase: "battery_case",
  batteryCaseCharging: "battery_case",
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
  touchFunctionLeft: "touch_function",
  touchFunctionRight: "touch_function",
  touchFunctionLeftOptions: "touch_function",
  touchFunctionRightOptions: "touch_function",
  earDetection: "ear_detection",
  worn: "worn",
  wornLeft: "worn",
  wornRight: "worn",
  multipoint: "multipoint",
  devices: "devices",
  touchPanel: "touch_panel",
  sidetone: "sidetone",
  quickAccessDoubleTap: "quick_access",
  quickAccessTripleTap: "quick_access",
  quickAccessDoubleTapOptions: "quick_access",
  quickAccessTripleTapOptions: "quick_access"
};

// Popup sections and the supported keys of their controls.
var SECTION_FEATURES = {
  noise: ["noise_control"],
  sound: ["eq_preset", "dsee", "connection_quality", "codec"],
  customEq: ["eq_custom"],
  speakToChat: ["speak_to_chat"],
  headset: ["volume", "voice_guidance", "voice_guidance_volume", "auto_power_off",
    "nc_amb_button", "button_function", "quick_access", "ear_detection", "touch_panel",
    "touch_function", "sidetone"],
  multipoint: ["multipoint", "devices"]
};

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

// A parsed status is a "view": the file-level keys (FILE_KEYS) plus the
// fields of one headset of `headsets` (the primary one for parseStatus, any
// one for headsetView). Every gating, label and command function takes a
// view, so they work the same for one headset or several.
var FILE_KEYS = ["ok", "lastError", "schemaVersion", "schemaTooNew", "lastUpdated", "primary", "headsets"];

function defaultStatus() {
  var out = {
    ok: false,
    lastError: "",
    schemaVersion: 0,
    schemaTooNew: false,
    connected: false,
    lastUpdated: 0,
    primary: null,
    headsets: []
  };
  var h = defaultHeadset();
  for (var k in h) out[k] = h[k];
  return out;
}

// Fields of one headset entry, all unknown.
function defaultHeadset() {
  return {
    id: null,
    model: null,
    kind: KIND_HEADPHONES,
    connectedSince: 0,
    deviceName: "",
    address: null,
    modelName: null,
    firmwareVersion: null,
    supported: null,
    supportedSource: null,
    batteryLevel: LEVEL_UNKNOWN,
    batteryCharging: null,
    batteryLeft: null,
    batteryLeftCharging: null,
    batteryRight: null,
    batteryRightCharging: null,
    batteryCase: null,
    batteryCaseCharging: null,
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
    touchFunctionLeft: null,
    touchFunctionRight: null,
    touchFunctionLeftOptions: null,
    touchFunctionRightOptions: null,
    earDetection: null,
    worn: null,
    wornLeft: null,
    wornRight: null,
    multipoint: null,
    devices: null,
    touchPanel: null,
    sidetone: null,
    quickAccessDoubleTap: null,
    quickAccessTripleTap: null,
    quickAccessDoubleTapOptions: null,
    quickAccessTripleTapOptions: null
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

// Battery percentage 0..100, or null when unknown (negative or not a number).
function _level(v) {
  var n = clamp(v, -1, 100, null);
  return n === null || n < 0 ? null : n;
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
  res.lastUpdated = clamp(parsed.last_updated, 0, Number.MAX_SAFE_INTEGER, 0);

  var headsets = [];
  if (version >= 3) {
    var list = Array.isArray(parsed.headsets) ? parsed.headsets : [];
    var seen = {};
    for (var n = 0; n < list.length; n++) {
      if (!list[n] || typeof list[n] !== "object" || Array.isArray(list[n])) continue;
      var h = _parseHeadset(list[n], version, res.lastUpdated);
      // Contract 3.1: `id` is a Bluetooth address, the selector and the tab
      // key. An entry without one cannot be addressed, so it is skipped.
      if (h === null || seen[h.id] === true) continue;
      seen[h.id] = true;
      headsets.push(h);
    }
  } else if (parsed.connected === true) {
    // Contract 6, rule 2: a schema 1 or 2 file is one headset.
    headsets.push(_parseHeadset(parsed, version, res.lastUpdated));
  }
  res.headsets = headsets;
  // Contract 3.5: a `primary` that names no entry is read as the first entry.
  var primary = version >= 3 ? _address(parsed.primary) : (headsets.length > 0 ? headsets[0].id : null);
  if (findHeadset(res, primary) === null) primary = headsets.length > 0 ? headsets[0].id : null;
  res.primary = primary;
  return headsetView(res, primary);
}

// One headset entry: a schema 3 `headsets[]` object, or the top level of a
// schema 1 or 2 file. Returns null for a schema 3 entry without a valid id.
function _parseHeadset(parsed, version, lastUpdated) {
  var res = defaultHeadset();
  var v1 = version < 2;
  // Schema 3 removed the deprecated aliases; older files may carry them.
  var legacy = version < 3;

  if (legacy) {
    res.address = _address(parsed.address);
    res.id = res.address !== null ? res.address : "unknown";
    res.model = "wh-1000xm5";
    res.kind = KIND_HEADPHONES;
    res.connectedSince = lastUpdated;
  } else {
    res.id = _address(parsed.id);
    if (res.id === null) return null;
    var addr = _address(parsed.address);
    res.address = addr !== null ? addr : res.id;
    // Contract 6, rules 5 and 6: unknown model reads as other, unknown kind
    // as headphones.
    var model = _enumValue(parsed.model, MODELS);
    res.model = model !== null ? model : "other";
    var kind = _enumValue(parsed.kind, KINDS);
    res.kind = kind !== null ? kind : KIND_HEADPHONES;
    res.connectedSince = clamp(parsed.connected_since, 0, Number.MAX_SAFE_INTEGER, 0);
  }
  res.deviceName = typeof parsed.device_name === "string" ? parsed.device_name : "";
  res.modelName = _string(parsed.model_name);
  res.firmwareVersion = _string(parsed.firmware_version);

  if (v1) {
    res.supported = v1Supported();
    res.supportedSource = null;
  } else {
    res.supported = _supported(parsed.supported);
    res.supportedSource = res.supported ? _enumValue(parsed.supported_source, ["device", "fallback"]) : null;
  }

  // Battery: v1 used -1 for unknown, v2 and v3 write null.
  var bl = clamp(parsed.battery_level, -1, 100, LEVEL_UNKNOWN);
  res.batteryLevel = bl < 0 ? LEVEL_UNKNOWN : bl;
  res.batteryCharging = _bool(_firstDefined(parsed.battery_charging, legacy ? parsed.charging : null));
  res.batteryLeft = _level(parsed.battery_left);
  res.batteryLeftCharging = _bool(parsed.battery_left_charging);
  res.batteryRight = _level(parsed.battery_right);
  res.batteryRightCharging = _bool(parsed.battery_right_charging);
  res.batteryCase = _level(parsed.battery_case);
  res.batteryCaseCharging = _bool(parsed.battery_case_charging);

  res.noiseMode = _enumValue(parsed.noise_mode, v1 ? [NOISE_ANC, NOISE_AMBIENT, NOISE_WIND, NOISE_OFF] : NOISE_MODES);
  res.ambientSoundLevel = clamp(_firstDefined(parsed.ambient_sound_level, legacy ? parsed.ambient_level : null), AMBIENT_MIN, AMBIENT_MAX, null);
  res.focusOnVoice = _bool(parsed.voice_passthrough);

  res.eqPreset = _enumValue(parsed.eq_preset, EQ_PRESET_VALUES.concat([EQ_UNKNOWN]));
  res.eqPresets = _enumList(parsed.eq_presets, EQ_PRESET_VALUES);
  var rawBands = _firstDefined(parsed.eq_custom_bands, legacy ? parsed.eq_bands : null);
  if (Array.isArray(rawBands)) {
    var bands = [];
    for (var i = 0; i < 5; i++) bands.push(clamp(rawBands[i], EQ_LEVEL_MIN, EQ_LEVEL_MAX, 0));
    res.eqCustomBands = bands;
  }
  res.clearBass = clamp(parsed.clear_bass, EQ_LEVEL_MIN, EQ_LEVEL_MAX, null);

  res.dseeExtreme = _bool(_firstDefined(parsed.dsee_extreme, legacy ? parsed.dsee : null));
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
  res.touchFunctionLeft = _enumValue(parsed.touch_function_left, TOUCH_FUNCTION_VALUES.concat(["other"]));
  res.touchFunctionRight = _enumValue(parsed.touch_function_right, TOUCH_FUNCTION_VALUES.concat(["other"]));
  res.touchFunctionLeftOptions = _enumList(parsed.touch_function_left_options, TOUCH_FUNCTION_VALUES);
  res.touchFunctionRightOptions = _enumList(parsed.touch_function_right_options, TOUCH_FUNCTION_VALUES);

  res.earDetection = _bool(parsed.ear_detection);
  res.worn = _bool(parsed.worn);
  res.wornLeft = _bool(parsed.worn_left);
  res.wornRight = _bool(parsed.worn_right);
  res.multipoint = _bool(parsed.multipoint);
  res.devices = _devices(parsed.devices);

  // Added within schema 2 or 3: a daemon without the supported key has no
  // field either, so it stays null (contract 6, rules 2 and 5).
  res.touchPanel = _bool(parsed.touch_panel);
  res.sidetone = _bool(parsed.sidetone);
  res.quickAccessDoubleTap = _enumValue(parsed.quick_access_double_tap, QUICK_ACCESS_VALUES.concat(["other"]));
  res.quickAccessTripleTap = _enumValue(parsed.quick_access_triple_tap, QUICK_ACCESS_VALUES.concat(["other"]));
  res.quickAccessDoubleTapOptions = _enumList(parsed.quick_access_double_tap_options, QUICK_ACCESS_VALUES);
  res.quickAccessTripleTapOptions = _enumList(parsed.quick_access_triple_tap_options, QUICK_ACCESS_VALUES);

  // A false supported key means the matching fields are null (contract 3.4).
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
// Headsets and selection (ui-spec 8)
// ---------------------------------------------------------------------------

function _headsets(status) {
  return status && Array.isArray(status.headsets) ? status.headsets : [];
}

// The entry of `headsets` with this id, or null.
function findHeadset(status, id) {
  var list = _headsets(status);
  if (id === null || id === undefined) return null;
  for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i];
  return null;
}

function headsetIndex(status, id) {
  var list = _headsets(status);
  for (var i = 0; i < list.length; i++) if (list[i].id === id) return i;
  return -1;
}

function headsetCount(status) {
  return status && status.ok === true ? _headsets(status).length : 0;
}

// The view of headset `id`: the file-level keys plus that entry's fields.
// An id that is not in `headsets` gives the primary headset; with no
// headset at all the view is "not connected".
function headsetView(status, id) {
  if (!status || status.ok !== true) return status;
  var h = findHeadset(status, id);
  if (h === null) h = findHeadset(status, status.primary);
  var out = {};
  for (var i = 0; i < FILE_KEYS.length; i++) out[FILE_KEYS[i]] = status[FILE_KEYS[i]];
  var src = h !== null ? h : defaultHeadset();
  for (var k in src) out[k] = src[k];
  out.connected = h !== null;
  return out;
}

// Id of the headset the popup shows: `selectedId` while it is connected,
// else the primary headset (ui-spec 8).
function shownHeadsetId(status, selectedId) {
  if (findHeadset(status, selectedId) !== null) return selectedId;
  return status && status.ok === true ? status.primary : null;
}

function shownHeadset(status, selectedId) {
  return headsetView(status, shownHeadsetId(status, selectedId));
}

// `selectedId` after the popup opens: kept while that headset is connected,
// else the primary headset.
function selectionOnOpen(status, selectedId) {
  if (findHeadset(status, selectedId) !== null) return selectedId;
  return status && status.ok === true ? status.primary : null;
}

// The headset `delta` tabs away from `shownId` (`[` / `]`), no wrap. Null
// at the ends and with fewer than two headsets.
function stepHeadset(status, shownId, delta) {
  if (!switcherVisible(status)) return null;
  var list = _headsets(status);
  var i = headsetIndex(status, shownId);
  if (i < 0) return null;
  var j = i + (delta > 0 ? 1 : -1);
  return j >= 0 && j < list.length ? list[j].id : null;
}

// The switcher row exists only with two or more headsets.
function switcherVisible(status) {
  return headsetCount(status) >= 2;
}

function isEarbuds(status) {
  return !!status && status.kind === KIND_EARBUDS;
}

function headsetGlyph(kind) {
  return kind === KIND_EARBUDS ? GLYPH_EARBUDS : GLYPH_HEADPHONES;
}

// Name of a headset entry or view.
function headsetLabel(h) {
  if (h && h.deviceName) return h.deviceName;
  if (h && h.modelName) return h.modelName;
  if (h && h.model === "wf-1000xm5") return "WF-1000XM5";
  if (h && h.model === "wh-1000xm5") return "WH-1000XM5";
  return "Sony headset";
}

// Tabs of the switcher: [{ id, kind, label, battery, charging, selected }].
function switcherTabs(status, shownId) {
  var out = [];
  if (!switcherVisible(status)) return out;
  var list = _headsets(status);
  for (var i = 0; i < list.length; i++) {
    var h = list[i];
    var reading = h.supported === null;
    out.push({
      id: h.id,
      kind: h.kind,
      label: headsetLabel(h),
      battery: reading ? "…" : formatBattery(barBattery(h)),
      charging: !reading && barCharging(h),
      selected: h.id === shownId
    });
  }
  return out;
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

// Functions the headset accepts for a press ("double" or "triple"); without a
// list, every contract value (the daemon skips its check while the list is
// null, contract 3.3).
function quickAccessOptions(status, press) {
  var field = press === "triple" ? "quickAccessTripleTapOptions" : "quickAccessDoubleTapOptions";
  var list = status && Array.isArray(status[field]) ? status[field] : null;
  return list && list.length > 0 ? list.slice() : QUICK_ACCESS_VALUES.slice();
}

// Presets one earbud ("left" or "right") accepts; without a list, the
// WF-1000XM5 list (ui-spec 7.9; the daemon skips its check while null).
function touchFunctionOptions(status, side) {
  var field = side === "right" ? "touchFunctionRightOptions" : "touchFunctionLeftOptions";
  var list = status && Array.isArray(status[field]) ? status[field] : null;
  return list && list.length > 0 ? list.slice() : TOUCH_FUNCTION_DEFAULTS.slice();
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

// Action table: feature key, value check and command shape. `status` is the
// view of the headset the command is for.
// Returns { args, pending, feature, headset } or { error }. For schema 3
// `args` starts with `--headset <id>`, also with one headset (ui-spec 5); an
// old daemon's CLI does not know the option (contract 6, rule 2).
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
  var args = status.schemaVersion >= 3 ? ["--headset", status.id].concat(spec.args) : spec.args;
  return { args: args, pending: spec.pending, feature: spec.feature, headset: status.id };
}

// The sony-ctl arguments without the `--headset <id>` selector.
function commandVerbArgs(args) {
  return Array.isArray(args) && args[0] === "--headset" ? args.slice(2) : args;
}

// The headset a command names with `--headset`, or null.
function commandHeadset(args) {
  return Array.isArray(args) && args[0] === "--headset" ? args[1] : null;
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
  case "touchPanel":
    return _boolSpec("touch_panel", "touch-panel", "touchPanel", value);
  case "sidetone":
    return _boolSpec("sidetone", "sidetone", "sidetone", value);
  case "touchFunctionLeft":
  case "touchFunctionRight": {
    var side = action === "touchFunctionRight" ? "right" : "left";
    var tfField = side === "right" ? "touchFunctionRight" : "touchFunctionLeft";
    var tf = _enumSpec("touch_function", "touch-function", tfField, value, touchFunctionOptions(status, side));
    if (tf.args) tf.args = ["touch-function", side, value];
    return tf;
  }
  case "quickAccessDouble":
  case "quickAccessTriple": {
    var press = action === "quickAccessTriple" ? "triple" : "double";
    var field = press === "triple" ? "quickAccessTripleTap" : "quickAccessDoubleTap";
    var qa = _enumSpec("quick_access", "quick-access", field, value, quickAccessOptions(status, press));
    if (qa.args) qa.args = ["quick-access", press, value];
    return qa;
  }
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
// Optimistic values. `pending` maps "<headset id>|<field>" to
// { value, id, headset, field }; `id` is the command that set it and
// `headset` the headset it belongs to (null: the primary headset).
// ---------------------------------------------------------------------------

function _same(a, b) {
  return JSON.stringify(a) === JSON.stringify(b);
}

function addPending(pending, fields, id, headset) {
  var hid = headset === undefined ? null : headset;
  var out = {};
  for (var k in pending) out[k] = pending[k];
  for (var f in fields) out[(hid === null ? "" : hid) + "|" + f] = { value: fields[f], id: id, headset: hid, field: f };
  return out;
}

function dropPending(pending, id) {
  var out = {};
  for (var k in pending) if (pending[k].id !== id) out[k] = pending[k];
  return out;
}

// The fields a pending entry compares against: the view itself when the
// entry is for its headset (a headset-less entry is for the primary one,
// which parseStatus returns), else that headset's entry.
function _pendingTarget(status, entry) {
  if (entry.headset === null) return isLive(status) && status.id === status.primary ? status : null;
  if (isLive(status) && entry.headset === status.id) return status;
  return findHeadset(status, entry.headset);
}

// Removes every entry that the daemon now reports, and every entry of a
// headset that left `headsets` (ui-spec 5). An unreadable file keeps them.
function reconcilePending(pending, status) {
  if (!status || status.ok !== true) return pending;
  var out = {};
  var changed = false;
  for (var k in pending) {
    var e = pending[k];
    var target = _pendingTarget(status, e);
    if (target === null) {
      if (e.headset === null) out[k] = e;
      else changed = true;
    } else if (_same(target[e.field], e.value)) {
      changed = true;
    } else {
      out[k] = e;
    }
  }
  return changed ? out : pending;
}

function hasPending(pending) {
  for (var k in pending) return true;
  return false;
}

// The status with the optimistic values on their headsets; the result is a
// view of the same headset as `status`.
function applyPending(status, pending) {
  if (!isLive(status) || !hasPending(pending)) return status;
  var byHeadset = {};
  var any = false;
  for (var k in pending) {
    var e = pending[k];
    var hid = e.headset === null ? status.primary : e.headset;
    if (findHeadset(status, hid) === null) continue;
    if (!byHeadset[hid]) byHeadset[hid] = {};
    byHeadset[hid][e.field] = e.value;
    any = true;
  }
  if (!any) return status;
  var list = _headsets(status);
  var headsets = [];
  for (var i = 0; i < list.length; i++) {
    var fields = byHeadset[list[i].id];
    if (!fields) {
      headsets.push(list[i]);
      continue;
    }
    var h = {};
    for (var f in list[i]) h[f] = list[i][f];
    for (var g in fields) h[g] = fields[g];
    headsets.push(h);
  }
  var next = {};
  for (var j = 0; j < FILE_KEYS.length; j++) next[FILE_KEYS[j]] = status[FILE_KEYS[j]];
  next.headsets = headsets;
  return headsetView(next, status.id);
}

// ---------------------------------------------------------------------------
// Command queue. Items are { args, id, headset }. A queued command with the
// same key is dropped and the new one goes to the end, so the last intent
// wins. Returns { queue, dropped } where `dropped` lists the removed ids.
// ---------------------------------------------------------------------------

// The headset id plus the verb. `quick-access` and `touch-function` set one
// press or one side per command, so that is part of the key: a triple-tap
// change must not drop a queued double-tap change, and a WH-1000XM5 volume
// change must not drop a queued WF-1000XM5 one.
function commandKey(args) {
  var a = commandVerbArgs(args);
  var key = (a[0] === "quick-access" || a[0] === "touch-function") ? a[0] + " " + a[1] : a[0];
  var hid = commandHeadset(args);
  return hid === null ? key : hid + " " + key;
}

// Drops queued commands of headsets that left `headsets` (ui-spec 5). An
// unreadable file keeps them. Returns { queue, dropped }.
function pruneQueue(queue, status) {
  if (!status || status.ok !== true) return { queue: queue, dropped: [] };
  var out = [];
  var dropped = [];
  for (var i = 0; i < queue.length; i++) {
    var hid = queue[i].headset;
    if (hid === undefined || hid === null || findHeadset(status, hid) !== null) out.push(queue[i]);
    else dropped.push(queue[i].id);
  }
  return dropped.length > 0 ? { queue: out, dropped: dropped } : { queue: queue, dropped: [] };
}

function enqueueCommand(queue, item) {
  var out = [];
  var dropped = [];
  var key = commandKey(item.args);
  for (var i = 0; i < queue.length; i++) {
    if (commandKey(queue[i].args) === key) dropped.push(queue[i].id);
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

function quickAccessLabel(v) {
  switch (v) {
    case "off": return "Off";
    case "spotify": return "Spotify";
    case "other": return "Other";
    default: return "Unknown";
  }
}

// Read-only line for a press whose function the contract does not name.
function quickAccessOtherText(status) {
  if (!status) return "";
  var presses = [];
  if (status.quickAccessDoubleTap === "other") presses.push("Double tap");
  if (status.quickAccessTripleTap === "other") presses.push("Triple tap");
  if (presses.length === 0) return "";
  return presses.join(" and ") + (presses.length > 1 ? " use functions" : " uses a function") + " this panel does not name.";
}

function touchFunctionLabel(v) {
  switch (v) {
    case "ambient_sound_control": return "Noise control";
    case "playback_control": return "Playback";
    case "volume_control": return "Volume";
    case "voice_recognition": return "Voice assistant";
    case "google_assistant": return "Google Assistant";
    case "amazon_alexa": return "Alexa";
    case "quick_access": return "Quick Access";
    case "none": return "Off";
    case "other": return "Other";
    default: return "Unknown";
  }
}

function touchFunctionTooltip(v) {
  switch (v) {
    case "ambient_sound_control": return "Tap: noise mode. Double or triple tap: Quick Access. Hold: Quick Attention";
    case "playback_control": return "Tap: play or pause. Double tap: next. Triple tap: previous";
    case "volume_control": return "Tap: volume up or down";
    case "none": return "Taps do nothing";
    default: return "";
  }
}

// Read-only line for an earbud whose preset the contract does not name.
function touchFunctionOtherText(status) {
  if (!status) return "";
  var sides = [];
  if (status.touchFunctionLeft === "other") sides.push("Left earbud");
  if (status.touchFunctionRight === "other") sides.push("Right earbud");
  if (sides.length === 0) return "";
  return sides.join(" and ") + (sides.length > 1 ? " use functions" : " uses a function") + " this panel does not name.";
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
  return isLive(status) ? headsetLabel(status) : "Sony headset";
}

// Contract 3.3, "Bar battery": the level the bar shows for a headset entry
// or view. Headphones: battery_level. Earbuds: the lower earbud, ignoring
// an unknown one; the case does not count. Null when unknown.
function barBattery(h) {
  if (!h) return null;
  if (h.kind === KIND_EARBUDS) {
    var l = typeof h.batteryLeft === "number" ? h.batteryLeft : null;
    var r = typeof h.batteryRight === "number" ? h.batteryRight : null;
    if (l === null) return r;
    if (r === null) return l;
    return Math.min(l, r);
  }
  return typeof h.batteryLevel === "number" && h.batteryLevel >= 0 ? h.batteryLevel : null;
}

// Earbuds charge when every known earbud flag is true (at least one known).
function barCharging(h) {
  if (!h) return false;
  if (h.kind === KIND_EARBUDS) {
    var known = 0;
    var flags = [h.batteryLeftCharging, h.batteryRightCharging];
    for (var i = 0; i < flags.length; i++) {
      if (flags[i] === null || flags[i] === undefined) continue;
      if (flags[i] !== true) return false;
      known++;
    }
    return known > 0;
  }
  return h.batteryCharging === true;
}

// The bar shows a battery when the headset has one it reports.
function showBattery(status) {
  return isLive(status) && (status.supported === null || status.supported.battery === true || status.supported.battery_lr === true);
}

// Bar text: "80%", "—" when unknown or disconnected, "" when unsupported.
function barBatteryText(status) {
  if (!isLive(status)) return "—";
  if (!showBattery(status)) return "";
  return formatBattery(barBattery(status));
}

function isCharging(status) {
  return showBattery(status) && barCharging(status);
}

// Bar battery as the icon tint input: the level, or -1 when unknown.
function barBatteryLevel(status) {
  if (!showBattery(status)) return LEVEL_UNKNOWN;
  var level = barBattery(status);
  return level === null ? LEVEL_UNKNOWN : level;
}

// Earbuds hero, first battery line: [{ side, text, low, charging }] for the
// left and the right earbud. A side value below 20 % is low; the case never.
function earbudBatteryParts(status) {
  var out = [];
  var sides = [
    { side: "L", level: status ? status.batteryLeft : null, charging: status ? status.batteryLeftCharging : null },
    { side: "R", level: status ? status.batteryRight : null, charging: status ? status.batteryRightCharging : null }
  ];
  for (var i = 0; i < sides.length; i++) {
    var s = sides[i];
    var known = typeof s.level === "number";
    out.push({ side: s.side, text: s.side + " " + formatBattery(known ? s.level : null), low: known && s.level < 20, charging: s.charging === true });
  }
  return out;
}

function caseBatteryText(status) {
  return "Case " + formatBattery(status && typeof status.batteryCase === "number" ? status.batteryCase : null);
}

function caseCharging(status) {
  return !!status && status.batteryCaseCharging === true;
}

// Earbuds: where the earbuds are, from worn_left and worn_right. Empty
// when a side is unknown.
function earbudsWornText(status) {
  if (!status) return "";
  var l = status.wornLeft;
  var r = status.wornRight;
  if (l === null || l === undefined || r === null || r === undefined) return "";
  if (l && r) return "In ears";
  if (l) return "Left in ear";
  if (r) return "Right in ear";
  return "Not in ears";
}

// Worn part of the hero meta line: "On head" / "Off head" for headphones,
// the per-side text for earbuds.
function wornText(status) {
  if (!isSupported(status, "worn")) return "";
  if (isEarbuds(status)) return earbudsWornText(status);
  if (status.worn === null) return "";
  return status.worn ? "On head" : "Off head";
}

// Battery for the tooltip: headphones "80%", earbuds "L 70% R 65% case 53%"
// (`sep` between the parts). `long` adds "(charging)" after a charging part.
// Empty when the headset reports no battery.
function _tooltipBattery(h, sep, long) {
  var supported = h.supported || {};
  function part(text, charging) { return text + (long && charging === true ? " (charging)" : ""); }
  if (h.kind === KIND_EARBUDS) {
    var parts = [];
    if (supported.battery_lr === true) {
      parts.push(part("L " + formatBattery(h.batteryLeft), h.batteryLeftCharging));
      parts.push(part("R " + formatBattery(h.batteryRight), h.batteryRightCharging));
    }
    if (supported.battery_case === true) parts.push(part("case " + formatBattery(h.batteryCase), h.batteryCaseCharging));
    return parts.join(sep);
  }
  if (supported.battery !== true || !(h.batteryLevel >= 0)) return "";
  return part(formatBattery(h.batteryLevel), h.batteryCharging);
}

// "<name>: <noise mode> · <battery>" for the tooltip with two or more headsets.
function _tooltipHeadsetLine(h, isPrimary) {
  var line;
  if (h.supported === null) {
    line = headsetLabel(h) + ": reading headset settings…";
  } else {
    var parts = [];
    if (h.supported.noise_control === true && h.noiseMode) parts.push(noiseModeName(h.noiseMode));
    var battery = _tooltipBattery(h, " ", false);
    if (battery !== "") parts.push(battery);
    line = headsetLabel(h) + (parts.length > 0 ? ": " + parts.join(" · ") : "");
  }
  return line + (isPrimary ? " (bar)" : "");
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
  var list = _headsets(status);
  if (list.length >= 2) {
    var multi = [];
    for (var i = 0; i < list.length; i++) multi.push(_tooltipHeadsetLine(list[i], list[i].id === status.primary));
    return multi.join("\n");
  }
  if (isReading(status)) return deviceTitle(status) + ": reading headset settings…";
  var lines = [deviceTitle(status)];
  var parts = [];
  if (isSupported(status, "noise_control") && status.noiseMode) parts.push(noiseModeName(status.noiseMode));
  if (isSupported(status, "codec") && status.codec && status.codec !== "NONE") parts.push(codecLabel(status.codec));
  if (parts.length > 0) lines.push(parts.join(" · "));
  var battery = _tooltipBattery(status, " · ", true);
  if (battery !== "") lines.push("Battery " + battery);
  return lines.join("\n");
}

function heroMeta(status) {
  if (!status || status.ok !== true) return status && status.schemaTooNew ? "Update needed" : "No status";
  if (!status.connected) return "Disconnected";
  if (isReading(status)) return "Reading settings";
  var parts = ["Connected"];
  if (isSupported(status, "codec") && status.codec && status.codec !== "NONE") parts.push(codecLabel(status.codec));
  var worn = wornText(status);
  if (worn !== "") parts.push(worn);
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
  if (status.schemaVersion < SUPPORTED_SCHEMA) lines.push("Old daemon (status schema " + status.schemaVersion + ")");
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
//   kind      cursor kind (see cursorMove); "display" = read-only hero item
//             without a cursor target
//   section   collapsible section that holds the target ("" = always open);
//             for a "collapse" target, the section it opens
//   feature   supported key; field / extra: status fields it needs
//   action    buildCommand action; toggle: flips `field`
//   v2        the verb does not exist for a schema 1 daemon
// ---------------------------------------------------------------------------

var TARGETS = {
  battery: { kind: "display", section: "", feature: "battery", field: "batteryLevel" },
  batteryLr: { kind: "display", section: "", feature: "battery_lr", field: "batteryLeft", extra: "batteryRight" },
  batteryCase: { kind: "display", section: "", feature: "battery_case", field: "batteryCase" },
  wornSides: { kind: "display", section: "", feature: "worn", field: "wornLeft", extra: "wornRight" },
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
  quickAccessDouble: { kind: "walk", section: "headset", feature: "quick_access", field: "quickAccessDoubleTap", action: "quickAccessDouble" },
  quickAccessTriple: { kind: "walk", section: "headset", feature: "quick_access", field: "quickAccessTripleTap", action: "quickAccessTriple" },
  earDetection: { kind: "single", section: "headset", feature: "ear_detection", field: "earDetection", action: "earDetection", toggle: true },
  touchPanel: { kind: "single", section: "headset", feature: "touch_panel", field: "touchPanel", action: "touchPanel", toggle: true },
  touchFunctionLeft: { kind: "walk", section: "headset", feature: "touch_function", field: "touchFunctionLeft", action: "touchFunctionLeft" },
  touchFunctionRight: { kind: "walk", section: "headset", feature: "touch_function", field: "touchFunctionRight", action: "touchFunctionRight" },
  sidetone: { kind: "single", section: "headset", feature: "sidetone", field: "sidetone", action: "sidetone", toggle: true },
  multipointHeader: { kind: "collapse", section: "multipoint" },
  multipoint: { kind: "single", section: "multipoint", feature: "multipoint", field: "multipoint", action: "multipoint", toggle: true },
  devices: { kind: "rows", section: "multipoint", feature: "devices", field: "devices", action: "source" }
};

var TARGET_ORDER = [
  "battery", "batteryLr", "batteryCase", "wornSides",
  "noise", "ambient", "focusVoice",
  "sound", "eqPreset", "dsee", "connectionQuality",
  "customEq", "eqBands",
  "speakToChatHeader", "speakToChat", "stcSensitivity", "stcTimeout",
  "headset", "volume", "voiceGuidance", "voiceGuidanceVolume", "autoPowerOff", "ncAmbButton", "buttonFunction",
  "quickAccessDouble", "quickAccessTriple", "earDetection", "touchPanel",
  "touchFunctionLeft", "touchFunctionRight", "sidetone",
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
  case "quickAccessDouble": return quickAccessOptions(status, "double");
  case "quickAccessTriple": return quickAccessOptions(status, "triple");
  case "touchFunctionLeft": return touchFunctionOptions(status, "left");
  case "touchFunctionRight": return touchFunctionOptions(status, "right");
  default: return [];
  }
}

// Chips for a target: [{ value, label, tooltip }].
function targetChoices(status, id) {
  var label = {
    noise: noiseModeLabel, eqPreset: eqPresetButtonLabel, connectionQuality: connectionQualityLabel,
    stcSensitivity: stcSensitivityLabel, stcTimeout: stcTimeoutLabel, autoPowerOff: autoPowerOffLabel,
    ncAmbButton: ncAmbButtonLabel, buttonFunction: buttonFunctionLabel,
    quickAccessDouble: quickAccessLabel, quickAccessTriple: quickAccessLabel,
    touchFunctionLeft: touchFunctionLabel, touchFunctionRight: touchFunctionLabel
  }[id];
  if (!label) return [];
  var tip = {
    autoPowerOff: autoPowerOffTooltip, ncAmbButton: ncAmbButtonTooltip,
    touchFunctionLeft: touchFunctionTooltip, touchFunctionRight: touchFunctionTooltip
  }[id];
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
    if (t.kind === "display") continue;
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

// The switcher row: the first cursor row with two or more headsets. `h`/`l`
// walk the tabs; entering it lands on the shown headset's tab.
var SWITCHER_TARGET = "switcher";

// The headset id of switcher tab `index`, or null.
function switcherActivation(status, index) {
  if (!switcherVisible(status)) return null;
  var list = _headsets(status);
  return index >= 0 && index < list.length ? list[index].id : null;
}

// Switcher, status targets, then setting targets: the cursor list of the
// popup. `status` is the view of the shown headset.
function panelCursorSections(status, expanded) {
  var list = [];
  if (switcherVisible(status)) {
    list.push({ id: SWITCHER_TARGET, kind: "walk", count: headsetCount(status), preferred: headsetIndex(status, status.id) });
  }
  list = list.concat(cursorSections(status, expanded));
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
