# Backend contract (schema version 3)

The daemon (`sony-headphones-daemon`) owns the RFCOMM links to the headsets.
It manages every connected supported headset at the same time: today the
WH-1000XM5 headphones and the WF-1000XM5 earbuds. The bar widget
(`Service.qml`, `Model.js`, `Panel.qml`) never talks to a headset. The two
sides share exactly two interfaces:

- **status.json**: the daemon writes the state of every managed headset to
  one file; the widget watches the file.
- **sony-ctl**: the widget runs the CLI once per change; the CLI sends one
  line to the daemon socket and prints one reply line. A selector names the
  headset the command is for.

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
| When written | on start, when a headset session opens or closes, when `primary` changes, and after every device reply or push that changes a value |

### Daemon socket

| Item | Value |
|---|---|
| Path | `$XDG_RUNTIME_DIR/sony-headphones.sock`, else `/tmp/run-<uid>/sony-headphones.sock` |
| Framing | one request line (`\n`-terminated), one reply line |
| Request line | `[@<selector> ]<verb> [args...]`. The optional first token `@<selector>` names the headset (section 2.5) |
| Reply | `OK`, `ERR <reason>`, or a JSON object (verbs `status` and `probe`) |

### sony-ctl

`sony-ctl [-s <socket>] [-H|--headset <selector>] <verb> [args...]`

`--headset <selector>` makes the CLI send `@<selector> ` before the verb.
Without it, the daemon uses the primary headset (section 2.4).

| Exit code | Meaning | Output |
|---|---|---|
| `0` | the daemon replied `OK`, or a JSON object | stdout: `OK` or the JSON line |
| `1` | bad arguments, or the daemon replied `ERR ...`, or socket I/O failed | stderr: one line, `Error: ...` or `Error from daemon: ERR ...` |
| `2` | cannot connect to the daemon socket | stderr: `Error: Cannot connect to daemon socket (...)` |

The CLI checks argument syntax before it connects, with the same rules as
the daemon. The CLI socket timeout is 2 s.

## 2. Headset management

### 2.1 Decision

One daemon process manages one **session** per connected supported headset.
There is one socket and one status.json. Each session has its own RFCOMM
link, MDR sequence number, init sequence, state and request queue; sessions
do not share protocol state.

Reasons:

- The widget reads one file and calls one CLI. A second daemon process per
  headset would need a second socket, a second file, and a widget that
  merges them.
- BlueZ discovery (D-Bus signals, the adapter address) runs once.
- The case with two headsets is rare. With one session the daemon behaves
  as before, and the per-session code is the existing single-headset code.

The daemon writes only schema 3. It does not write schema 2 for old
widgets: the daemon and the widget ship together from this repository.

### 2.2 Which headsets get a session

| Mode | Rule |
|---|---|
| Automatic (no `--device`) | every BlueZ device that is paired, **connected** (BlueZ `Connected = true`), and whose name contains `WH-1000XM5` or `WF-1000XM5` (case-insensitive) |
| Allow-list (`--device <MAC>`, may be repeated) | every listed address that is paired and connected, whatever its name. No other device gets a session |

The daemon never starts a Bluetooth connection to a headset. It opens the
RFCOMM link only to a device that BlueZ already reports as connected, and it
waits for the user (or the headset) to connect it. This replaces the
single-headset rule "prefer a connected headset, else connect to a paired
one". A device with no name and the Sony service is used only through
`--device`.

### 2.3 Session lifecycle

1. BlueZ reports a matching device as connected → the daemon opens the
   RFCOMM link and runs the init sequence.
2. When the protocol handshake answers, the headset enters `headsets[]`
   with `supported: null` (initializing). Feature fields stay `null` until
   their values arrive, as in section 3.4.
3. When BlueZ reports the device as disconnected, or the RFCOMM link fails,
   the daemon closes the session and removes the entry from `headsets[]`.
   Requests queued for that headset fail with `ERR not connected`.
4. The daemon never starts a Bluetooth connection. While BlueZ still
   reports the headset as connected, it reopens only the RFCOMM link: up
   to 6 attempts in a row with a short backoff (2 to 10 s), then one
   attempt every 60 s. An attempt fails when the RFCOMM link does not open
   or the protocol handshake does not answer; only an answered handshake
   resets the count. When BlueZ reports the headset as disconnected, the
   daemon stops retrying and waits for the next BlueZ connect. After
   `multipoint` and `connection-quality` the headset drops the link and
   reconnects by itself; that is the same path (a new session, a new
   entry).

A headset is either in `headsets[]` (handshake answered) or absent. There is
no "connecting" entry.

### 2.4 Primary headset

`primary` is the `id` of the headset that the bar icon represents and that
commands without a selector go to. The daemon computes it on every write:

1. If `headsets` is empty, `primary` is `null`.
2. A headset **plays from this computer** when its `devices` list has an
   entry whose `address` is the address of a local Bluetooth adapter (BlueZ
   `Adapter1.Address`) and that entry has `active: true`. When `devices` is
   `null` or unsupported, the headset plays from this computer when `codec`
   is not `null` and not `NONE`.
3. If one or more headsets play from this computer, pick among them; else
   pick among all headsets.
4. Pick the one with the largest `connected_since` (the most recently
   connected). On a tie, pick the first in `headsets` order.

`primary` is a pure function of the file content plus the adapter address,
so it never depends on the widget. The widget's selected tab does not
change `primary`.

