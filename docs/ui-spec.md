# UI specification: Sony WH-1000XM5 bar widget (status schema 2)

Binding inputs: [`backend-contract.md`](backend-contract.md) (status.json
schema 2, `supported`, every `sony-ctl` verb). This document says how the
widget shows that data. It does not change the contract.

Files: `Panel.qml` (bar button, popup, cursor model, reusable inline
components), `Service.qml` (status file watcher, optimistic values, command
queue), `Model.js` (all logic: parsing, gating, labels, command building,
pending values, queue, cursor movement), `SonyIcon.qml` (headphone glyph).
`Panel.qml` keeps its helper components inline so the deploy list in `setup`
stays unchanged.

## 1. Bar button

| Item | Content |
|---|---|
| Icon | `SonyIcon`: dim when disconnected or in an error state, green while `battery_charging`, urgent below 20 % |
| Text | `battery_level` as `80%`; `—` when unknown or disconnected; hidden when `supported.battery` is `false` |
| Charging hint | a bolt glyph after the percentage while `battery_charging` is `true` |
| Tooltip | line 1 `device_name`; line 2 noise mode name and codec (`Noise Cancelling · LDAC`); line 3 `Battery 80% (charging)`. Disconnected: `Sony headphones: disconnected`. Initializing: `Reading headset settings…`. Error states: the error text |
| Left click | open or close the popup |
| Right click | cycle the noise mode ANC → Ambient → Off (`noise`). Ignored unless the noise control is enabled (section 4) |

## 2. Popup layout

Width `Style.space(380)`. Height fits the content, capped at
`Style.space(640)` and at the screen; the content scrolls in a `Flickable`.
Sections use the existing style: `PanelSectionHeader` titles,
`PanelSeparator` between sections, equal-width bordered `Button` chips,
`Toggle` rows, `PanelSlider` rows inside a `CursorSurface`.

```text
┌──────────────────────────────────────────────┐
│ [icon] WH-1000XM5                      80 %   │  hero
│        CONNECTED · LDAC · ON HEAD   Charging  │
│ status line (Applying…, error, Reading…)      │
│ ──────────────────────────────────────────── │
│ NOISE CONTROL                                 │  always open
│ [ ANC ] [ Ambient ] [ Off ]                   │
│ AMBIENT SOUND LEVEL                  14 / 20  │
│ ────────────●──────────────                   │
│ Focus on Voice                        [ on]   │
│ ──────────────────────────────────────────── │
│ SOUND                          Custom    ▾    │  collapsible, open
│ [Off][Bright][Excited][Mellow][Relaxed]       │
│ [Vocal][Treble][Bass][Speech][Custom]         │
│ DSEE Extreme                          [ on]   │
│ CONNECTION PRIORITY                           │
│ [ Sound quality ] [ Stable connection ]       │
│ Codec: LDAC                                   │
│ CUSTOM EQ                      +2 +1 …   ▸    │  collapsible, closed
│ SPEAK-TO-CHAT                  On        ▸    │  collapsible, closed
│ HEADSET                        Vol 12    ▸    │  collapsible, closed
│ MULTIPOINT                     Off       ▸    │  collapsible, closed
│ ──────────────────────────────────────────── │
│ WH-1000XM5 · firmware 2.3.1 · AC:80:…         │  info footer
│ Limited: headset did not report its features  │  fallback only
└──────────────────────────────────────────────┘
```

A collapsible header shows a one-line summary of its content on the right.
Expand state lives in the widget for the shell session (not persisted).

## 3. Controls

"Key" is the `supported` key that gates the control. "Field" is the
status.json field it shows. Sliders commit on release; keyboard steps commit
after a 400 ms pause.

| Section | Control | Field | Verb | Key |
|---|---|---|---|---|
| Noise | ANC / Ambient / Off chips | `noise_mode` | `noise anc\|ambient\|off` | `noise_control` |
| Noise | Ambient level slider 1..20 (dim, still usable outside Ambient; a change switches to Ambient) | `ambient_sound_level` | `ambient-level <n>` | `noise_control` |
| Noise | Focus on Voice toggle | `voice_passthrough` | `focus-voice on\|off` | `noise_control` |
| Sound | EQ preset chips, grid of 5 columns; list from `eq_presets`, else the v1 list | `eq_preset` | `eq <preset>` | `eq_preset` |
| Sound | DSEE Extreme toggle | `dsee_extreme` | `dsee on\|off` | `dsee` |
| Sound | Connection priority chips: Sound quality / Stable connection; caption "The headset may reconnect." | `connection_quality` | `connection-quality sound\|connection` | `connection_quality` |
| Sound | Codec, read-only text | `codec` | none | `codec` |
| Custom EQ | 5 band sliders (400, 1k, 2.5k, 6.3k, 16k) and Clear Bass, each -10..10 | `eq_custom_bands`, `clear_bass` | `eq custom b1..b5 cb` (all six values, the changed one replaced) | `eq_custom` |
| Speak-to-Chat | toggle | `speak_to_chat` | `speak-to-chat on\|off` | `speak_to_chat` |
| Speak-to-Chat | Sensitivity chips Auto / High / Low | `speak_to_chat_sensitivity` | `speak-to-chat-config <s> <t>` with the current timeout | `speak_to_chat` |
| Speak-to-Chat | Timeout chips Short / Standard / Long / Off | `speak_to_chat_timeout` | `speak-to-chat-config <s> <t>` with the current sensitivity | `speak_to_chat` |
| Headset | Volume slider 0..30 | `volume` | `volume <n>` | `volume` |
| Headset | Voice guidance toggle | `voice_guidance` | `voice-guidance on\|off` | `voice_guidance` |
| Headset | Voice guidance volume slider -2..2 (the headset beeps) | `voice_guidance_volume` | `voice-guidance-volume <n>` | `voice_guidance_volume` |
| Headset | Auto power off chips, grid of 4; list from `auto_power_off_options` | `auto_power_off` | `auto-power-off <v>` | `auto_power_off` |
| Headset | NC/AMB button cycle chips: NC·AMB·Off / NC·AMB / NC·Off / AMB·Off | `nc_amb_button` | `nc-amb-button <v>` | `nc_amb_button` |
| Headset | NC/AMB button function chips, grid of 3; list from `button_functions` | `button_function` | `button-function <v>` | `button_function` |
| Headset | Pause when taken off toggle | `ear_detection` | `ear-detect on\|off` | `ear_detection` |
| Multipoint | Multipoint toggle; caption "The headset reconnects after this change." | `multipoint` | `multipoint on\|off` | `multipoint` |
| Multipoint | Device rows: name (or address), `Playing` / `Connected` / `Not connected` marker | `devices` | none | `devices` |
| Multipoint | Activate a connected, not active row: switch the audio source | `devices[].address` | `source <address>` | `source_switch` |
| Hero | Codec and `On head` / `Off head` in the meta line | `codec`, `worn` | none | `codec`, `worn` |
| Info footer | `model_name`, `firmware_version`, `address`; fallback hint | identity, `supported_source` | none | none |

