# WH-1000XM5 MDR v2 protocol reference

This document describes the Sony MDR protocol, version 2, as the WH-1000XM5
speaks it over Bluetooth RFCOMM. It is the byte-level reference for the
daemon (`daemon/src/MDRProtocol.*`). The contract between the daemon and the
bar widget is a separate document: [`backend-contract.md`](backend-contract.md).

## Sources and confidence

All byte values come from these sources. No value in this document was
captured from a real XM5 by this project.

| Source | License | Used for |
|---|---|---|
| [mos9527/SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient) `libmdr/include/mdr/ProtocolV2.hpp`, `ProtocolV2T1.hpp`, `ProtocolV2T2.hpp`, `libmdr/src/HeadphonesV2*.cpp`, `libmdr/src/DetailsV2.hpp`, `libmdr/src/Command.cpp`, `libmdr/src/Headphones.cpp`, `client/Client.cpp` | MIT | Message layouts, enum values, init order, gating rules |
| mos9527 `docs/device-support/WH-1000XM5.md` | MIT | Which features users confirmed on a real XM5 |
| Gadgetbridge `SonyWH1000XM5Coordinator.java`, `SonyProtocolImplV2.java` | AGPL-3.0 | Facts only (feature list of the XM5, a few byte values). No code or text is copied |

Each feature table has a **Confidence** value:

- **verified on XM5**: mos9527 `WH-1000XM5.md` marks the feature as working, or
  the Gadgetbridge XM5 coordinator enables it.
- **in code, unverified on XM5**: the layout exists in mos9527 (or Gadgetbridge
  for other models), but no source confirms it on an XM5.

Notation: all numbers are hexadecimal unless written in words. A request or
reply is shown as its **payload** bytes only (what goes inside the frame). The
"Table" column says which packet type carries it: `T1` = `0x0C`, `T2` = `0x0E`.

## 1. Framing

The repository's `packFrame` / `unpackFrame` / `StreamFramer` in
`daemon/src/MDRProtocol.cpp` match mos9527 `libmdr/src/Command.cpp`
(`MDRPackCommand`, `MDRUnpackCommand`, `Escape`, `Unescape`). Confirmed
byte for byte:

```
3E | type | seq | len (4 bytes, big-endian) | payload (len bytes) | checksum | 3C
```

| Field | Size | Meaning |
|---|---|---|
| start | 1 | always `3E` (`>`) |
| type | 1 | packet type, see below |
| seq | 1 | sequence number, `00` or `01` |
| len | 4 | payload length, big-endian, unescaped |
| payload | len | the message; first byte is the command |
| checksum | 1 | sum of `type`, `seq`, `len` and `payload` bytes, modulo 256 |
| end | 1 | always `3C` (`<`) |

Escaping is applied to everything between the markers (type through
checksum), after the checksum is computed:

| Raw byte | On the wire |
|---|---|
| `3C` | `3D 2C` |
| `3D` | `3D 2D` |
| `3E` | `3D 2E` |

Note: table 2 command `PERI_SET_EXTENDED_PARAM` is `3C`. Its first payload
byte is always escaped to `3D 2C`. The existing `escapeBytes` handles this.

Example, GET battery (`22 00`) with seq `00`:

```
3E 0C 00 00 00 00 02 22 00 30 3C
```

### Packet types

| Type | Name | Use |
|---|---|---|
| `01` | ACK | acknowledges one data packet; payload is empty |
| `0C` | DATA_MDR | table 1 messages (most features) |
| `0E` | DATA_MDR_NO2 | table 2 messages (multipoint devices, voice guidance, wearing status) |

Table 1 and table 2 use overlapping command numbers (for example `23` is
`POWER_RET_STATUS` in both). The receiver must dispatch on the packet type
first, then on the command byte.

### ACK and sequence rules

From mos9527 `libmdr/src/Headphones.cpp` (`SendACK`, `HandleAck`) and
`libmdr/src/Details.hpp` (`SendCommandACK`):

1. The host keeps one sequence number `S`, starting at `00` after connect.
   Table 1 and table 2 share it.
2. The host sends a data packet with `seq = S`, then waits for an ACK.
3. The device ACKs with `seq = 1 - S`. When the host receives that ACK, it
   sets `S = 1 - S`. An ACK with any other seq is stale; ignore it.
4. Only one unacknowledged data packet may be in flight. mos9527 waits
   1000 ms per try and retries up to 10 times with the **same** seq.
5. For every data packet the device sends (type `0C` or `0E`), the host
   replies with an ACK: type `01`, `seq = 1 - received seq`, empty payload.
   Example: device sends seq `00`, host sends `3E 01 01 00 00 00 00 02 3C`.
6. A reply (RET) can arrive before the ACK of its request (seen on the
   WH-1000XM3). Accept a reply at any time; do not require ACK first.

## 2. Common value encodings

Several enums invert the usual meaning of 0 and 1. Check the table for every
field.

| Enum | Values |
|---|---|
| `OnOffSettingValue` | `00` ON, `01` OFF |
| `EnableDisable` | `00` ENABLE, `01` DISABLE |
| `GsSettingValue` | `00` ON, `01` OFF |
| `NcAsmOnOffValue` | `00` OFF, `01` ON |
| `UpscalingTypeAutoOff` | `00` OFF, `01` AUTO |
| `ValueChangeStatus` | `00` UNDER_CHANGING (slider is moving), `01` CHANGED |
| Prefixed string | 1 byte length N, then N bytes of text (no terminator) |
| Prefixed array | 1 byte element count N, then N elements |
| Bluetooth address | 17 ASCII bytes, format `AA:BB:CC:DD:EE:FF` |

