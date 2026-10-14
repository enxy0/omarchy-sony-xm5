# Backend contract (schema version 2)

The daemon (`sony-headphones-daemon`) owns the RFCOMM link to the headset.
The bar widget (`Service.qml`, `Model.js`, `Panel.qml`) never talks to the
headset. The two sides share exactly two interfaces:

- **status.json**: the daemon writes the full headset state to a file; the
  widget watches the file.
- **sony-ctl**: the widget runs the CLI once per change; the CLI sends one
  line to the daemon socket and prints one reply line.

Byte-level protocol details are in [`protocol.md`](protocol.md). This
document does not depend on them.

Any change that breaks this document increments `schema_version`.

## 1. Transport

### status.json

| Item | Value |
|---|---|
| Path | `$XDG_STATE_HOME/sony-headphones/status.json`, else `$HOME/.local/state/sony-headphones/status.json` |
| Writer | the daemon only |
| Write method | write a temp file in the same directory, then `rename(2)` over status.json. Readers never see a partial file |
| Encoding | UTF-8 JSON, one object. Pretty-printed or single-line; readers must not care |
| When written | on start, on connect, on disconnect, and after every device reply or push that changes a value |

### Daemon socket

| Item | Value |
|---|---|
| Path | `$XDG_RUNTIME_DIR/sony-headphones.sock`, else `/tmp/run-<uid>/sony-headphones.sock` |
| Framing | one request line (`\n`-terminated), one reply line |
| Reply | `OK`, `ERR <reason>`, or a JSON object (verbs `status` and `probe`) |

### sony-ctl

`sony-ctl [-s <socket>] <verb> [args...]`

| Exit code | Meaning | Output |
|---|---|---|
| `0` | the daemon replied `OK`, or a JSON object | stdout: `OK` or the JSON line |
| `1` | bad arguments, or the daemon replied `ERR ...`, or socket I/O failed | stderr: one line, `Error: ...` or `Error from daemon: ERR ...` |
| `2` | cannot connect to the daemon socket | stderr: `Error: Cannot connect to daemon socket (...)` |

The CLI checks argument syntax before it connects, with the same rules as
the daemon. The CLI socket timeout is 2 s.

## 2. status.json

### 2.1 Rules

1. `null` means "unknown": the daemon has not read the value yet, or the
   headset does not support it (check `supported`). A key that is missing is
   the same as `null`.
2. When `connected` is `false`, the file has only the three keys
   `schema_version`, `connected`, `last_updated`.
3. When `connected` is `true`, every key in section 2.3 is present.
4. Readers ignore keys they do not know. The daemon may add keys within
   schema version 2 (see section 5).
5. Strings that are enum values are lower case ASCII, except `codec`.
6. Bluetooth addresses are strings in the form `AA:BB:CC:DD:EE:FF`
   (upper case hex). Never parse them as numbers.

### 2.2 Top-level keys (always present)

| Key | Type | Meaning |
|---|---|---|
| `schema_version` | int | `2` |
| `connected` | bool | `true` when the RFCOMM link to the headset is open and the protocol handshake answered |
| `last_updated` | int | Unix time in seconds of this write |

### 2.3 Keys present when connected

v1 keys keep their v1 name and meaning. New in v2: they may be `null`
(v1 daemons wrote invented defaults such as `codec: "LDAC"`; v2 daemons must
not).

**Identity**

| Key | Type | Allowed values | v1 | Notes |
|---|---|---|---|---|
| `device_name` | string | any | yes | Name to show. The BlueZ alias of the headset; `"WH-1000XM5"` if BlueZ has none. Never `null` |
| `address` | string \| null | Bluetooth address | new | Address of the headset itself |
| `model_name` | string \| null | any | new | Model name the headset reports, for example `"WH-1000XM5"` |
| `firmware_version` | string \| null | any | new | Firmware version string the headset reports |

**Capabilities**

| Key | Type | Allowed values | v1 | Notes |
|---|---|---|---|---|
| `supported` | object \| null | see 2.4 | new | `null` until the daemon has the headset's support function list |
| `supported_source` | string \| null | `device`, `fallback` | new | `device`: from the headset. `fallback`: the headset did not answer the support request; the daemon uses its built-in XM5 list (2.4). `null` while `supported` is `null` |