### 2.5 Headset selector

A selector is one token without spaces:

| Form | Matches |
|---|---|
| Bluetooth address (`AA:BB:CC:DD:EE:FF`, any case) | the entry whose `id` equals it |
| anything else | every entry whose `model` equals it (case-insensitive), for example `wf-1000xm5` |

Resolution, for every verb except `status`:

| Situation | Result |
|---|---|
| `headsets` is empty | `ERR not connected` |
| no selector | the `primary` headset |
| selector matches one entry | that headset |
| selector matches no entry | `ERR no headset <selector>` |
| model selector matches two or more entries | `ERR ambiguous headset <selector>` (use the address) |

`<selector>` in the reply is the text as received. The widget always sends
the address (`--headset <id>`), never a model, and never relies on
`primary` for a command. `sony-ctl` users may use either form.

## 3. status.json

### 3.1 Rules

1. `null` means "unknown": the daemon has not read the value yet, or the
   headset does not support it (check `supported`). A key that is missing is
   the same as `null`.
2. The top level always has the five keys of section 3.2.
3. Every entry of `headsets` has every key of section 3.3, for every model.
   Fields of features the model does not have are `null` (for example
   `battery_left` on the WH-1000XM5, `battery_level` on the WF-1000XM5).
4. Readers ignore keys they do not know. The daemon may add keys within
   schema version 3 (see section 6).
5. Strings that are enum values are lower case ASCII, except `codec`.
6. Bluetooth addresses are strings in the form `AA:BB:CC:DD:EE:FF`
   (upper case hex). Never parse them as numbers.

### 3.2 Top-level keys (always present)

| Key | Type | Meaning |
|---|---|---|
| `schema_version` | int | `3` |
| `connected` | bool | `true` when `headsets` is not empty |
| `last_updated` | int | Unix time in seconds of this write |
| `primary` | string \| null | `id` of an entry of `headsets` (section 2.4). `null` exactly when `headsets` is empty |
| `headsets` | array of Headset | Connected headsets whose handshake answered. Order: `kind` `headphones` before `earbuds`, then `id` ascending. The order does not depend on connect order, so widget tabs do not move |

No headset connected:

```json
{"schema_version": 3, "connected": false, "last_updated": 1760438400, "primary": null, "headsets": []}
```

The daemon writes this on start and whenever the last session closes.

### 3.3 Headset object

"v2" says how the key relates to the schema 2 top-level key of the same
name: `same` = same name, type and meaning; `new` = new in v3; `changed` =
see the notes.

Removed in v3: the per-headset `connected` (an entry exists only while
connected; see the top-level `connected`), and the deprecated aliases
`charging`, `ambient_level`, `eq_bands`, `dsee`. Read the canonical keys
`battery_charging`, `ambient_sound_level`, `eq_custom_bands`,
`dsee_extreme`.

An option list (`eq_presets`, `button_functions`, `*_options`) is never
`[]`: when no reported value maps to a contract value, the daemon writes
`null`.

**Identity**

| Key | Type | Allowed values | v2 | Notes |
|---|---|---|---|---|
| `id` | string | Bluetooth address | new | Key of the entry. The selector, the widget's tab key, and the value of `primary` |
| `model` | string | `wh-1000xm5`, `wf-1000xm5`, `other` | new | From the MODEL_NAME reply, compared case-insensitively. Before that reply, from the BlueZ name (section 2.2). `other` = any other model (only with `--device`). Never `null` |
| `kind` | string | `headphones`, `earbuds` | new | `wh-1000xm5` → `headphones`; `wf-1000xm5` → `earbuds`; `other` → `earbuds` when `supported.battery_lr` is `true`, else `headphones`. Never `null`. Drives the icon |
| `connected_since` | int | Unix seconds | new | Time the handshake of this session answered. Used for `primary` |
| `device_name` | string | any | changed | Name to show: the BlueZ alias; when BlueZ has none, `"WH-1000XM5"` or `"WF-1000XM5"` by `model`, else `"Sony headset"`. Never `null` |
| `address` | string | Bluetooth address | changed | Same value as `id`. Never `null` in v3 |
| `model_name` | string \| null | any | same | Model name the headset reports, for example `"WF-1000XM5"` |
| `firmware_version` | string \| null | any | same | |

**Capabilities**

| Key | Type | Allowed values | v2 | Notes |
|---|---|---|---|---|
| `supported` | object \| null | see 3.4 | same | `null` until the daemon has the headset's support function list |
| `supported_source` | string \| null | `device`, `fallback` | same | `fallback`: the headset did not answer the support request; the daemon uses the fallback list of its `model` (3.4) |

**Battery, headphones** (feature key `battery`)

| Key | Type | Allowed values | v2 | Notes |
|---|---|---|---|---|
| `battery_level` | int \| null | 0..100 | same | |
| `battery_charging` | bool \| null | | same | `true` while on the cable and charging, and while on the cable and full |

**Battery, earbuds** (feature keys `battery_lr`, `battery_case`)

All six are new. A charging flag is `true` for "charging" and for "charged,
still in the case or on the cable", `false` for "not charging", and `null`
for "unknown" or not read.

An earbud that reports level 0 with "not charging" is switched off or out
of range, not empty (protocol.md 9.11): the daemon writes its
`battery_left` / `battery_right` as `null` and keeps its charging flag
(`false`). The bar battery then shows the other earbud. A level 0 with any
other charging status, and a case level 0, are written as 0.

