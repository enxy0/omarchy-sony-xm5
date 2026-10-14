# UI specification: Sony XM5 bar widget (status schema 3)

Binding inputs: [`backend-contract.md`](backend-contract.md) (status.json
schema 3, `headsets`, `primary`, `supported`, every `sony-ctl` verb and the
`--headset` selector). This document says how the widget shows that data.
It does not change the contract.

The widget supports the WH-1000XM5 headphones and the WF-1000XM5 earbuds,
also both at once. Two connected headsets are rare: with one headset the
panel looks and works as before, apart from the earbuds fields. Every rule
in sections 2 to 7 applies to the **shown headset** (section 8); the bar
button follows the `primary` headset.

Files: `Panel.qml` (bar button, popup, cursor model, reusable inline
components), `Service.qml` (status file watcher, optimistic values, command
queue), `Model.js` (all logic: parsing, gating, labels, command building,
pending values, queue, cursor movement, headset selection), `SonyIcon.qml`
(headphones and earbuds glyphs: Material Design Icons from the Nerd Font,
`headphones` U+F02CB and `earbuds` U+F184F, the set the first-party widgets
use; Audio shows the same headphones glyph for a headphone sink).
`setup` deploys `manifest.json` and every `*.qml` and `*.js` file in the
repository root, so a new QML file there needs no change in `setup`.

## 1. Bar button

| Item | Content |
|---|---|
| Size | `BarIconButton`, as the first-party icon widgets (Audio, Bluetooth, Agents): `Style.bar.iconSlot` along the bar, the bar thickness across it. Icon only, no text |
| Headset | The `primary` headset (contract 2.4). The popup's selected tab does not change the bar button |
| Icon | `SonyIcon` with `kind` from the primary headset: the headphones glyph (U+F02CB) for `headphones`, the earbuds glyph (U+F184F) for `earbuds`; the headphones glyph when disconnected. Box `Style.bar.iconCanvas`, glyph at `Style.bar.iconFont`, as `BarIconButton` draws a glyph. The normal foreground colour in every state, also when disconnected or in an error state (first-party icon widgets do the same); green while the bar charging value is `true`, urgent when the bar battery is below 20 % |
| Vertical position | The glyph sits on the same vertical center as the first-party glyph icons (Audio, Bluetooth) next to it. The old vector outline painted from about 0.12 to 0.92 of its box and sat about 1 px (at scale 1.25) below its neighbours. `SonyIcon` now paints the font glyph through `OpticalGlyph`, the component `BarIconButton` uses, in a box of `Style.bar.iconCanvas`; the icon is exactly that box and centered in the button, as `BarIconButton` centers its canvas. Both Material Design glyphs have their ink centered on the same line as the neighbours' glyphs. Measured at scale 1.25: painted center within 0.1 px of the Audio and Bluetooth glyph centers |
| Tooltip, one headset | line 1 `device_name`; line 2 noise mode name and codec (`Noise Cancelling · LDAC`); line 3 battery: headphones `Battery 80% (charging)`, earbuds `Battery L 70% · R 65% · case 53%` (each `—` when `null`; `case` part omitted when `battery_case` is unsupported; `(charging)` after a part whose charging flag is `true`) |
| Tooltip, two or more headsets | one line per headset in `headsets` order: `device_name: noise mode · battery`, for example `WH-1000XM5: Noise Cancelling · 80%` and `WF-1000XM5: Ambient Sound · L 70% R 65% case 53%`. The primary headset's line ends with ` (bar)` |
| Tooltip, other states | Disconnected: `Sony headphones: disconnected`. Initializing (one headset, `supported: null`): `<device_name>: reading headset settings…`; with two or more headsets an initializing entry has that line. Error states: the error text |
| Left click | open or close the popup |
| Right click | cycle the noise mode ANC → Ambient → Off (`noise`) on the **primary** headset, with `--headset <primary id>`. Ignored unless that headset's noise control is enabled (section 4) |

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
Expand state lives in the widget for the shell session (not persisted). It
is per section, shared by all headsets: switching tabs keeps it.

### Two or more headsets: headset switcher

When `headsets` has two or more entries, a switcher row sits at the top of
the popup, above the hero. With one entry the row does not exist and takes
no space.

```text
┌──────────────────────────────────────────────┐
│ ┌─────────────────────┐┌────────────────────┐ │  switcher
│ │[hp] WH-1000XM5  80% ││[eb] WF-1000XM5 65% │ │
│ └─────────────────────┘└────────────────────┘ │
│ [eb] WF-1000XM5              L 70 %  R 65 %   │  hero of the shown headset
│      CONNECTED · LDAC · IN EARS   Case 53 %   │
│ ──────────────────────────────────────────── │
│ ... sections of the shown headset ...         │
└──────────────────────────────────────────────┘
```