**Battery** (feature key `battery`)

| Key | Type | Allowed values | v1 | Notes |
|---|---|---|---|---|
| `battery_level` | int \| null | 0..100 | yes | v1 used `-1` for unknown. v2 writes `null`. Readers treat `-1` and `null` the same |
| `battery_charging` | bool \| null | | yes | `true` while on the cable and charging, and while on the cable and full |
| `charging` | bool \| null | | yes | Alias of `battery_charging`, same value. Deprecated; read `battery_charging` |

**Noise control** (feature key `noise_control`)

| Key | Type | Allowed values | v1 | Notes |
|---|---|---|---|---|
| `noise_mode` | string \| null | `anc`, `ambient`, `off` | yes | v1 also allowed `wind`. A v2 daemon never writes `wind` |
| `ambient_sound_level` | int \| null | 1..20 | yes | Ambient sound level. The headset keeps it in every mode; it applies in `ambient` mode. v1 allowed 0 (= wind) |
| `ambient_level` | int \| null | 1..20 | yes | Alias of `ambient_sound_level`. Deprecated |
| `voice_passthrough` | bool \| null | | yes | Focus on Voice. Applies in `ambient` mode |

**Equalizer** (feature keys `eq_preset`, `eq_custom`)

| Key | Type | Allowed values | v1 | Notes |
|---|---|---|---|---|
| `eq_preset` | string \| null | `off`, `bright`, `excited`, `mellow`, `relaxed`, `vocal`, `treble`, `bass`, `speech`, `custom`, `user1`..`user5`, `unknown` | yes | `unknown` = the headset reports a preset ID this contract does not name. `user1`..`user5` are new |
| `eq_presets` | array of string \| null | values of `eq_preset` except `unknown` | new | Presets the headset accepts, in the headset's order. `null` = list not read; then use the v1 list `off, bright, excited, mellow, relaxed, vocal, treble, bass, speech, custom` |
| `eq_custom_bands` | array of 5 int \| null | each -10..10 | yes | Custom EQ: 400 Hz, 1 kHz, 2.5 kHz, 6.3 kHz, 16 kHz. Last values the headset reported for the custom preset; the daemon keeps them while another preset is active. `null` until reported once |
| `eq_bands` | array of 5 int \| null | each -10..10 | yes | Alias of `eq_custom_bands`. Deprecated |
| `clear_bass` | int \| null | -10..10 | yes | Clear Bass of the custom preset; same rule as `eq_custom_bands` |

**Sound and DSEE**

| Key | Type | Allowed values | v1 | Feature key | Notes |
|---|---|---|---|---|---|
| `dsee_extreme` | bool \| null | | yes | `dsee` | DSEE Extreme ("auto" on the headset = `true`) |
| `dsee` | bool \| null | | yes | `dsee` | Alias of `dsee_extreme`. Deprecated |
| `connection_quality` | string \| null | `sound`, `connection`, `other` | new | `connection_quality` | `sound` = prioritize sound quality (LDAC possible), `connection` = prioritize a stable connection. `other` = a mode this contract does not name |
| `codec` | string \| null | `SBC`, `AAC`, `LDAC`, `APTX`, `APTX_HD`, `LC3`, `OTHER`, `NONE` | yes | `codec` | Codec of the current audio stream. `NONE` = no stream yet. v1 always wrote `"LDAC"` |
| `volume` | int \| null | 0..30 | new | `volume` | Headset volume |

**Speak-to-Chat** (feature key `speak_to_chat`)

| Key | Type | Allowed values | v1 | Notes |
|---|---|---|---|---|
| `speak_to_chat` | bool \| null | | yes | |
| `speak_to_chat_sensitivity` | string \| null | `auto`, `high`, `low` | new | Voice detection sensitivity |
| `speak_to_chat_timeout` | string \| null | `short`, `standard`, `long`, `off` | new | Time until the headset leaves Speak-to-Chat after you stop talking. `off` = it does not leave by itself |