Commands follow a fixed pattern per group: `GET_x` (host asks), `RET_x`
(device answers), `SET_x` (host writes), `NTFY_x` (device pushes a change,
also after a host SET). For a SET, the device usually answers with NTFY.

## 3. Init sequence

Order from mos9527 `libmdr/src/HeadphonesV2.cpp`, `RequestInitV2`. Each line
is one request; send it, wait for its ACK, then send the next. Steps marked
"gate" are sent only when the support function list contains the ID.

### 3.1 Identity

| # | Table | Request | Reply | Notes |
|---|---|---|---|---|
| 1 | T1 | `00 00` CONNECT_GET_PROTOCOL_INFO | `01 00 VV VV VV VV T1 T2` | `VV` = protocol version, 4 bytes big-endian. `T1`/`T2`: `00` = table supported (`EnableDisable`). Stop if `T1` is not `00`. Reply length is exactly 8 bytes |
| 2 | T1 | `02 00` CONNECT_GET_CAPABILITY_INFO | `03 00 CC <string uniqueId>` | `CC` = capability counter |
| 3 | T1 | `04 02` CONNECT_GET_DEVICE_INFO, FW_VERSION | `05 02 <string fwVersion>` | ASCII, for example a version string like `"1.2.3"` |
| 4 | T1 | `04 01` CONNECT_GET_DEVICE_INFO, MODEL_NAME | `05 01 <string modelName>` | ASCII model name |
| 5 | T1 | `04 03` CONNECT_GET_DEVICE_INFO, SERIES_AND_COLOR | `05 03 SS CC` | `SS` = model series (`30` PREMIUM), `CC` = color (`01` BLACK, `03` SILVER, ...) |
| 6 | T1 | `06 00` CONNECT_GET_SUPPORT_FUNCTION | `07 00 NN (FF PP) x NN` | `FF` = table 1 function ID, `PP` = priority (unused) |
| 7 | T2 | `06 00` CONNECT_GET_SUPPORT_FUNCTION | `07 00 NN (FF PP) x NN` | only if step 1 said `T2 = 00`. `FF` = table 2 function ID |

Steps 6 and 7 are the source of every "gate" below. The official app caches
them per Bluetooth address.

### 3.2 Settings reads

| # | Table | Gate (support function) | Request | Purpose |
|---|---|---|---|---|
| 8 | T1 | `D1`..`D4` GENERAL_SETTING_n | `D0 Dn 01` then `D6 Dn` | capability (subject string) and value of general setting slot `n`; `01` = English |
| 9 | T1 | `E2` UPSCALING_AUTO_OFF | `E0 01` | DSEE capability (upscaling type) |
| 10 | T1 | `FE` or `F3` ASSIGNABLE_SETTING | `F0 0E` (if `FE`) else `F0 03` | button assignment capability |
| 11 | T1 | `90` FIXED_MESSAGE | `94 00 00` ALERT_SET_STATUS FIXED_MESSAGE ENABLE | lets the device ask for confirmation (multipoint toggle) |
| 12 | T1 | `12` CODEC_INDICATOR | `12 02` | current codec |
| 13 | T1 | any of `A1`..`A4` PLAYBACK_CONTROLLER_* | `A6 20` | headset volume (mos9527 also reads track metadata here; we skip it) |
| 14 | T1 | `6B` (else `6D`, else `67`) | `66 17` (for `6D`: `66 19`; for `67`: `66 22`) | NC/ASM state |
| 15 | T2 | `32` or `33` | `32 02` then `36 02` | pairing mode and paired device list (with class of device) |
| 16 | T2 | `30` | `32 00` then `36 00` | same, without class of device |
| 17 | T2 | `31` SOURCE_SWITCH_CONTROL | `36 01` | automatic source switch flag |
| 18 | T1 | `FC` SMART_TALKING_MODE_TYPE2 | `F6 0C` then `FA 0C` | Speak-to-Chat on/off, then sensitivity and timeout |
| 19 | T1 | `50` PRESET_EQ (see section 5.3) | `50 00 01`, `52 00`, `56 00` | EQ preset list, EQ available, EQ value |
| 20 | T1 | `E1` (or `E3`) | `E6 00` (for `E3`: `E6 02`) | connection quality mode |
| 21 | T1 | `E2` UPSCALING_AUTO_OFF | `E2 01` then `E6 01` | DSEE available, DSEE value |
| 22 | T1 | `FE` or `F3` | `F6 0E` or `F6 03` | button assignment value |
| 23 | T1 | `69` AMBIENT_SOUND_CONTROL_MODE_SELECT | `66 30` | NC/AMB button cycle |
| 24 | T1 | `24` (else `25`) | `26 04` (for `25`: `26 05`) | auto power off |
| 25 | T1 | `F1` PLAYBACK_CONTROL_BY_WEARING_REMOVING_HEADPHONE_ON_OFF | `F6 01` | pause when taken off |
| 26 | T2 | table 2 present and (T2 `F0` or T1 `F6` or T1 `F1`) | `F2 00` | worn status |
| 27 | T2 | any of T2 `40`..`45` | `46 01` (and `46 20` if T2 `42` or `43`) | voice guidance on/off, voice guidance volume |
| 28 | T1 | none | `C4 01 00` LOG_SET_STATUS, TIME_SERIES_OPERATIONLOG_NOTIFIER, ENABLE | the device then sends operation log lines (`C9`), including `"unitWear"` / `"unitRemove"` |