| Item | Content |
|---|---|
| Tabs | one per entry, in `headsets` order (headphones first; contract 3.2). Equal width, same height as a chip row |
| Tab content | `SonyIcon` of the entry's `kind` at `Style.space(16)`; `device_name`, elided at the right; the entry's bar battery as `65%` (`—` when `null`), with the bolt while the bar charging value is `true`. Initializing entries show `…` instead of the battery |
| Selected tab | drawn as a selected chip (same colors as the selected EQ chip). Exactly one tab is selected: the shown headset |
| Click | shows that headset (section 8) |

The hero, every section and every command below the switcher belong to the
shown headset.

### Earbuds hero

For an entry with `kind: "earbuds"` the hero changes; the rest of the
layout is the same.

| Item | Headphones (unchanged) | Earbuds |
|---|---|---|
| Icon | headphones glyph, `Style.space(28)` | earbuds glyph, `Style.space(28)` |
| Title | `device_name`; `Sony headset` when no headset is connected | same |
| Battery (right side, first line) | `80%` | `L 70%  R 65%`; `—` for a `null` side; a bolt after a side whose charging flag is `true`. Hidden when `supported.battery_lr` is `false` |
| Battery (right side, second line) | `Charging` while charging | `Case 53%` with a bolt (green) while `battery_case_charging`; `Case —` when `null`; hidden when `supported.battery_case` is `false` |
| Worn in the meta line | `ON HEAD` / `OFF HEAD` from `worn` | `IN EARS` (`worn_left` and `worn_right` `true`), `LEFT IN EAR`, `RIGHT IN EAR`, `NOT IN EARS` (both `false`); nothing when both are `null` |
| Low battery tint | `battery_level` below 20 % | each side value below 20 % is drawn in the urgent color; the case value is never urgent |

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
| Headset | Quick Access: a "Double tap" and a "Triple tap" chip row (Off / Spotify); lists from `quick_access_double_tap_options` and `quick_access_triple_tap_options`. `other` selects no chip and shows the caption "Double tap uses a function this panel does not name." (read-only) | `quick_access_double_tap`, `quick_access_triple_tap` | `quick-access double\|triple <v>` | `quick_access` |
| Headset | Pause when taken off toggle | `ear_detection` | `ear-detect on\|off` | `ear_detection` |
| Headset | Touch controls toggle ("Tap and swipe the ear cup to play, skip and change the volume") | `touch_panel` | `touch-panel on\|off` | `touch_panel` |
| Headset | Earbud touch functions, under the title "TOUCH FUNCTIONS": a "Left earbud" and a "Right earbud" chip row; lists from `touch_function_left_options` and `touch_function_right_options`. Labels: `ambient_sound_control` "Noise control", `playback_control` "Playback", `volume_control` "Volume", `voice_recognition` "Voice assistant", `google_assistant` "Google Assistant", `amazon_alexa` "Alexa", `quick_access` "Quick Access", `none` "Off"; the first three and `none` have a chip tooltip that says what the taps do. Caption "What a tap, double tap and hold on each earbud do." `other` selects no chip and shows "Left earbud uses a function this panel does not name." (read-only) | `touch_function_left`, `touch_function_right` | `touch-function left\|right <v>` | `touch_function` |
| Headset | Hear your voice in calls toggle (sidetone), description "Plays your own voice in the earbuds during a call" | `sidetone` | `sidetone on\|off` | `sidetone` |
| Multipoint | Multipoint toggle; caption "The headset reconnects after this change." | `multipoint` | `multipoint on\|off` | `multipoint` |
| Multipoint | Device rows: name (or address), `Playing` / `Connected` / `Not connected` marker | `devices` | none | `devices` |
| Multipoint | Activate a connected, not active row: switch the audio source | `devices[].address` | `source <address>` | `source_switch` |
| Hero | Codec and the worn text in the meta line (section 2, earbuds hero) | `codec`, `worn`, `worn_left`, `worn_right` | none | `codec`, `worn` |
| Hero | Battery: headphones level, or earbuds L / R and case (section 2) | `battery_level` or `battery_left`, `battery_right`, `battery_case` and their charging flags | none | `battery`, `battery_lr`, `battery_case` |
| Switcher | one tab per headset, only with two or more | `headsets[].id`, `kind`, `device_name`, bar battery | none (UI state) | none |
| Info footer | `model_name`, `firmware_version`, `address` of the shown headset; fallback hint | identity, `supported_source` | none | none |

A section is hidden when all of its controls are hidden.

## 4. Gating and states

Every control resolves to one state through `Model.controlState`:

| Condition (first match wins) | State |
|---|---|
| status not readable, or `headsets` is empty | hidden (no controls at all) |
| the shown headset's `supported` is `null` | disabled |
| `supported[key]` is not `true` (also a missing key) | hidden |
| field is `null` | disabled (value unknown) |
| otherwise | enabled |

A disabled control is drawn at 45 % opacity and ignores clicks and keys.

| Situation | Hero meta | Body |
|---|---|---|
| file missing, empty, not JSON, not an object, no `schema_version` | `NO STATUS` | text "The sony-headphones daemon wrote no usable status." and the parse error |
| `schema_version` > 3 or not a number | `UPDATE NEEDED` | "The daemon speaks status schema N, this panel reads 3." No controls |
| `headsets` is empty | `DISCONNECTED` | "Turn the headset on or connect it in Bluetooth settings." No controls, no switcher |
| shown headset has `supported: null` | `READING SETTINGS` | status line "Reading headset settings…"; every section shown, every control disabled. The switcher stays usable |
| `schema_version: 2` (old daemon still running) | as connected | read as one headset (contract 6, rule 2); footer "Old daemon (status schema 2)"; commands without `--headset`; earbuds fields and new verbs hidden |
| `supported_source: "fallback"` | as connected | footer hint "Limited: headset did not report its features" |
| `schema_version: 1` | as connected | v1 support table from contract 6, rule 2; `battery_level: -1` is unknown; footer "Old daemon (status schema 1)". Verbs the v1 CLI does not have are refused and their controls hidden: Focus on Voice, Speak-to-Chat sensitivity and timeout, bare `eq custom`, `user1`..`user5` |
| `schema_version` not an integer >= 1 (a string of digits counts) | `UPDATE NEEDED` | as "too new" (contract 6, rule 3) |
| command pending (queue not empty for more than 300 ms) | as connected | status line "Applying…" (dim) |
| command error (`sony-ctl` exit code not 0) | as connected | status line in urgent color with the error text for 6 s; the optimistic value of that command reverts |

"Applying…" and the error line belong to the headset the command was for.
They show only while that headset is the shown headset.

Enum values `unknown` (`eq_preset`), `other` (`connection_quality`,
`button_function`, `quick_access_double_tap`, `quick_access_triple_tap`,
`touch_function_left`, `touch_function_right`) and `OTHER` (`codec`) select
no chip; the summary shows `Unknown` or `Other`.

`touch_panel` and `quick_access` were added within schema 2. A daemon older
than these keys has no `supported` entry for them: they read as `false`, the
fields as `null`, and the controls stay hidden (contract 6, rule 2). The
same holds within schema 3 for any `supported` key a daemon does not write
(contract 6, rule 5).

## 5. Optimistic values and the command queue