**Voice guidance**

| Key | Type | Allowed values | v1 | Feature key | Notes |
|---|---|---|---|---|---|
| `voice_guidance` | bool \| null | | new | `voice_guidance` | Spoken notifications (battery, mode changes) |
| `voice_guidance_volume` | int \| null | -2..2 | new | `voice_guidance_volume` | |

**Power**

| Key | Type | Allowed values | v1 | Feature key | Notes |
|---|---|---|---|---|---|
| `auto_power_off` | string \| null | `off`, `5min`, `15min`, `30min`, `60min`, `180min`, `when_taken_off` | new | `auto_power_off` | |
| `auto_power_off_options` | array of string \| null | values of `auto_power_off` | new | `auto_power_off` | Values the headset accepts. `when_taken_off` is present only when the headset supports it |

**Buttons**

| Key | Type | Allowed values | v1 | Feature key | Notes |
|---|---|---|---|---|---|
| `nc_amb_button` | string \| null | `nc_ambient_off`, `nc_ambient`, `nc_off`, `ambient_off` | new | `nc_amb_button` | Modes the NC/AMB button cycles through: NC → Ambient → Off, NC → Ambient, NC → Off, Ambient → Off |
| `button_function` | string \| null | `ambient_sound_control`, `voice_recognition`, `google_assistant`, `amazon_alexa`, `quick_access`, `none`, `other` | new | `button_function` | What the NC/AMB button does |
| `button_functions` | array of string \| null | values of `button_function` except `other` | new | `button_function` | Values the headset accepts for the NC/AMB button |

**Wearing**

| Key | Type | Allowed values | v1 | Feature key | Notes |
|---|---|---|---|---|---|
| `ear_detection` | bool \| null | | yes | `ear_detection` | Pause playback when the headset is taken off (and resume when put on) |
| `worn` | bool \| null | | new | `worn` | `true` = the headset is on the head now. `null` also when the sensor reports an error |

**Multipoint**

| Key | Type | Allowed values | v1 | Feature key | Notes |
|---|---|---|---|---|---|
| `multipoint` | bool \| null | | yes | `multipoint` | "Connect to 2 devices simultaneously" |
| `devices` | array of Device \| null | | new | `devices` | Devices paired with the headset, in the headset's order. `[]` = read, none paired |

Device object:

| Key | Type | Notes |
|---|---|---|
| `address` | string | Bluetooth address of the source device (phone, laptop) |
| `name` | string | Name the headset stores for it; may be `""` |
| `connected` | bool | `true` when the device is connected to the headset now |
| `active` | bool | `true` for the device that plays audio now. At most one device has `active: true`; `active` implies `connected` |

### 2.4 `supported`

When not `null`, `supported` is an object with **every** key below, each
`true` or `false`. A `false` key means: hide the control, and the matching
fields are `null`.