### 3.3 Periodic sync

mos9527 `RequestSyncV2` re-reads values that the device does not always push:

| Table | Gate | Request |
|---|---|---|
| T1 | `20` BATTERY_LEVEL_INDICATOR (else `28`) | `22 00` (for `28`: `22 08`) |
| T1 | any `A1`..`A4` | `A6 20` |
| T2 | as step 26 | `F2 00` |

The battery is also pushed with `25` (NTFY). Re-read the worn status when a
`C9 01` log line carries `"unitWear"` or `"unitRemove"`; the device does not
push `F5 00` by itself (mos9527 `DetailsV2.hpp` comment).

## 4. Function ID tables

Only the IDs this project uses. Full lists: mos9527 `ProtocolV2.hpp`,
`enum class FunctionType` in namespaces `t1` and `t2`.

### Table 1 function IDs

| ID | Name | Feature |
|---|---|---|
| `12` | CODEC_INDICATOR | codec |
| `20` | BATTERY_LEVEL_INDICATOR | battery |
| `23` | POWER_OFF | power off (not in the contract) |
| `24` | AUTO_POWER_OFF | auto power off, timer only |
| `25` | AUTO_POWER_OFF_WITH_WEARING_DETECTION | auto power off, with "when taken off" |
| `28` | BATTERY_LEVEL_WITH_THRESHOLD | battery, variant |
| `50` | PRESET_EQ | EQ presets and custom EQ |
| `67` | AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT | ambient only (no NC) |
| `68` | MODE_NC_ASM_..._AUTO_... | NC/ASM, inquired type `15` |
| `69` | AMBIENT_SOUND_CONTROL_MODE_SELECT | NC/AMB button cycle |
| `6B` | MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT | NC/ASM, inquired type `17` |
| `6D` | same + NOISE_ADAPTATION | NC/ASM, inquired type `19` |
| `90` | FIXED_MESSAGE | confirmation alerts |
| `A1`..`A4` | PLAYBACK_CONTROLLER_* | headset volume |
| `D1`..`D4` | GENERAL_SETTING_1..4 | general setting slots (multipoint, touch panel) |
| `E1` | CONNECTION_MODE_SOUND_QUALITY_CONNECTION_QUALITY | connection quality |
| `E2` | UPSCALING_AUTO_OFF | DSEE |
| `E3` | CONNECTION_MODE_..._WITH_LDAC_STATUS_... | connection quality, variant |
| `ED` | UPSCALING_AUTO_OFF_WITH_STATUS_DISABLE_REASON | DSEE, variant (not supported by mos9527) |
| `F1` | PLAYBACK_CONTROL_BY_WEARING_REMOVING_HEADPHONE_ON_OFF | pause when taken off |
| `F3` | ASSIGNABLE_SETTING | button assignment |
| `F6` | WEARING_STATUS_DETECTOR | worn sensor |
| `FC` | SMART_TALKING_MODE_TYPE2 | Speak-to-Chat |
| `FE` | ASSIGNABLE_SETTING_WITH_LIMITATION | button assignment, variant |

### Table 2 function IDs

| ID | Name | Feature |
|---|---|---|
| `30` | PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT | paired device list, inquired type `00` |
| `31` | SOURCE_SWITCH_CONTROL | switch the playing source |
| `32` | PAIRING_DEVICE_MANAGEMENT_WITH_BLUETOOTH_CLASS_OF_DEVICE_CLASSIC_BT | paired device list, inquired type `02` |
| `33` | same, CLASSIC_LE | paired device list, inquired type `02` |
| `40` | VOICE_GUIDANCE_..._NOT_SUPPORT_LANGUAGE_SWITCH | voice guidance, inquired type `00` |
| `41` | VOICE_GUIDANCE_..._SUPPORT_LANGUAGE_SWITCH | voice guidance, inquired type `01` |
| `42` | same + VOLUME_ADJUSTMENT | voice guidance and its volume |
| `43` | VOICE_GUIDANCE_VOLUME_SETTING_MTK_FIXED_TO_5_STEPS | voice guidance volume |
| `44` | VOICE_GUIDANCE_SETTING_SUPPORT_LANGUAGE_SWITCH | voice guidance, inquired type `02` |
| `45` | VOICE_GUIDANCE_SETTING_ONLY_ON_OFF_SWITCH | voice guidance, inquired type `03` |
| `F0` | WEARING_STATUS_CHECKER | worn sensor |

## 5. Features

Each feature lists the gate, the requests, the reply layout and the value
enums. "Reply" covers both RET (answer to GET) and NTFY (push); they have the
same layout unless a row says otherwise.

### 5.1 Battery and charging

| Item | Value |
|---|---|
| Confidence | **verified on XM5** (mos9527 "Battery Life", Gadgetbridge `BatterySingle`) |
| Gate | T1 `20` (type `00`), else T1 `28` (type `08`) |
| GET | T1 `22 00` (or `22 08`) |
| RET | `23 00 LL CC` (or `23 08 LL CC TT`) |
| NTFY | `25 00 LL CC` (or `25 08 LL CC TT`) |
| SET | none |