| Key | Type | Allowed values | Feature key | Notes |
|---|---|---|---|---|
| `battery_left` | int \| null | 0..100 | `battery_lr` | Left earbud, as reported |
| `battery_left_charging` | bool \| null | | `battery_lr` | |
| `battery_right` | int \| null | 0..100 | `battery_lr` | Right earbud, as reported |
| `battery_right_charging` | bool \| null | | `battery_lr` | |
| `battery_case` | int \| null | 0..100 | `battery_case` | Charging case |
| `battery_case_charging` | bool \| null | | `battery_case` | `true` while the case is on the cable or a charging pad |

**Bar battery** (derived by the widget, not written by the daemon):

| `kind` | Bar battery value | Bar charging |
|---|---|---|
| `headphones` | `battery_level` | `battery_charging == true` |
| `earbuds` | the smaller of `battery_left` and `battery_right`, ignoring `null`; `null` when both are `null`. The case is not included | `true` when at least one of `battery_left_charging`, `battery_right_charging` is not `null` and every non-`null` one is `true` |

**Noise control** (feature key `noise_control`)

| Key | Type | Allowed values | v2 | Notes |
|---|---|---|---|---|
| `noise_mode` | string \| null | `anc`, `ambient`, `off` | same | |
| `ambient_sound_level` | int \| null | 1..20 | same | Kept in every mode; applies in `ambient` mode |
| `voice_passthrough` | bool \| null | | same | Focus on Voice. Applies in `ambient` mode |

**Equalizer** (feature keys `eq_preset`, `eq_custom`)

| Key | Type | Allowed values | v2 | Notes |
|---|---|---|---|---|
| `eq_preset` | string \| null | `off`, `bright`, `excited`, `mellow`, `relaxed`, `vocal`, `treble`, `bass`, `speech`, `custom`, `user1`..`user5`, `unknown` | same | `unknown` = the headset reports a preset ID this contract does not name |
| `eq_presets` | array of string \| null | values of `eq_preset` except `unknown` | same | Presets the headset accepts, in the headset's order. `null` = list not read; then use `off, bright, excited, mellow, relaxed, vocal, treble, bass, speech, custom` |
| `eq_custom_bands` | array of 5 int \| null | each -10..10 | same | Band levels at 400 Hz, 1 kHz, 2.5 kHz, 6.3 kHz, 16 kHz of the **active** preset, as the headset last reported them. For `custom` these are the custom values; for any other preset they are that preset's curve. `null` until reported once |
| `clear_bass` | int \| null | -10..10 | same | Clear Bass of the active preset; same rule as `eq_custom_bands` |

**Sound and DSEE**

| Key | Type | Allowed values | v2 | Feature key | Notes |
|---|---|---|---|---|---|
| `dsee_extreme` | bool \| null | | same | `dsee` | DSEE Extreme ("auto" on the headset = `true`) |
| `connection_quality` | string \| null | `sound`, `connection`, `other` | same | `connection_quality` | `sound` = prioritize sound quality, `connection` = prioritize a stable connection. Unsupported on the WF-1000XM5 (protocol.md 9.5) |
| `codec` | string \| null | `SBC`, `AAC`, `LDAC`, `APTX`, `APTX_HD`, `LC3`, `OTHER`, `NONE` | same | `codec` | Codec of the current audio stream. `NONE` = no stream |
| `volume` | int \| null | 0..30 | same | `volume` | Headset volume |

**Speak-to-Chat** (feature key `speak_to_chat`)

| Key | Type | Allowed values | v2 | Notes |
|---|---|---|---|---|
| `speak_to_chat` | bool \| null | | same | |
| `speak_to_chat_sensitivity` | string \| null | `auto`, `high`, `low` | same | |
| `speak_to_chat_timeout` | string \| null | `short`, `standard`, `long`, `off` | same | |

**Voice guidance**

| Key | Type | Allowed values | v2 | Feature key | Notes |
|---|---|---|---|---|---|
| `voice_guidance` | bool \| null | | same | `voice_guidance` | |
| `voice_guidance_volume` | int \| null | -2..2 | same | `voice_guidance_volume` | Supported on the WF-1000XM5, not on the WH-1000XM5 |

**Power**

| Key | Type | Allowed values | v2 | Feature key | Notes |
|---|---|---|---|---|---|
| `auto_power_off` | string \| null | `off`, `5min`, `15min`, `30min`, `60min`, `180min`, `when_taken_off` | same | `auto_power_off` | |
| `auto_power_off_options` | array of string \| null | values of `auto_power_off` | same | `auto_power_off` | |

**Buttons (headphones)**

| Key | Type | Allowed values | v2 | Feature key | Notes |
|---|---|---|---|---|---|
| `nc_amb_button` | string \| null | `nc_ambient_off`, `nc_ambient`, `nc_off`, `ambient_off` | same | `nc_amb_button` | |
| `button_function` | string \| null | `ambient_sound_control`, `voice_recognition`, `google_assistant`, `amazon_alexa`, `quick_access`, `none`, `other` | same | `button_function` | What the NC/AMB button does |
| `button_functions` | array of string \| null | values of `button_function` except `other` | same | `button_function` | |

**Touch sensor function (earbuds)** (feature key `touch_function`, new)

The preset that decides what taps on one earbud do (protocol.md 9.3).