| Key | Fields it gates | Verbs it gates | Derived from (see protocol.md) |
|---|---|---|---|
| `battery` | `battery_level`, `battery_charging`, `charging` | none | T1 `20` or `28` |
| `noise_control` | `noise_mode`, `ambient_sound_level`, `ambient_level`, `voice_passthrough` | `noise`, `ambient-level`, `focus-voice` | T1 `6B`, `6D` or `68` |
| `eq_preset` | `eq_preset`, `eq_presets` | `eq <preset>` | T1 `50` |
| `eq_custom` | `eq_custom_bands`, `eq_bands`, `clear_bass` | `eq custom` | T1 `50`, and the EQ capability reports 5 or 6 bands or an EQ value reply carried 6 band steps |
| `dsee` | `dsee_extreme`, `dsee` | `dsee` | T1 `E2` |
| `speak_to_chat` | `speak_to_chat`, `speak_to_chat_sensitivity`, `speak_to_chat_timeout` | `speak-to-chat`, `speak-to-chat-config` | T1 `FC` |
| `voice_guidance` | `voice_guidance` | `voice-guidance` | T2 `40`, `41`, `42`, `44` or `45` |
| `voice_guidance_volume` | `voice_guidance_volume` | `voice-guidance-volume` | T2 `42` or `43` |
| `auto_power_off` | `auto_power_off`, `auto_power_off_options` | `auto-power-off` | T1 `24` or `25` |
| `nc_amb_button` | `nc_amb_button` | `nc-amb-button` | T1 `69` |
| `button_function` | `button_function`, `button_functions` | `button-function` | T1 `F3` or `FE`, and the capability lists an NC/AMB key |
| `ear_detection` | `ear_detection` | `ear-detect` | T1 `F1` |
| `worn` | `worn` | none | table 2 present and (T2 `F0`, T1 `F6` or T1 `F1`) |
| `volume` | `volume` | `volume` | T1 `A1`, `A2`, `A3` or `A4` |
| `connection_quality` | `connection_quality` | `connection-quality` | T1 `E1` or `E3` |
| `codec` | `codec` | none | T1 `12` |
| `multipoint` | `multipoint` | `multipoint` | a general setting slot (T1 `D1`..`D4`) whose capability subject is `MULTIPOINT_SETTING` |
| `devices` | `devices` | none | T2 `30`, `32` or `33` |
| `source_switch` | none (`devices[].active` is gated by `devices`) | `source` | T2 `31`, and `devices` is `true` |

Lifecycle:

1. On connect, the daemon writes `connected: true` with `supported: null`
   and all feature fields `null`.
2. When the support function list arrives (and, for `eq_custom`,
   `button_function` and `multipoint`, the capability replies), the daemon
   writes `supported` with `supported_source: "device"`. Feature fields stay
   `null` until their values arrive.
3. If the headset does not answer the support function request within the
   daemon's retry budget, the daemon writes the **fallback** list with
   `supported_source: "fallback"` and reads values with the fixed XM5
   requests. Fallback list: `true` for `battery`, `noise_control`,
   `eq_preset`, `eq_custom`, `speak_to_chat`, `voice_guidance`,
   `auto_power_off`, `nc_amb_button`, `ear_detection`, `volume`; `false` for
   all others. These are the features confirmed on a real XM5 (protocol.md
   section 5).
4. `supported` does not change while the connection stays up, with one
   exception: `eq_custom`, `button_function` and `multipoint` may change from
   `false` to `true` when a late capability reply arrives.

### 2.5 What the widget must do

| Situation | Widget behavior |
|---|---|
| file missing, empty, not JSON, not an object | show the error state (as today in `Model.parseStatus`) |
| `schema_version` missing | error state, as today |
| `schema_version` greater than the widget knows | see section 5 |
| `connected: false` | show "Disconnected"; no controls |
| `connected: true`, `supported: null` | show `device_name` and battery if known; show every control as disabled, with the text "Reading headset settings…" |
| `supported[key] == false` | hide the control for that key |
| `supported[key] == true`, field `null` | show the control as disabled ("unknown") |
| `supported_source == "fallback"` | show controls as usual; optional hint "Limited: headset did not report its features" |
| `schema_version: 1` file (old daemon) | read it with v1 meaning; treat `supported` as `true` for `battery`, `noise_control`, `eq_preset`, `eq_custom`, `dsee`, `speak_to_chat`, `ear_detection`, `multipoint`, `codec` and `false` for the rest; treat `battery_level: -1` as unknown |

Optimistic updates: the widget may show a requested value at once (as
`Service.qml` does today with its 4 s settle timer). The daemon's next
status.json write with the real value replaces it. `OK` from `sony-ctl`
means the headset accepted the packet, not that the value changed.

### 2.6 Full example

Connected, read from the device. Every key is present.