A section is hidden when all of its controls are hidden.

## 4. Gating and states

Every control resolves to one state through `Model.controlState`:

| Condition (first match wins) | State |
|---|---|
| status not readable, or `connected: false` | hidden (no controls at all) |
| `supported` is `null` | disabled |
| `supported[key]` is not `true` (also a missing key) | hidden |
| field is `null` | disabled (value unknown) |
| otherwise | enabled |

A disabled control is drawn at 45 % opacity and ignores clicks and keys.

| Situation | Hero meta | Body |
|---|---|---|
| file missing, empty, not JSON, not an object, no `schema_version` | `NO STATUS` | text "The sony-headphones daemon wrote no usable status." and the parse error |
| `schema_version` > 2 or not a number | `UPDATE NEEDED` | "The daemon speaks status schema N, this panel reads 2." No controls |
| `connected: false` | `DISCONNECTED` | "Turn the headset on or connect it in Bluetooth settings." No controls |
| `connected: true`, `supported: null` | `READING SETTINGS` | status line "Reading headset settings…"; every section shown, every control disabled |
| `supported_source: "fallback"` | as connected | footer hint "Limited: headset did not report its features" |
| `schema_version: 1` | as connected | v1 support table from contract 2.5; `battery_level: -1` is unknown; footer "Old daemon (status schema 1)" |
| command pending (queue not empty for more than 300 ms) | as connected | status line "Applying…" (dim) |
| command error (`sony-ctl` exit code not 0) | as connected | status line in urgent color with the error text for 6 s; the optimistic value of that command reverts |

Enum values `unknown` (`eq_preset`), `other` (`connection_quality`,
`button_function`) and `OTHER` (`codec`) select no chip; the summary shows
`Unknown` or `Other`.

## 5. Optimistic values and the command queue

- A control change builds the command with `Model.buildCommand(action, value,
  status)`. It returns `sony-ctl` arguments and the optimistic field values,
  or an error (not ready, unsupported, value out of range, value not in the
  headset's list). The UI never sends a command the contract rejects.
- The optimistic values show at once. Each is removed when status.json
  reports the same value, when its command fails, or 4 s after the last
  command finished.
- One `sony-ctl` process runs at a time. A queued (not yet running) command
  with the same verb is replaced by the newer one, which goes to the end of
  the queue, so a slider wheel never builds a backlog.

## 6. Keyboard

The first-party cursor model: no highlight until the first key or hover; the
first key lands on the first target.

| Key | Action |
|---|---|
| `j` `k` `Down` `Up` | next or previous target. Chip grids move by row; Custom EQ bands and the device list move by row |
| `h` `l` `Left` `Right` | walk chips; adjust a slider by one step; on a section header `l` expands and `h` collapses |
| `Enter` `Space` | select the chip, flip the toggle, expand or collapse the header, switch to the device |
| `Esc` | close the popup |
| `Tab` `Shift+Tab` | switch to the neighbour bar panel |

Target order follows the layout: noise chips, ambient slider, Focus on
Voice, Sound header, EQ grid, DSEE, connection priority, Custom EQ header,
band rows, Speak-to-Chat header, toggle, sensitivity, timeout, Headset
header, volume, voice guidance, guidance volume, auto power off, NC/AMB
cycle, button function, pause when taken off, Multipoint header, toggle,
device rows. Hidden controls have no target; disabled controls keep their
target but ignore activation. The panel scrolls to keep the cursor visible.

## 7. Contract readings (safe choices)

1. `auto_power_off_options` is `null`: offer `off`, `5min`, `15min`,
   `30min`, `60min`, `180min`, plus `when_taken_off` only when it is the
   current value.
2. `button_functions` is `null`: offer every contract value except `other`.
3. `speak-to-chat-config` needs both values. While the other value is `null`
   the chip row is disabled.
4. The ambient slider stays usable outside Ambient mode, because
   `ambient-level` switches the mode to Ambient (contract 3.3).
5. A v1 `noise_mode: "wind"` shows the name "Wind Noise Reduction" with no
   chip selected; right click goes to ANC.
6. Fields whose `supported` key is `false` are read as `null` even if the
   file carries a value.