| Byte | Meaning |
|---|---|
| `LL` | level 0..100 (`64` = 100) |
| `CC` | `BatteryChargingStatus`: `00` not charging, `01` charging, `02` unknown, `03` charged (full, still on the cable) |
| `TT` | threshold (only in the `08` variant) |

### 5.2 Noise cancelling and ambient sound (NC/ASM)

| Item | Value |
|---|---|
| Confidence | **verified on XM5** (mos9527 "NC/AMB Settings", Gadgetbridge `AmbientSoundControl`), including ambient level and Focus on Voice |
| Gate | T1 `6B` → inquired type `17`. T1 `6D` → type `19`. T1 `68` → type `15` (Gadgetbridge only) |
| GET | T1 `66 17` |
| RET | `67 17 VS TE MO AM LV` |
| NTFY | `69 17 VS TE MO AM LV` |
| SET | T1 `68 17 VS TE MO AM LV` |

| Byte | Meaning |
|---|---|
| `VS` | `ValueChangeStatus`: send `00` while the ambient slider is moving, `01` for the final value |
| `TE` | NC/ASM total effect (`NcAsmOnOffValue`): `00` off (mode "off"), `01` on |
| `MO` | `NcAsmMode`: `00` noise cancelling, `01` ambient sound |
| `AM` | `AmbientSoundMode`: `00` normal, `01` voice (Focus on Voice) |
| `LV` | ambient level, 1..20 (mos9527 client slider range in V2) |

Mode mapping used by the daemon:

| Contract `noise_mode` | `TE` | `MO` |
|---|---|---|
| `anc` | `01` | `00` |
| `ambient` | `01` | `01` |
| `off` | `00` | keep the last `MO` |

When the host switches mode, it sends the last known `AM` and `LV` unchanged
(mos9527 `RequestCommitV2` always sends the stored values). The device keeps
`AM` and `LV` while in NC or off mode and reports them in RET.

The XM5 has no manual "wind noise reduction" mode in this message. Wind noise
reduction is automatic on the XM5.

Variant `19` (gate T1 `6D`) adds two bytes after `LV`:
`NA` = auto ambient sound (`NcAsmOnOffValue`) and `NS` =
`NoiseAdaptiveSensitivity` (`00` standard, `01` high, `02` low). Variant `15`
has the same 7-byte layout as `17`. Gadgetbridge's V2 code selects `15` for
every model without its "AmbientSoundControl2" capability, and its XM5
coordinator does not list that capability.

### 5.3 Equalizer: presets, custom 5-band, Clear Bass

| Item | Value |
|---|---|
| Confidence | presets: **verified on XM5** (Gadgetbridge `EqualizerWithCustomBands`; mos9527 notes "no way to choose presets" in its UI). Custom bands and Clear Bass: **verified on XM5** (mos9527 "Equalizer", Gadgetbridge) |
| Gate | T1 `50` PRESET_EQ |
| GET capability | T1 `50 00 01` (inquired type PRESET_EQ, language English) |
| RET capability | `51 00 BB SS NN (PID <string name>) x NN` |
| GET status | T1 `52 00` |
| RET status | `53 00 EN` (NTFY `55 00 EN`). `EN` `00` = EQ can be used, `01` = EQ is disabled by the device |
| GET value | T1 `56 00` |
| RET value | `57 00 PID KK (step) x KK` (NTFY `59 00 ...`) |
| SET preset | T1 `58 00 PID 00` |
| SET custom | T1 `58 00 A0 06 CB B1 B2 B3 B4 B5` |

`BB` = band count, `SS` = steps, both as reported; do not hard-code them.
`KK` is the number of band steps that follow:

| `KK` | Layout |
|---|---|
| `00` | preset only, no band values |
| `06` | `CB B1 B2 B3 B4 B5`: Clear Bass, then 400 Hz, 1 kHz, 2.5 kHz, 6.3 kHz, 16 kHz. Each value is `level + 10` (wire `00`..`14` = -10..+10) |
| `0A` | 10-band EQ (other models), each value `level + 6`. Not used by the XM5 |

Preset IDs (`EqPresetId`):

| PID | Contract value | PID | Contract value |
|---|---|---|---|
| `00` | `off` | `15` | `treble` (Treble Boost) |
| `10` | `bright` | `16` | `bass` (Bass Boost) |
| `11` | `excited` | `17` | `speech` |
| `12` | `mellow` | `A0` | `custom` |
| `13` | `relaxed` | `A1`..`A5` | `user1`..`user5` (USER_SETTING1..5) |
| `14` | `vocal` | other | `unknown` |

The capability reply lists the IDs the device accepts and their display
names. After a SET, mos9527 sends `56 00` again to read the result.

### 5.4 DSEE Extreme (upscaling)

| Item | Value |
|---|---|
| Confidence | **in code, unverified on XM5** (Gadgetbridge marks DSEE as TODO for the XM5) |
| Gate | T1 `E2` UPSCALING_AUTO_OFF |
| GET capability | T1 `E0 01` → `E1 01 UT` |
| GET status | T1 `E2 01` → `E3 01 EN` (NTFY `E5 01 EN`); `EN` `00` = available |
| GET value | T1 `E6 01` → `E7 01 VV` (NTFY `E9 01 VV`) |
| SET | T1 `E8 01 VV` |