```json
{
  "schema_version": 2,
  "connected": true,
  "last_updated": 1760438400,
  "device_name": "WH-1000XM5",
  "address": "AC:80:0A:12:34:56",
  "model_name": "WH-1000XM5",
  "firmware_version": "2.3.1",
  "supported_source": "device",
  "supported": {
    "battery": true, "noise_control": true, "eq_preset": true, "eq_custom": true,
    "dsee": true, "speak_to_chat": true, "voice_guidance": true,
    "voice_guidance_volume": false, "auto_power_off": true, "nc_amb_button": true,
    "button_function": true, "ear_detection": true, "worn": true, "volume": true,
    "connection_quality": true, "codec": true, "multipoint": true, "devices": true,
    "source_switch": true
  },
  "battery_level": 80,
  "battery_charging": false,
  "charging": false,
  "noise_mode": "anc",
  "ambient_sound_level": 14,
  "ambient_level": 14,
  "voice_passthrough": false,
  "eq_preset": "custom",
  "eq_presets": ["off", "bright", "excited", "mellow", "relaxed", "vocal", "treble", "bass", "speech", "custom"],
  "eq_custom_bands": [2, 1, -1, 3, 0],
  "eq_bands": [2, 1, -1, 3, 0],
  "clear_bass": 4,
  "dsee_extreme": true,
  "dsee": true,
  "connection_quality": "sound",
  "codec": "LDAC",
  "volume": 12,
  "speak_to_chat": false,
  "speak_to_chat_sensitivity": "auto",
  "speak_to_chat_timeout": "standard",
  "voice_guidance": true,
  "voice_guidance_volume": null,
  "auto_power_off": "when_taken_off",
  "auto_power_off_options": ["off", "5min", "15min", "30min", "60min", "180min", "when_taken_off"],
  "nc_amb_button": "nc_ambient",
  "button_function": "ambient_sound_control",
  "button_functions": ["ambient_sound_control", "google_assistant", "amazon_alexa", "quick_access"],
  "ear_detection": true,
  "worn": true,
  "multipoint": false,
  "devices": [
    {"address": "F0:1D:BC:AA:00:01", "name": "laptop", "connected": true, "active": true}
  ]
}
```

Fixtures that conform to this contract: `tests/fixtures/status_v2_*.json`.

| Fixture | Case |
|---|---|
| `status_v2_full.json` | connected, every feature supported and read |
| `status_v2_minimal.json` | connected, the headset advertises only battery and noise control |
| `status_v2_multipoint.json` | connected, multipoint on, two connected sources, one active |
| `status_v2_initializing.json` | connected, `supported: null` |
| `status_v2_fallback.json` | connected, `supported_source: "fallback"` |
| `status_v2_disconnected.json` | not connected |

## 3. Verbs

### 3.1 Common rules

The daemon checks a setter request in this order and stops at the first
failure:

| Order | Check | Reply |
|---|---|---|
| 1 | verb is known | `ERR unknown command '<verb>'` |
| 2 | argument syntax and range | `ERR usage: <syntax from the table>` |
| 3 | headset connected | `ERR not connected` |
| 4 | `supported` is not `null` | `ERR not ready` |
| 5 | `supported[<feature key>]` is `true` | `ERR unsupported <feature key>` |
| 6 | value is in the headset's option list (only verbs marked "list") | `ERR unsupported <feature key> <value>` |
| 7 | send to the headset; wait for its ACK | `OK`, or `ERR device timeout` when no ACK arrives within 1.8 s |

Verbs and fixed keywords are case-insensitive; the daemon lowercases them.
Bluetooth addresses are case-insensitive and normalized to upper case. The
daemon handles one headset request at a time; other requests wait in a
queue. A request that waits behind others can exceed the 2 s CLI timeout;
the CLI then exits 1 with a socket error.

### 3.2 Read verbs

| Verb | Reply | Notes |
|---|---|---|
| `status` | the status.json object, one line | Same content as the file. Works when disconnected |
| `probe` | one-line JSON object (3.4), or `ERR not connected` | Hardware debugging; the widget does not use it |

### 3.3 Setter verbs

"Feature" is the `supported` key checked in step 5. "List" means step 6
applies, against the named field.