| Key | Type | Allowed values | Notes |
|---|---|---|---|
| `touch_function_left` | string \| null | `ambient_sound_control`, `playback_control`, `volume_control`, `voice_recognition`, `google_assistant`, `amazon_alexa`, `quick_access`, `none`, `other` | Left earbud. `ambient_sound_control`: tap cycles the noise modes, double / triple tap starts Quick Access, hold = Quick Attention. `playback_control`: play / pause, next, previous. `volume_control`: volume up / down. `none`: taps do nothing. `other` = a preset this contract does not name |
| `touch_function_right` | string \| null | same | Right earbud |
| `touch_function_left_options` | array of string \| null | values of `touch_function_left` except `other` | Presets the left earbud accepts, in capability order. The WF-1000XM5 fw 6.1.0 lists `ambient_sound_control`, `playback_control`, `none` |
| `touch_function_right_options` | array of string \| null | same | Right earbud |

**Wearing**

| Key | Type | Allowed values | v2 | Feature key | Notes |
|---|---|---|---|---|---|
| `ear_detection` | bool \| null | | same | `ear_detection` | Pause playback when the headset is taken off (and resume when put on) |
| `worn` | bool \| null | | changed | `worn` | Headphones: `true` = on the head. Earbuds: `true` = at least one earbud is in an ear. `null` also when the sensor reports an error |
| `worn_left` | bool \| null | | new | `worn` | Earbuds only: left earbud in the ear. Always `null` for headphones |
| `worn_right` | bool \| null | | new | `worn` | Earbuds only: right earbud in the ear. Always `null` for headphones |

**Multipoint**

| Key | Type | Allowed values | v2 | Feature key | Notes |
|---|---|---|---|---|---|
| `multipoint` | bool \| null | | same | `multipoint` | "Connect to 2 devices simultaneously" |
| `devices` | array of Device \| null | | same | `devices` | Devices paired with the headset, in the headset's order. `[]` = read, none paired |

Device object (unchanged):

| Key | Type | Notes |
|---|---|---|
| `address` | string | Bluetooth address of the source device (phone, laptop) |
| `name` | string | Name the headset stores for it; may be `""` |
| `connected` | bool | `true` when the device is connected to the headset now |
| `active` | bool | `true` for the device that plays audio now. At most one device has `active: true`; `active` implies `connected` |

**Touch panel, sidetone, Quick Access**

| Key | Type | Allowed values | v2 | Feature key | Notes |
|---|---|---|---|---|---|
| `touch_panel` | bool \| null | | same | `touch_panel` | Headphones: touch sensor control panel on the ear cup. `false` = touch gestures are off |
| `sidetone` | bool \| null | | new | `sidetone` | Hear your own voice in the headset during a call ("Capture voice during a phone call"). Earbuds today |
| `quick_access_double_tap` | string \| null | `off`, `spotify`, `other` | same | `quick_access` | |
| `quick_access_triple_tap` | string \| null | `off`, `spotify`, `other` | same | `quick_access` | |
| `quick_access_double_tap_options` | array of string \| null | values of `quick_access_double_tap` except `other` | same | `quick_access` | |
| `quick_access_triple_tap_options` | array of string \| null | same | same | `quick_access` | |

### 3.4 `supported`

When not `null`, `supported` is an object with **every** key below, each
`true` or `false`. A `false` key means: hide the control, and the matching
fields are `null`.

| Key | Fields it gates | Verbs it gates | Derived from (see protocol.md) |
|---|---|---|---|
| `battery` | `battery_level`, `battery_charging` | none | T1 `20` or `28` |
| `battery_lr` | `battery_left`, `battery_left_charging`, `battery_right`, `battery_right_charging` | none | T1 `29` or `21` (9.2) |
| `battery_case` | `battery_case`, `battery_case_charging` | none | T1 `2A` or `22` (9.2) |
| `noise_control` | `noise_mode`, `ambient_sound_level`, `voice_passthrough` | `noise`, `ambient-level`, `focus-voice` | T1 `6B`, `6D` or `68` |
| `eq_preset` | `eq_preset`, `eq_presets` | `eq <preset>` | T1 `50` |
| `eq_custom` | `eq_custom_bands`, `clear_bass` | `eq custom` | T1 `50`, and the EQ capability reports 5 or 6 bands or an EQ value reply carried 6 band steps |
| `dsee` | `dsee_extreme` | `dsee` | T1 `E2` |
| `speak_to_chat` | `speak_to_chat`, `speak_to_chat_sensitivity`, `speak_to_chat_timeout` | `speak-to-chat`, `speak-to-chat-config` | T1 `FC` |
| `voice_guidance` | `voice_guidance` | `voice-guidance` | T2 `40`, `41`, `42`, `44` or `45` |
| `voice_guidance_volume` | `voice_guidance_volume` | `voice-guidance-volume` | T2 `42` or `43` |
| `auto_power_off` | `auto_power_off`, `auto_power_off_options` | `auto-power-off` | T1 `24` or `25` |
| `nc_amb_button` | `nc_amb_button` | `nc-amb-button` | T1 `69` |
| `button_function` | `button_function`, `button_functions` | `button-function` | T1 `F3` or `FE`, and the capability lists an NC/AMB key (`04` or `05`) |
| `touch_function` | `touch_function_left`, `touch_function_right`, `touch_function_left_options`, `touch_function_right_options` | `touch-function` | T1 `F3` or `FE`, and the capability lists key `00` (left) and key `01` (right) (9.3) |
| `ear_detection` | `ear_detection` | `ear-detect` | T1 `F1` |
| `worn` | `worn`, `worn_left`, `worn_right` | none | table 2 present and (T2 `F0` or T1 `F6`). T1 `F1` alone does not count |
| `volume` | `volume` | `volume` | T1 `A1`, `A2`, `A3` or `A4` |
| `connection_quality` | `connection_quality` | `connection-quality` | T1 `E1` or `E3` |
| `codec` | `codec` | none | T1 `12` |
| `multipoint` | `multipoint` | `multipoint` | a general setting slot (T1 `D1`..`D4`) whose capability subject is `MULTIPOINT_SETTING` |
| `devices` | `devices` | none | T2 `30`, `32` or `33` |
| `source_switch` | none (`devices[].active` is gated by `devices`) | `source` | T2 `31`, and `devices` is `true` |
| `touch_panel` | `touch_panel` | `touch-panel` | a general setting slot whose capability subject is `TOUCH_PANEL_SETTING` |
| `sidetone` | `sidetone` | `sidetone` | a general setting slot whose capability subject is `SIDETONE_SETTING` (9.7) |
| `quick_access` | `quick_access_double_tap`, `quick_access_triple_tap`, `quick_access_double_tap_options`, `quick_access_triple_tap_options` | `quick-access` | T1 `FD`, **and** at least one action in the Quick Access capability lists one or more functions. The WF-1000XM5 lists none, so it is `false` there (9.6) |