| Byte | Meaning |
|---|---|
| `UT` | `UpscalingType`: `00` DSEE HX, `01` DSEE, `02` DSEE HX AI, `03` DSEE Ultimate |
| `VV` | `UpscalingTypeAutoOff`: `00` off, `01` auto |

If the XM5 advertises T1 `ED` (UPSCALING_AUTO_OFF_WITH_STATUS_DISABLE_REASON,
inquired type `0B`) instead of `E2`, this layout does not apply; mos9527 has
no implementation for it.

### 5.5 Speak-to-Chat

| Item | Value |
|---|---|
| Confidence | on/off: **verified on XM5** (Gadgetbridge `SpeakToChatEnabled`; mos9527 marks it untested). Sensitivity and timeout: **verified on XM5** (Gadgetbridge `SpeakToChatConfig`) |
| Gate | T1 `FC` SMART_TALKING_MODE_TYPE2 |
| GET on/off | T1 `F6 0C` → `F7 0C OO PP` (NTFY `F9 0C OO PP`) |
| SET on/off | T1 `F8 0C OO 01` |
| GET config | T1 `FA 0C` → `FB 0C SE TI` (NTFY `FD 0C SE TI`) |
| SET config | T1 `FC 0C SE TI` |

| Byte | Meaning |
|---|---|
| `OO` | `OnOffSettingValue`: `00` on, `01` off |
| `PP` | preview mode, `OnOffSettingValue`; always send `01` (off) |
| `SE` | `DetectSensitivity`: `00` auto, `01` high, `02` low |
| `TI` | `ModeOutTime`: `00` short (FAST), `01` standard (MID), `02` long (SLOW), `03` off (NONE, never return automatically) |

### 5.6 Voice guidance

| Item | Value |
|---|---|
| Confidence | on/off: **verified on XM5** (Gadgetbridge `VoiceNotifications`). Volume: **in code, unverified on XM5**; mos9527 marks "Voice Guidance Volume ❌" for the XM5, so expect the gate to be absent |
| Gate on/off | T2 `40` → type `00`; T2 `41` or `42` → type `01`; T2 `44` → type `02`; T2 `45` → type `03` |
| Gate volume | T2 `42` or `43` |
| GET on/off | T2 `46 01` → `47 01 VV` |
| SET on/off | T2 `48 01 VV` |
| GET volume | T2 `46 20` → `47 20 VL` |
| SET volume | T2 `48 20 VL 00` |

| Byte | Meaning |
|---|---|
| `VV` | `OnOffSettingValue`: `00` on, `01` off |
| `VL` | signed byte, -2..+2 (mos9527 client slider) |
| last byte of SET volume | feedback sound, `OnOffSettingValue`; `00` = play a test beep |

mos9527 always uses type `01` for on/off (gates `41`/`42`). Table 2 has no
NTFY for voice guidance; send the GET again after a SET.

### 5.7 Auto power off

| Item | Value |
|---|---|
| Confidence | **verified on XM5** (Gadgetbridge `AutomaticPowerOffWhenTakenOff`, which uses type `05`) |
| Gate | T1 `25` → type `05` (with "when taken off"); T1 `24` → type `04` |
| GET | T1 `26 05` → `27 05 CU LA` (NTFY `29 05 CU LA`) |
| SET | T1 `28 05 CU LA` |

`CU` = current setting, `LA` = last selected timer:

| Value | Contract value |
|---|---|
| `00` | `5min` |
| `04` | `15min` |
| `01` | `30min` |
| `02` | `60min` |
| `03` | `180min` |
| `10` | `when_taken_off` (type `05` only) |
| `11` | `off` |

For SET, send `LA = CU` when `CU` is a timer (`00`..`04`), else `LA = 00`.
mos9527 always sends `LA = 00`; Gadgetbridge sends the pairs above. Both are
reported to work.

### 5.8 NC/AMB button: cycle and assignment

Two settings exist. The cycle setting chooses which modes the NC/AMB button
steps through. The assignment chooses what the button does at all.

**Cycle** (NC_AMB_TOGGLE):

| Item | Value |
|---|---|
| Confidence | **verified on XM5** (mos9527 "NC/AMB Button Setting") |
| Gate | T1 `69` AMBIENT_SOUND_CONTROL_MODE_SELECT |
| GET | T1 `66 30` → `67 30 FN` (NTFY `69 30 FN`) |
| SET | T1 `68 30 FN` |

| `FN` | Contract value | Button cycles through |
|---|---|---|
| `01` | `nc_ambient_off` | NC → Ambient → Off |
| `02` | `nc_ambient` | NC → Ambient |
| `03` | `nc_off` | NC → Off |
| `04` | `ambient_off` | Ambient → Off |

**Assignment** (ASSIGNABLE_SETTING):

| Item | Value |
|---|---|
| Confidence | **in code, unverified on XM5** |
| Gate | T1 `FE` → type `0E`; else T1 `F3` → type `03` |
| GET capability | T1 `F0 03` → `F1 03 NN (key) x NN`. For type `0E`: `F1 0E LT NN (key) x NN` |
| GET value | T1 `F6 03` → `F7 03 NN (preset) x NN` (NTFY `F9 03 ...`) |
| SET | T1 `F8 03 NN (preset) x NN` |