| Verb and syntax | Arguments | Feature | List | Effect | v1 |
|---|---|---|---|---|---|
| `noise anc\|ambient\|off` | mode | `noise_control` | | set the noise mode; keep the stored ambient level and Focus on Voice | yes; `wind` now returns `ERR usage` |
| `ambient-level <n>` | int 1..20 | `noise_control` | | set the ambient level **and** switch to `ambient` mode | yes; v1 range was 0..20, `0` now returns `ERR usage` |
| `focus-voice on\|off` | | `noise_control` | | set Focus on Voice; keep mode and level | new |
| `eq <preset>` | `off`, `bright`, `excited`, `mellow`, `relaxed`, `vocal`, `treble`, `bass`, `speech`, `custom`, `user1`..`user5` | `eq_preset` | `eq_presets` (skipped while `eq_presets` is `null`) | select a preset | yes; `custom` and `user1`..`user5` are new for the preset form |
| `eq custom <b1> <b2> <b3> <b4> <b5> <clear_bass>` | six ints, each -10..10; bands in the order 400 Hz, 1 kHz, 2.5 kHz, 6.3 kHz, 16 kHz | `eq_custom` | | select the custom preset with these values | yes, same argument order |
| `dsee on\|off` | | `dsee` | | DSEE Extreme auto / off | yes |
| `speak-to-chat on\|off` | | `speak_to_chat` | | | yes |
| `speak-to-chat-config <sensitivity> <timeout>` | `auto\|high\|low`, `short\|standard\|long\|off` | `speak_to_chat` | | both values are sent together | new |
| `voice-guidance on\|off` | | `voice_guidance` | | | new |
| `voice-guidance-volume <n>` | int -2..2 | `voice_guidance_volume` | | the headset plays a test beep | new |
| `auto-power-off <value>` | `off`, `5min`, `15min`, `30min`, `60min`, `180min`, `when_taken_off` | `auto_power_off` | `auto_power_off_options` | | new |
| `nc-amb-button <value>` | `nc_ambient_off`, `nc_ambient`, `nc_off`, `ambient_off` | `nc_amb_button` | | | new |
| `button-function <value>` | `ambient_sound_control`, `voice_recognition`, `google_assistant`, `amazon_alexa`, `quick_access`, `none` | `button_function` | `button_functions` | changes only the NC/AMB button | new |
| `ear-detect on\|off` | alias: `ear-detection` | `ear_detection` | | pause when taken off | yes |
| `volume <n>` | int 0..30 | `volume` | | headset volume | new |
| `connection-quality sound\|connection` | | `connection_quality` | | the headset may reconnect after this change | new |
| `multipoint on\|off` | | `multipoint` | | the daemon confirms the headset's alert; the headset then drops the link and reconnects. Expect `connected: false` for a few seconds | yes |
| `source <address>` | Bluetooth address | `source_switch` | must be a `devices[]` entry with `connected: true`, else `ERR unsupported source_switch <address>` | make this device the active audio source | new |

The `ERR usage` text for each verb is `ERR usage: ` followed by the
"Verb and syntax" cell with `\|` written as `|`. Example:
`ERR usage: noise anc|ambient|off`.

v1 daemons used other `ERR` texts for argument errors (for example
`ERR invalid mode 'x'`). The widget only shows `ERR` texts; it must not
parse them.

Test-only verbs (`_set_battery`, `_disconnect`, `_reconnect`) exist in the
daemon for the mock and stress tests. They are not part of this contract.

### 3.4 `probe` reply

One line of JSON. All `"0x.."` values are strings with two upper case hex
digits. Names are the mos9527 enum names (`protocol.md` section 4); an ID
without a known name has the name `"UNKNOWN"`. A key whose data was not
received is `null`.