- A control change builds the command with `Model.buildCommand(action, value,
  status)`. It returns `sony-ctl` arguments and the optimistic field values,
  or an error (not ready, unsupported, value out of range, value not in the
  headset's list). The UI never sends a command the contract rejects.
- The optimistic values show at once. Each is removed when status.json
  reports the same value, when its command fails, or 4 s after the last
  command finished.
- One `sony-ctl` process runs at a time. A queued (not yet running) command
  with the same key is replaced by the newer one, which goes to the end of
  the queue, so a slider wheel never builds a backlog. The key
  (`Model.commandKey`) is the headset `id` plus the verb; for `quick-access`
  also the press and for `touch-function` also the side. So a WH-1000XM5
  volume change never replaces a queued WF-1000XM5 volume change.
- Every command for a schema 3 status carries `--headset <id>` with the
  address of the headset it was built for, also with one headset. The
  widget never relies on the daemon's `primary` for a command.
- Optimistic values are stored per headset `id` (key `<id>|<field>`; an
  entry without an id is for the primary headset). When a headset leaves
  `headsets`, its queued (not yet running) commands are dropped and its
  optimistic values are cleared. A running command finishes; its result is
  ignored. An unreadable status file keeps both.

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
| `[` `]` | show the previous or next headset (switcher order, no wrap). Works from any target; only with two or more headsets |

With two or more headsets the switcher row is the first target row: `h` /
`l` walk the tabs, `Enter` / `Space` shows the tab under the cursor. After
the shown headset changes (by click, key, or because it disconnected), the
target list is rebuilt for the new headset and the cursor goes to the
switcher row.

Target order follows the layout: switcher (two or more headsets only),
noise chips, ambient slider, Focus on
Voice, Sound header, EQ grid, DSEE, connection priority, Custom EQ header,
band rows, Speak-to-Chat header, toggle, sensitivity, timeout, Headset
header, volume, voice guidance, guidance volume, auto power off, NC/AMB
cycle, button function, Quick Access double tap, triple tap, pause when
taken off, touch controls, left earbud touch function, right earbud touch
function, hear your voice in calls, Multipoint header, toggle,
device rows. Hidden controls have no target; disabled controls keep their
target but ignore activation. There is no target while the headset is
disconnected. The panel scrolls to keep the cursor visible.

## 7. Contract readings (safe choices)

1. `auto_power_off_options` is `null`: offer `off`, `5min`, `15min`,
   `30min`, `60min`, `180min`, plus `when_taken_off` only when it is the
   current value.
2. `button_functions` is `null`: offer every contract value except `other`.
   The same holds for `quick_access_double_tap_options` and
   `quick_access_triple_tap_options` (`off`, `spotify`); the daemon skips its
   list check while a list is `null` (contract 4.3).
3. `speak-to-chat-config` needs both values. While the other value is `null`
   the chip row is disabled.
4. The ambient slider stays usable outside Ambient mode, because
   `ambient-level` switches the mode to Ambient (contract 4.3).
5. A v1 `noise_mode: "wind"` shows the name "Wind Noise Reduction" with no
   chip selected; right click goes to ANC.
6. Fields whose `supported` key is `false` are read as `null` even if the
   file carries a value.
7. The Custom EQ chip sends the full `eq custom b1..b5 cb` form with the
   known values; only when they are unknown it sends the bare preset form.
8. The mouse wheel over a slider adjusts the slider, as in the first-party
   Audio panel (`PanelSlider` has no switch for this). A release with an
   unchanged value sends nothing.
9. `touch_function_left_options` or `touch_function_right_options` is
   `null`: offer `ambient_sound_control`, `playback_control`, `none` (the
   list of the WF-1000XM5 fw 6.1.0); the daemon skips its list check while
   a list is `null` (contract 4.3).
10. Earbuds bar battery: the contract rule (3.3, "Bar battery") is the only
    rule. The widget does not guess whether an earbud level of 0 means "in
    the case"; see protocol.md 9.10, question 1.
11. A schema 3 entry whose `id` is not a Bluetooth address, or repeats an
    earlier `id`, is skipped: it cannot be a tab key or a `--headset`
    selector. `connected: true` with an empty `headsets` reads as
    disconnected.
12. Earbuds worn text with one side `null`: nothing is shown, as for both
    `null`; the panel does not say "Left in ear" while the right side is
    unknown.
13. Earbuds tooltip battery line: shown when `battery_lr` or `battery_case`
    is supported, with `—` for an unknown part.

All per-control data (feature key, fields, action, cursor kind, section,
v2-only flag) is one table, `Model.TARGETS`, in layout order
`Model.TARGET_ORDER`. The hero's read-only items (`battery`, `batteryLr`,
`batteryCase`, `wornSides`) are `kind: "display"` rows of the same table:
they gate visibility and have no cursor target. Widget-setting targets are
a second table, `Model.SETTING_TARGETS`; `Model.panelCursorSections` puts
them after the status targets. The switcher is not in `Model.TARGETS`; it
is the cursor row `switcher` in front of them.

## 8. Shown headset

The widget keeps one piece of selection state, `selectedId` (a headset
`id` or `null`). It lives for the shell session and is not persisted.

| Rule | Behavior |
|---|---|
| Shown headset | the entry whose `id` is `selectedId`, if `headsets` has it; else the entry whose `id` is `primary` |
| Click a tab, `Enter` on a tab, `[` / `]` | `selectedId` = that tab's `id` |
| Popup opens | if `selectedId` is `null` or not in `headsets`, set it to `primary`. Else keep it: the user's last pick stays while that headset is connected |
| Shown headset disconnects (its entry leaves `headsets`) | the popup shows `primary` at once; `selectedId` is kept. If the same `id` comes back (for example after the multipoint toggle reconnects it), it is shown again |
| Another headset connects | the switcher appears or gains a tab; the shown headset does not change |
| Only one headset | no switcher; the shown headset is that one, whatever `selectedId` holds |
| `primary` changes | the bar button follows it; the popup follows only when `selectedId` is not in `headsets` |

The shown headset drives every section, the hero, the info footer, the
status line and every command (`--headset <shown id>`). The bar button
always uses `primary`.

`Model.parseStatus` returns a view: the file keys (`primary`, `headsets`,
schema, error state) plus the fields of the primary headset, for v1, v2 and
v3 files alike (contract section 6). `Model.headsetView(status, id)` and
`Model.shownHeadset(status, selectedId)` give the view of another headset;
every gating, label and command function takes a view. Selection helpers:
`shownHeadsetId`, `selectionOnOpen`, `stepHeadset`, `switcherVisible`,
`switcherTabs`, `switcherActivation`. Battery helpers: `barBattery`,
`barCharging`, `barBatteryLevel`, `earbudBatteryParts`, `caseBatteryText`.
Names: `headsetLabel`, `headsetGlyph`.