Each key in the capability is: `KEY TYPE DEFAULT NP (presetCapability) x NP`,
and each preset capability is `PRESET NA (ACTION FUNCTION) x NA NC
(ACTION DEFAULT NF (FUNCTION) x NF) x NC`. See mos9527 `AssignableSettingsKey`
and `AssignableSettingsPresetCapability`. `KEY` `04` = NC_AMB_KEY,
`05` = NC_AMBIENT_KEY, `02` = CUSTOM.

The value list has one preset per key, in capability key order. SET must
send the full list; change only the NC/AMB key.

| Preset | Contract value |
|---|---|
| `00` | `ambient_sound_control` |
| `30` | `voice_recognition` |
| `31` | `google_assistant` |
| `32` | `amazon_alexa` |
| `36` | `quick_access` |
| `FF` | `none` |
| other | `other` |

### 5.9 Wearing: pause when taken off, worn status

**Pause when taken off** (the v1 contract calls it `ear_detection`):

| Item | Value |
|---|---|
| Confidence | **verified on XM5** (Gadgetbridge `PauseWhenTakenOff`) |
| Gate | T1 `F1` |
| GET | T1 `F6 01` → `F7 01 VV` (NTFY `F9 01 VV`) |
| SET | T1 `F8 01 VV` |
| `VV` | `OnOffSettingValue`: `00` on, `01` off |

**Worn status** (read only):

| Item | Value |
|---|---|
| Confidence | **in code, unverified on XM5** |
| Gate | table 2 present and (T2 `F0` or T1 `F6` or T1 `F1`) |
| GET | T2 `F2 00` → `F3 00 WS` (NTFY `F5 00 WS`, rarely sent) |

| `WS` | Meaning |
|---|---|
| `00` | NORMAL, worn |
| `01` | ILLEGAL, sensor error |
| `02` / `03` | left / right side not worn |
| `04` | not worn |

### 5.10 Headset volume

| Item | Value |
|---|---|
| Confidence | **verified on XM5** (mos9527 "Volume") |
| Gate | any of T1 `A1`..`A4` |
| GET | T1 `A6 20` → `A7 20 VL` (NTFY `A9 20 VL`) |
| SET | T1 `A8 20 VL` |
| `VL` | 0..30 (mos9527 client slider range) |

### 5.11 Connection quality (sound vs connection)

| Item | Value |
|---|---|
| Confidence | **in code, unverified on XM5** |
| Gate | T1 `E1` → type `00`; T1 `E3` → type `02` |
| GET | T1 `E6 00` → `E7 00 PM` (NTFY `E9 00 PM`) |
| SET | T1 `E8 00 PM` |

| `PM` | `PriorMode` | Contract value |
|---|---|---|
| `00` | SOUND_QUALITY_PRIOR | `sound` |
| `01` | CONNECTION_QUALITY_PRIOR | `connection` |
| `02` | LOW_LATENCY_PRIOR_BETA | `other` |

Gadgetbridge uses type `02` with the same third byte on other V2 models.
Changing this mode can make the device disconnect; it may first send a
fixed-message alert `99 00 00 ..` (DISCONNECT_CAUSED_BY_CONNECTION_MODE_CHANGE),
see 5.13.

### 5.12 Current codec

| Item | Value |
|---|---|
| Confidence | **in code, unverified on XM5** |
| Gate | T1 `12` CODEC_INDICATOR |
| GET | T1 `12 02` → `13 02 CD` |
| NTFY | `15 02 CD` (sent when the codec changes) |
| SET | none |

| `CD` | Contract value |
|---|---|
| `00` | `NONE` (UNSETTLED, no audio stream) |
| `01` | `SBC` |
| `02` | `AAC` |
| `10` | `LDAC` |
| `20` | `APTX` |
| `21` | `APTX_HD` |
| `30` | `LC3` |
| other | `OTHER` |

### 5.13 Multipoint on/off

The XM5 exposes "connect to 2 devices" as a **general setting** slot. The slot
number is not fixed; the capability reply names it.

| Item | Value |
|---|---|
| Confidence | **in code, unverified on XM5** (mos9527 "Multipoint Control ?"; Gadgetbridge lists it as TODO for the XM5) |
| Gate | T1 `Dn` (GENERAL_SETTING_n, n = 1..4) **and** the slot capability subject is `"MULTIPOINT_SETTING"` |
| GET capability | T1 `D0 Dn 01` → `D1 Dn ST SF <string subject> <string summary>` |
| GET value | T1 `D6 Dn` → `D7 Dn 00 VV` (NTFY `D9 Dn 00 VV`) |
| SET | T1 `D8 Dn 00 VV` |

| Byte | Meaning |
|---|---|
| `Dn` | slot: `D1`, `D2`, `D3` or `D4` |
| `ST` | `GsSettingType`: `00` boolean, `01` list. Only boolean is handled |
| `SF` | `GsStringFormat`: `00` raw text, `01` enum name |
| subject | with `SF = 01`: `"MULTIPOINT_SETTING"`, `"TOUCH_PANEL_SETTING"`, `"SIDETONE_SETTING"` (names from mos9527 `client/Client.cpp`) |
| `VV` | `GsSettingValue`: `00` on, `01` off |

**Confirmation alert.** Toggling multipoint makes the device disconnect and
reconnect. The device asks first with a fixed message (needs init step 11):