```json
{
  "probe_version": 1,
  "address": "AC:80:0A:12:34:56",
  "protocol": {"version": 4096, "table1": true, "table2": true},
  "model_name": "WH-1000XM5",
  "firmware_version": "2.3.1",
  "series": "0x30",
  "color": "0x01",
  "unique_id": "...",
  "nc_asm_type": "0x17",
  "support_table1": [
    {"id": "0x20", "name": "BATTERY_LEVEL_INDICATOR", "priority": 1}
  ],
  "support_table2": [
    {"id": "0x32", "name": "PAIRING_DEVICE_MANAGEMENT_WITH_BLUETOOTH_CLASS_OF_DEVICE_CLASSIC_BT", "priority": 1}
  ],
  "general_settings": [
    {"slot": "0xD1", "type": "boolean", "format": "enum_name", "subject": "MULTIPOINT_SETTING", "summary": "MULTIPOINT_SETTING_SUMMARY", "value": false}
  ],
  "eq_capability": {"bands": 6, "steps": 21, "presets": [{"id": "0x10", "name": "Bright"}]},
  "assignable": {
    "type": "0x03",
    "keys": [{"key": "0x04", "type": "0x01", "default": "0x00", "presets": ["0x00", "0x31", "0x32"]}],
    "value": ["0x00"]
  },
  "recent_frames": [
    {"dir": "tx", "type": "0x0C", "seq": 0, "payload": "06 00"},
    {"dir": "rx", "type": "0x0C", "seq": 1, "payload": "07 00 02 20 01 6B 01"}
  ]
}
```

| Key | Content |
|---|---|
| `probe_version` | `1`. Independent of `schema_version`; probe output is for humans and bug reports |
| `protocol` | from CONNECT_RET_PROTOCOL_INFO; `version` as a decimal int |
| `series`, `color`, `unique_id` | from the device info and capability info replies |
| `nc_asm_type` | NC/ASM inquired type the daemon chose, or `null` |
| `support_table1`, `support_table2` | raw support function lists, in the order received |
| `general_settings` | one entry per advertised slot; `type` is `boolean`, `list` or `"0x.."`; `format` is `raw_name`, `enum_name` or `"0x.."`; `value` is `null` for non-boolean slots |
| `eq_capability` | from the EQ capability reply |
| `assignable` | from the assignable capability and value replies |
| `recent_frames` | the last 64 data packets in both directions (ACKs excluded), oldest first; `payload` is space-separated upper case hex |

## 4. Example session

```
$ sony-ctl status
{"schema_version":2,"connected":true,"last_updated":1760438400,"device_name":"WH-1000XM5",...}
$ sony-ctl noise ambient
OK
$ sony-ctl ambient-level 0
Error: 'ambient-level' requires an integer level between 1 and 20
$ sony-ctl dsee on
Error from daemon: ERR unsupported dsee
$ sony-ctl source F0:1D:BC:AA:00:02
OK
```

The first CLI error comes from the CLI's own argument check (exit 1, the
daemon is not contacted). Its text is not part of the contract.

## 5. Version rule

`Model.js` today (`SUPPORTED_SCHEMA = 1`, function `parseStatus`):

- `schema_version` missing or `null` → error "The sony status file carried no
  schema_version", no data.
- `schema_version` not a number, or greater than `SUPPORTED_SCHEMA` →
  `schemaTooNew: true`, `lastError` "sony daemon speaks status schema N,
  this panel reads M", no data and no controls.

Rules for schema version 2:

1. The widget sets `SUPPORTED_SCHEMA = 2` and reads both `1` and `2`
   (section 2.5 says how to read `1`).
2. A `schema_version` greater than 2 keeps today's behavior: `schemaTooNew`,
   the message above, no data, no controls. The widget must not guess at a
   newer file.
3. The daemon always writes the newest version it knows. It never writes 1
   once it implements 2.
4. Within version 2, the daemon may add new keys and new `supported` keys.
   The widget ignores unknown keys; an unknown `supported` key has no
   control, so nothing changes for the user.
5. Removing a key, renaming it, changing its type, adding a value to an
   existing enum, or changing a meaning is a breaking change and needs
   `schema_version: 3`. Exception: `unknown`, `other` and `OTHER` values exist
   so the daemon can report unexpected headset values without a version bump.
6. Deprecated aliases (`charging`, `ambient_level`, `eq_bands`, `dsee`) stay
   in version 2 with the same value as their canonical key. They may be
   removed in version 3.

## 6. Debugging

- `journalctl --user -u sony-headphones -f` shows the daemon log, including
  every received payload in hex.
- `sony-ctl probe` prints the identity, the support function lists and the
  last 64 frames. Attach its output to bug reports about missing features.