Lifecycle, per headset:

1. When the handshake answers, the entry appears with `supported: null`
   and all feature fields `null`.
2. When the support function list arrives (and, for `eq_custom`,
   `button_function`, `touch_function`, `multipoint`, `touch_panel`,
   `sidetone` and `quick_access`, the capability replies), the daemon writes
   `supported` with `supported_source: "device"`. Feature fields stay
   `null` until their values arrive.
3. If the headset does not answer the support function request within the
   daemon's retry budget, the daemon writes the **fallback** list of the
   entry's `model` with `supported_source: "fallback"` and reads values
   with fixed requests:

   | `model` | `true` in the fallback list (all other keys `false`) |
   |---|---|
   | `wh-1000xm5` | `battery`, `noise_control`, `eq_preset`, `eq_custom`, `speak_to_chat`, `voice_guidance`, `auto_power_off`, `nc_amb_button`, `ear_detection`, `volume` |
   | `wf-1000xm5` | `battery_lr`, `battery_case`, `noise_control`, `eq_preset`, `eq_custom`, `speak_to_chat`, `voice_guidance`, `voice_guidance_volume`, `auto_power_off`, `ear_detection`, `volume` |
   | `other` | none |

   These are the features both available captures confirm for that model
   (protocol.md 3.4, 9.1, 9.2).
4. `supported` does not change while the session stays up, with one
   exception: the keys in step 2 that need a capability reply may change
   from `false` to `true` when a late capability reply arrives.

### 3.5 What the widget must do

| Situation | Widget behavior |
|---|---|
| file missing, empty, not JSON, not an object | error state (as today in `Model.parseStatus`) |
| `schema_version` missing | error state, as today |
| `schema_version` greater than the widget knows | see section 6 |
| `schema_version` 1 or 2 | read it as a v3 file with at most one entry (section 6, rule 2) |
| `connected: false` / `headsets: []` | show "Disconnected"; no controls |
| `primary` not `null` but not the `id` of any entry | treat as the first entry (defensive; the daemon never writes this) |
| one entry | the single-headset panel, unchanged from schema 2 apart from the earbuds fields |
| two or more entries | the single-headset panel plus the headset switcher (ui-spec.md) |
| entry with `supported: null` | show its `device_name` and battery if known; every control of that entry disabled, text "Reading headset settings…" |
| `supported[key] == false` | hide the control for that key |
| `supported[key] == true`, field `null` | show the control as disabled ("unknown") |
| `supported_source == "fallback"` | controls as usual; optional hint "Limited: headset did not report its features" |
| `model` or `kind` value the widget does not know | read `model` as `other`, `kind` as `headphones` |

Optimistic updates: the widget may show a requested value at once. The
daemon's next status.json write with the real value replaces it. `OK` from
`sony-ctl` means the headset accepted the packet, not that the value
changed. Optimistic values and queued commands belong to one headset `id`.

### 3.6 Examples

Both headsets connected. The WF-1000XM5 plays from this computer
(`F0:1D:BC:AA:00:01` is the local adapter); the WH-1000XM5 is connected to
this computer too, but plays from the phone. So the WF-1000XM5 is `primary`
although the WH-1000XM5 connected later. The full objects are in the
fixtures.