| Direction | Payload | Meaning |
|---|---|---|
| device → host | T1 `99 00 MT AT` | ALERT_NTFY_PARAM, FIXED_MESSAGE. `MT` = message type, `07` DISCONNECT_CAUSED_BY_CHANGING_MULTIPOINT or `06` ..._MULTIPOINT_LDAC_DISABLE. `AT` = `01` POSITIVE_NEGATIVE (needs an answer), `00` CONFIRMATION_ONLY |
| host → device | T1 `98 00 MT 01` | ALERT_SET_PARAM: `01` = positive (accept), `00` = negative (cancel). Echo the received `MT` |

Gadgetbridge (other V2 models) sends `98 00 06 01` right after the general
setting write without waiting for the alert. After the answer, the headset
drops the RFCOMM link; the daemon must reconnect.

### 5.14 Paired devices, connected devices, switch source

| Item | Value |
|---|---|
| Confidence | **in code, unverified on XM5** (mos9527 "Multipoint Control ?", not tested) |
| Gate list | T2 `32` or `33` → inquired type `02`; else T2 `30` → type `00` |
| Gate switch | T2 `31` SOURCE_SWITCH_CONTROL |

Device list:

| Request | Reply |
|---|---|
| T2 `36 02` | `37 02 NN (device) x NN PB` (NTFY `39 02 ...`) |
| T2 `36 00` | `37 00 NN (device) x NN PB` (NTFY `39 00 ...`) |

Device entry for type `02`: `ADDR[17] CS COD[3] <string name>`.
Device entry for type `00`: `ADDR[17] CS <string name>`.

| Field | Meaning |
|---|---|
| `ADDR` | Bluetooth address, 17 ASCII bytes |
| `CS` | connection status: `00` = not connected; any other value = connected, the value is a connection slot number |
| `COD` | Bluetooth class of device, 3 bytes big-endian |
| `PB` | slot number of the device that plays audio. A device is the active source when its `CS == PB` and `CS != 00` |

Switch the playing source (active device):

| Direction | Payload |
|---|---|
| host → device | T2 `3C 01 ADDR[17]` (PERI_SET_EXTENDED_PARAM, SOURCE_SWITCH_CONTROL) |
| device → host | T2 `3D 01 RS ADDR[17]` (PERI_NTFY_EXTENDED_PARAM) |

`RS` (`SourceSwitchControlResult`): `00` success, `01` fail, `02` fail:
calling, `03` fail: A2DP not connected, `04` fail: voice assistant has
priority.

Other operations in the same group (not in the contract yet):

| Request | Meaning |
|---|---|
| T2 `3C TT AC ADDR[17]` | connect (`AC = 01`), disconnect (`00`) or unpair (`02`) a paired device; `TT` = list type. Result NTFY `3D TT AC RS ADDR[17]` |
| T2 `32 TT` → `33 TT BM EN` | pairing mode: `BM` `00` normal, `01` inquiry scan (pairing) |
| T2 `36 01` → `37 01 VV` | automatic source switch enabled, `VV != 00` = on. SET `38 01 VV` |

### 5.15 Firmware version and model name

| Item | Value |
|---|---|
| Confidence | **in code, unverified on XM5** (mos9527 sends these during init for every V2 device; no XM5 capture is published) |
| Gate | none |
| Requests | init steps 3, 4, 5 in section 3.1 |

### 5.16 Power off (extra, not in the contract)

| Item | Value |
|---|---|
| Confidence | **verified on XM5** (mos9527 "Power Off", Gadgetbridge `PowerOffFromPhone`) |
| Gate | T1 `23` POWER_OFF |
| SET | T1 `24 03 01` (POWER_SET_STATUS, POWER_OFF, USER_POWER_OFF) |

## 6. Features we skip

| Feature | Reason |
|---|---|
| Playback control and track metadata (T1 `A2`/`A4`, `A6 01`) | The desktop already controls playback through MPRIS and AVRCP. mos9527 reports the XM5 metadata notify as broken |
| Safe listening / sound pressure (T2 `5A`, `58`) | Needs periodic polling and a health-style UI; no value for a bar widget |
| 360 Reality Audio | Needs the Sony app account and ear photo analysis; no MDR setting is useful on a PC |
| Touch panel on/off (general setting `"TOUCH_PANEL_SETTING"`) | Can be added later with the same mechanism as multipoint (5.13) |
| Quick Access, head gestures, listening modes (BGM, cinema) | Not requested for this widget |

## 7. Upstream implementation review

This section compares the bytes that upstream `daemon/src/MDRProtocol.cpp`
and `daemon/src/main.cpp` send today with the reference above.