```json
{
  "schema_version": 3,
  "connected": true,
  "last_updated": 1760438460,
  "primary": "AC:80:0A:65:43:21",
  "headsets": [
    {
      "id": "AC:80:0A:12:34:56",
      "model": "wh-1000xm5",
      "kind": "headphones",
      "connected_since": 1760438430,
      "device_name": "WH-1000XM5",
      "address": "AC:80:0A:12:34:56",
      "battery_level": 80,
      "battery_charging": false,
      "battery_left": null,
      "battery_right": null,
      "battery_case": null,
      "worn": true, "worn_left": null, "worn_right": null,
      "touch_function_left": null, "touch_function_right": null,
      "devices": [
        {"address": "F0:1D:BC:AA:00:01", "name": "laptop", "connected": true, "active": false},
        {"address": "5C:E9:1E:BB:00:02", "name": "phone", "connected": true, "active": true}
      ],
      "...": "every other key of 3.3"
    },
    {
      "id": "AC:80:0A:65:43:21",
      "model": "wf-1000xm5",
      "kind": "earbuds",
      "connected_since": 1760438400,
      "device_name": "WF-1000XM5",
      "address": "AC:80:0A:65:43:21",
      "battery_level": null,
      "battery_charging": null,
      "battery_left": 70, "battery_left_charging": false,
      "battery_right": 65, "battery_right_charging": false,
      "battery_case": 53, "battery_case_charging": false,
      "worn": true, "worn_left": true, "worn_right": true,
      "touch_function_left": "ambient_sound_control",
      "touch_function_right": "playback_control",
      "touch_function_left_options": ["ambient_sound_control", "playback_control", "none"],
      "touch_function_right_options": ["ambient_sound_control", "playback_control", "none"],
      "sidetone": true,
      "devices": [
        {"address": "F0:1D:BC:AA:00:01", "name": "laptop", "connected": true, "active": true}
      ],
      "...": "every other key of 3.3"
    }
  ]
}
```

The `"..."` keys stand for omitted content; they are not part of the
schema. Fixtures that conform to this contract, every key present:

| Fixture | Case |
|---|---|
| `status_v3_wh.json` | one WH-1000XM5, `supported` and values from the fw 2.5.1 capture |
| `status_v3_wf.json` | one WF-1000XM5, `supported` and values from the fw 6.1.0 capture; the earbud levels are invented, the case level is from the mos9527 capture |
| `status_v3_both.json` | both connected; the WF-1000XM5 is `primary` because it plays from this computer |
| `status_v3_initializing.json` | one WF-1000XM5 with `supported: null` |
| `status_v3_disconnected.json` | no headset |

The schema 2 fixtures (`status_v2_*.json`) stay; the widget still reads
schema 2 (section 6).

## 4. Verbs

### 4.1 Common rules

The daemon checks a setter request in this order and stops at the first
failure:

| Order | Check | Reply |
|---|---|---|
| 1 | selector token, if present, is `@` plus at least one character | `ERR usage: @<address\|model> <verb> [args]` |
| 2 | verb is known | `ERR unknown command '<verb>'` |
| 3 | argument syntax and range | `ERR usage: <syntax from the table>` |
| 4 | a headset is resolved (2.5) | `ERR not connected`, `ERR no headset <selector>`, `ERR ambiguous headset <selector>` |
| 5 | that headset's `supported` is not `null` | `ERR not ready` |
| 6 | `supported[<feature key>]` is `true` | `ERR unsupported <feature key>` |
| 7 | value is in the headset's option list (only verbs marked "list") | `ERR unsupported <feature key> <value>` (verb-specific forms below) |
| 8 | send to the headset; wait for its ACK | `OK`, or `ERR device timeout` when no ACK arrives within 1.8 s |

Verbs and fixed keywords are case-insensitive; the daemon lowercases them.
Bluetooth addresses are case-insensitive and normalized to upper case. Each
session handles one request at a time; other requests for the same headset
wait in its queue. Requests for different headsets do not wait for each
other. A request that waits behind others can exceed the 2 s CLI timeout;
the CLI then exits 1 with a socket error.

### 4.2 Read verbs

| Verb | Reply | Notes |
|---|---|---|
| `status` | the status.json object, one line | Same content as the file, all headsets. Works when disconnected. A selector is not allowed: `@x status` replies `ERR usage: status`, and `sony-ctl --headset x status` is rejected by the CLI |
| `probe` | one-line JSON object (4.4) for the resolved headset, or an error of step 4 | Hardware debugging; the widget does not use it |

### 4.3 Setter verbs

Every setter acts on the resolved headset only. "Feature" is the
`supported` key checked in step 6. "List" means step 7 applies, against the
named field.