| # | Location | Finding | Verdict |
|---|---|---|---|
| 1 | `main.cpp` `onConnected` | No init sequence. The daemon never sends CONNECT_GET_PROTOCOL_INFO (`00 00`) or the support function requests (`06 00` on T1 and T2). No feature is gated; the daemon assumes every feature exists | bug |
| 2 | `main.cpp` `nextSeq`, `onConnected` | Six GET requests are sent back to back. The seq flips on every send, not on every received ACK, and incoming ACK packets (type `01`) are ignored. There is no wait and no retry. A lost ACK desynchronizes the seq | bug |
| 3 | `MDRProtocol.cpp` `serializeQueryEq` | Sends `56` alone. EQEBB_GET_PARAM needs the inquired type: `56 00` | bug |
| 4 | `MDRProtocol.cpp` `parseInboundPayload`, battery `08` branch | Reads charging from byte 4. In `23 08 LL CC TT` the charging byte is 3; byte 4 is the threshold | bug |
| 5 | `parseInboundPayload`, battery | Charging is true only for `01`. Value `03` (charged, on the cable) reads as "not charging" | unclear |
| 6 | `serializeQueryBattery` | Always sends type `00`, also when the device advertises only T1 `28` | unclear |
| 7 | `serializeNoiseMode`, `serializeAmbientLevel`, NC/ASM parser | "wind" mode is invented: ambient mode with level 0. The `17` message has no wind mode; the XM5 reduces wind noise automatically. Level 0 is outside the 1..20 range | bug |
| 8 | `serializeNoiseMode` | For `anc` and `off`, sends `AM = 00` and `LV = 00`. mos9527 sends the stored Focus on Voice and ambient level in every mode. Sending 0 may reset the stored ambient level on the device | unclear |
| 9 | `serializeNoiseMode`, `serializeQueryNoiseMode` | Inquired type `17` is hard-coded. The correct type depends on the advertised function (`6B` → `17`, `6D` → `19`, `68` → `15`). Gadgetbridge's code path for the XM5 selects `15` (see 5.2) | unclear |
| 10 | `serializeMultipoint` | Bytes `D8 D1 00 VV` match GsSetParamBoolean, but slot `D1` is assumed. The multipoint slot must be found by the capability subject `"MULTIPOINT_SETTING"` (5.13) | unclear |
| 11 | multipoint flow | FIXED_MESSAGE alerts are never enabled (`94 00 00`) and the confirmation `98 00 MT 01` is never sent. The device waits for an answer, so the toggle may never apply | bug |
| 12 | `main.cpp` init | `serializeQueryGeneralSetting` (`D6 D1`) exists but is never sent. The multipoint value is never read; status.json shows the default `true` | bug |
| 13 | `HeadphoneState` defaults | `dsee_extreme = true`, `multipoint = true`, `ear_detection = true`, `codec = "LDAC"` are written to status.json before (or without) any device reply | bug |
| 14 | `HeadphoneState::codec` | Always `"LDAC"`. The codec is never queried (`12 02`) and `13 02` / `15 02` are never parsed | bug |
| 15 | `parseInboundPayload` "compact" branches | Invented commands `10`..`14` collide with real T1 commands: `11` COMMON_RET_CAPABILITY, `13` COMMON_RET_STATUS (the codec reply), `15` is next to them. The 3-byte codec reply `13 02 CD` is skipped only because the fake branch needs 7 bytes; a `11` reply with 4 or more bytes is parsed as an NC/ASM change. These branches must go before the daemon sends COMMON requests | bug |
| 16 | `main.cpp` `onDataReceived` | T2 (`0E`) payloads go to the same table 1 parser. T2 command numbers overlap T1 (`23`, `37`, `47`, `F3`), so T2 replies can be misread as T1 state | bug |
| 17 | `serializeEqPreset` and EQ parser | Unknown preset IDs (`A1`..`A5`, `18`+) are shown as `off`. The device preset list (`50 00 01`) is never read | unclear |
| 18 | `serializeDsee` / `serializeQueryDsee` | Bytes match mos9527 (`E8 01 VV`, `E6 01`). Not gated on T1 `E2`; if the XM5 advertises `ED` instead, these requests are wrong | unclear |
| 19 | `serializeSpeakToChat`, `serializeEarDetection` | Bytes match mos9527 (`F8 0C OO 01`, `F8 01 VV`). No issue apart from missing gating | OK |
| 20 | `onConnected` | `device_name` comes from the BlueZ alias, not from MODEL_NAME (`04 01`) | unclear |

## 8. Open questions for a real device

Run `sony-ctl probe` (see the contract) on a real XM5 to answer these:

1. Which NC/ASM function does the XM5 advertise: T1 `6B` (type `17`), `68`
   (type `15`) or `6D` (type `19`)?
2. Does the XM5 advertise T1 `E2` or `ED` for DSEE Extreme, and which
   `UpscalingType` does `E1 01` return?
3. Which general setting slot carries `"MULTIPOINT_SETTING"`, and what is the
   exact alert (`99 00 MT AT`) after toggling it?
4. Does the XM5 advertise table 2 `30`, `32` or `33`, and `31`? What does
   `CS` / `PB` look like with two connected sources?
5. Does the XM5 advertise T1 `E1` or `E3` for connection quality?
6. Does the XM5 advertise T1 `20` or `28` for the battery?
7. Does the XM5 accept ambient level 0, or only 1..20?
8. Is the headset volume range 0..30 on the XM5?
9. Does the XM5 advertise `F3` or `FE`, and which keys and presets does the
   assignable capability list?
10. Does the XM5 answer the table 2 worn status `F2 00`?
11. Does the XM5 ACK the multipoint write `D8 Dn 00 VV` before it sends the
    alert `99 00 MT 01`, or does it hold the ACK until the host answers? The
    daemon keeps rule 4 of section 1 (one unacknowledged data packet): the
    answer `98 00 MT 01` waits until `D8` is ACKed or its retries run out.
    Sending it earlier needs a seq the rules do not define: seq `S` reads as
    a retransmit of `D8`, and seq `1 - S` is not yet expected. If the XM5
    holds the ACK, `sony-ctl multipoint` returns `ERR device timeout` after
    1.8 s, then the queued answer goes out and the toggle still applies. The
    `recent_frames` of `sony-ctl probe` show which order the XM5 uses.