| Verb and syntax | Arguments | Feature | List | Effect | v2 |
|---|---|---|---|---|---|
| `noise anc\|ambient\|off` | mode | `noise_control` | | set the noise mode; keep the stored ambient level and Focus on Voice | same |
| `ambient-level <n>` | int 1..20 | `noise_control` | | set the ambient level **and** switch to `ambient` mode | same |
| `focus-voice on\|off` | | `noise_control` | | set Focus on Voice; keep mode and level | same |
| `eq <preset>` | `off`, `bright`, `excited`, `mellow`, `relaxed`, `vocal`, `treble`, `bass`, `speech`, `custom`, `user1`..`user5` | `eq_preset` | `eq_presets` (skipped while `null`) | select a preset | same |
| `eq custom <b1> <b2> <b3> <b4> <b5> <clear_bass>` | six ints, each -10..10; bands in the order 400 Hz, 1 kHz, 2.5 kHz, 6.3 kHz, 16 kHz | `eq_custom` | | select the custom preset with these values | same |
| `dsee on\|off` | | `dsee` | | DSEE Extreme auto / off | same |
| `speak-to-chat on\|off` | | `speak_to_chat` | | | same |
| `speak-to-chat-config <sensitivity> <timeout>` | `auto\|high\|low`, `short\|standard\|long\|off` | `speak_to_chat` | | both values are sent together | same |
| `voice-guidance on\|off` | | `voice_guidance` | | | same |
| `voice-guidance-volume <n>` | int -2..2 | `voice_guidance_volume` | | the headset plays a test beep | same |
| `auto-power-off <value>` | `off`, `5min`, `15min`, `30min`, `60min`, `180min`, `when_taken_off` | `auto_power_off` | `auto_power_off_options` | | same |
| `nc-amb-button <value>` | `nc_ambient_off`, `nc_ambient`, `nc_off`, `ambient_off` | `nc_amb_button` | | | same |
| `button-function <value>` | `ambient_sound_control`, `voice_recognition`, `google_assistant`, `amazon_alexa`, `quick_access`, `none` | `button_function` | `button_functions` | changes only the NC/AMB key; the other keys keep their preset | same |
| `touch-function left\|right <value>` | side, then `ambient_sound_control`, `playback_control`, `volume_control`, `voice_recognition`, `google_assistant`, `amazon_alexa`, `quick_access`, `none` | `touch_function` | `touch_function_left_options` or `touch_function_right_options` for the named side (skipped while `null`); the error is `ERR unsupported touch_function <side> <value>` | assign the preset to one earbud; the other earbud (and any other key) keeps its preset | new |
| `ear-detect on\|off` | alias: `ear-detection` | `ear_detection` | | pause when taken off | same |
| `volume <n>` | int 0..30 | `volume` | | headset volume | same |
| `connection-quality sound\|connection` | | `connection_quality` | | the headset may reconnect after this change | same |
| `multipoint on\|off` | | `multipoint` | | the daemon confirms the headset's alert; the headset then drops the link and reconnects. Expect the entry to disappear for a few seconds | same |
| `source <address>` | Bluetooth address | `source_switch` | must be a `devices[]` entry with `connected: true`, else `ERR unsupported source_switch <address>` | make this device the active audio source | same |
| `touch-panel on\|off` | | `touch_panel` | | turn the touch sensor control panel on or off | same |
| `sidetone on\|off` | | `sidetone` | | hear your own voice during calls | new |
| `quick-access double\|triple <value>` | press, then `off` or `spotify` | `quick_access` | `quick_access_double_tap_options` or `quick_access_triple_tap_options` (skipped while `null`); the error is `ERR unsupported quick_access <press> <value>` | assign the function to a double or triple press; the other press keeps its function | same |

The `ERR usage` text for each verb is `ERR usage: ` followed by the
"Verb and syntax" cell with `\|` written as `|`. Example:
`ERR usage: touch-function left|right <value>`.

The widget only shows `ERR` texts; it must not parse them.

Test-only verbs (`_set_battery`, `_disconnect`, `_reconnect`) exist in the
daemon for the mock and stress tests. They act on the resolved headset. They
are not part of this contract.

### 4.4 `probe` reply

One line of JSON for one headset, `probe_version: 1`, same keys as schema 2
(`address`, `protocol`, `model_name`, `firmware_version`, `series`,
`color`, `unique_id`, `nc_asm_type`, `support_table1`, `support_table2`,
`general_settings`, `eq_capability`, `assignable`, `recent_frames`). New
keys within `probe_version` 1:

| Key | Content |
|---|---|
| `id`, `model`, `kind` | as in the headset object |
| `assignable.keys[].presets` | preset IDs per key, parsed with the corrected layout (protocol.md 5.8, 9.3) |
| `connection_mode` | `{"capability": "<hex>", "status": "<hex>"}` with the raw `E1 05` and `E3 05` replies (protocol.md 9.5), `null` values when not advertised or not answered |

All `"0x.."` values are strings with two upper case hex digits; `<hex>` is
space-separated upper case hex. A key whose data was not received is
`null`. `recent_frames` holds the last 64 data packets of this headset's
session (ACKs excluded), oldest first.

```json
{
  "probe_version": 1,
  "id": "AC:80:0A:65:43:21",
  "model": "wf-1000xm5",
  "kind": "earbuds",
  "address": "AC:80:0A:65:43:21",
  "protocol": {"version": 50343960, "table1": true, "table2": true},
  "model_name": "WF-1000XM5",
  "firmware_version": "6.1.0",
  "series": "0x30",
  "color": "0x03",
  "unique_id": "AC:80:0A:65:43:21",
  "nc_asm_type": "0x17",
  "support_table1": [{"id": "0x29", "name": "LR_BATTERY_LEVEL_WITH_THRESHOLD", "priority": 255}],
  "support_table2": [{"id": "0x42", "name": "VOICE_GUIDANCE_SETTING_MTK_TRANSFER_WITHOUT_DISCONNECTION_SUPPORT_LANGUAGE_SWITCH_AND_VOLUME_ADJUSTMENT", "priority": 61}],
  "general_settings": [
    {"slot": "0xD1", "type": "boolean", "format": "enum_name", "subject": "SIDETONE_SETTING", "summary": "SIDETONE_SETTING_SUMMARY", "value": true}
  ],
  "eq_capability": {"bands": 6, "steps": 21, "presets": [{"id": "0x00", "name": ""}]},
  "assignable": {
    "type": "0x03",
    "keys": [
      {"key": "0x00", "type": "0x00", "default": "0x35", "presets": ["0x35", "0x20", "0xFF"]},
      {"key": "0x01", "type": "0x00", "default": "0x20", "presets": ["0x35", "0x20", "0xFF"]}
    ],
    "value": ["0x20", "0x20"]
  },
  "connection_mode": {"capability": null, "status": null},
  "recent_frames": [
    {"dir": "tx", "type": "0x0C", "seq": 0, "payload": "22 09"}
  ]
}
```

## 5. Example session

```
$ sony-ctl status
{"schema_version":3,"connected":true,"last_updated":1760438460,"primary":"AC:80:0A:65:43:21","headsets":[...]}
$ sony-ctl noise ambient
OK                                   (goes to the primary headset)
$ sony-ctl --headset wh-1000xm5 noise anc
OK
$ sony-ctl --headset AC:80:0A:65:43:21 touch-function left playback_control
OK
$ sony-ctl --headset wh-1000xm5 touch-function left none
Error from daemon: ERR unsupported touch_function
$ sony-ctl --headset AA:BB:CC:DD:EE:FF volume 10
Error from daemon: ERR no headset AA:BB:CC:DD:EE:FF
$ sony-ctl --headset x status
Error: 'status' does not take --headset
```

The last CLI error comes from the CLI's own argument check (exit 1, the
daemon is not contacted). Its text is not part of the contract.

## 6. Version rule

Rules for schema version 3:

1. The widget sets `SUPPORTED_SCHEMA = 3` and reads `1`, `2` and `3`.
2. A schema 1 or 2 file (an old daemon still running after an update) is
   read as a v3 file: `connected: false` → `headsets: []`, `primary: null`.
   `connected: true` → one entry built from the top-level keys, with
   `id` = `address` (or `"unknown"` when `address` is `null`), `model` =
   `"wh-1000xm5"`, `kind` = `"headphones"`, `connected_since` =
   `last_updated`, and every key new in v3 `null` (their `supported` keys
   `false`). `primary` = that `id`. Commands to such a file are built
   without `--headset` (an old CLI does not know the option). Old-file
   reading rules, unchanged from the schema 2 contract:
   - A canonical key that is missing is read from its alias (`charging`,
     `ambient_level`, `eq_bands`, `dsee`).
   - `battery_level: -1` means unknown.
   - Schema 1 has no `supported`: read it as `true` for `battery`,
     `noise_control`, `eq_preset`, `eq_custom`, `dsee`, `speak_to_chat`,
     `ear_detection`, `multipoint`, `codec`, and `false` for the rest.
   - A schema 2 `supported` object without `touch_panel` or `quick_access`
     reads those keys as `false`.
3. A `schema_version` greater than 3, or not an integer >= 1 (a string of
   digits counts as its number), keeps today's behavior: `schemaTooNew`,
   the message "sony daemon speaks status schema N, this panel reads 3",
   no data, no controls. The widget must not guess at a newer file.
4. The daemon always writes the newest version it knows. It never writes 1
   or 2 once it implements 3.
5. Within version 3, the daemon may add new keys (top level, headset object)
   and new `supported` keys. The widget ignores unknown keys; a `supported`
   key that is missing reads as `false`, and its fields read as `null`.
6. Within version 3, the daemon may add values to `model` (a new supported
   model). The widget reads an unknown `model` as `other` and an unknown
   `kind` as `headphones`.
7. Removing a key, renaming it, changing its type, adding a value to any
   other enum, or changing a meaning is a breaking change and needs
   `schema_version: 4`. Exception: `unknown`, `other` and `OTHER` values
   exist so the daemon can report unexpected headset values without a
   version bump.

## 7. Debugging

- `journalctl --user -u sony-headphones -f` shows the daemon log, including
  every received payload in hex. Log lines name the headset address.
- `sony-ctl [--headset <id>] probe` prints the identity, the support
  function lists and the last 64 frames of one headset. Attach its output to
  bug reports about missing features.

Known limitation: the daemon learns BlueZ connection changes by polling,
not by D-Bus signals. Every 2 s it runs one blocking `GetManagedObjects`
call (2 s timeout) on the system bus, so a headset can take up to about
2 s to appear in or leave `headsets[]`.

## 8. Daemon changes for schema 3 (summary for implementers)

| Area | Change |
|---|---|
| Sessions | one session object per headset (link, seq, queue, state, init); a session manager keyed by address; `primary` computed on every write (2.4) |
| Selection | section 2.2: names `WH-1000XM5` and `WF-1000XM5`, connected only, repeatable `--device`; never initiate a Bluetooth connection |
| BlueZ | read the local adapter address(es) for 2.4; react to `Connected` changes per device |
| Requests | parse the optional `@<selector>` token; per-session queue; new verbs `touch-function`, `sidetone`; `status` rejects a selector |
| Protocol | L/R and case battery (protocol.md 9.2), also in the periodic sync; assignable capability with the corrected layout and per-side keys (5.8, 9.3); worn per side (9.4); case-insensitive wear log match (9.4); sidetone slot (9.7); Quick Access gate (9.6); raw connection mode bytes for `probe` (9.5) |
| status.json | schema 3 layout; aliases removed; `model`, `kind`, `connected_since`; fallback list per model |
| Mock | `--mock-profile` gains `wf` (one fake WF-1000XM5) and `both` (a fake WH-1000XM5 and a fake WF-1000XM5), so the end-to-end tests cover two sessions |
| CLI | `-H` / `--headset <selector>`; rejected with `status` |
