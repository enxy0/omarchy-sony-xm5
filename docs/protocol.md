# Sony XM5 MDR v2 protocol reference (WH-1000XM5, WF-1000XM5)

This document describes the Sony MDR protocol, version 2, as the WH-1000XM5
headphones and the WF-1000XM5 earbuds speak it over Bluetooth RFCOMM. It is
the byte-level reference for the daemon (`daemon/src/MDRProtocol.*`).
Sections 1 to 8 describe the WH-1000XM5; most of it applies to the
WF-1000XM5 too. Section 9 lists what is new or different on the WF-1000XM5.
The contract between the daemon and the bar widget is a separate document:
[`backend-contract.md`](backend-contract.md).

## Sources and confidence

All byte values come from these sources.

| Source | License | Used for |
|---|---|---|
| [mos9527/SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient) `libmdr/include/mdr/ProtocolV2.hpp`, `ProtocolV2T1.hpp`, `ProtocolV2T2.hpp`, `libmdr/src/HeadphonesV2*.cpp`, `libmdr/src/DetailsV2.hpp`, `libmdr/src/Command.cpp`, `libmdr/src/Headphones.cpp`, `client/Client.cpp` | MIT | Message layouts, enum values, init order, gating rules |
| mos9527 `docs/device-support/WH-1000XM5.md` | MIT | Which features users confirmed on a real XM5 |
| Gadgetbridge `SonyWH1000XM5Coordinator.java`, `SonyProtocolImplV2.java` | AGPL-3.0 | Facts only (feature list of the XM5, a few byte values). No code or text is copied |
| Capture from a WH-1000XM5, firmware 2.5.1, made with this daemon (init sequence, `sony-ctl probe`, `sony-ctl status`) | this project | What the device advertises and answers. Section 3.4 |
| Capture from a WF-1000XM5, firmware 6.1.0, made with this daemon (read-only init, `sony-ctl probe`, `sony-ctl status`) | this project | What the earbuds advertise and answer. Section 9.1 |
| mos9527 `docs/device-support/WF-1000XM5.md` and the packet captures in `tests/WF-1000XM5-6.1.0/` | MIT | Which features work on a real WF-1000XM5; replies this daemon did not request (L/R and case battery) |

Each feature table has a **Confidence** value:

- **confirmed on WH-1000XM5 fw 2.5.1**: the capture in section 3.4 shows the
  request and the reply. For a setter this confirms the read path only,
  unless the row says otherwise.
- **verified on XM5**: mos9527 `WH-1000XM5.md` marks the feature as working, or
  the Gadgetbridge XM5 coordinator enables it.
- **in code, unverified on XM5**: the layout exists in mos9527 (or Gadgetbridge
  for other models), but no source confirms it on an XM5.

Section 10 overrides these levels for setters: it lists every value this
daemon wrote to a real WH-1000XM5 and WF-1000XM5 and read back from a fresh
session after a Bluetooth reconnect.

Section 9 uses the same three levels for the earbuds: **confirmed on
WF-1000XM5 fw 6.1.0** (our capture, section 9.1), **verified on WF-1000XM5**
(mos9527 `WF-1000XM5.md` or its WF-1000XM5 packet captures), and **in code,
unverified on WF**.

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
| 26 | T2 | table 2 present and (T2 `F0` or T1 `F6`); mos9527 also sends it for T1 `F1`, which the XM5 does not answer (3.4) | `F2 00` | worn status |
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

### 3.4 Capture: WH-1000XM5, firmware 2.5.1

One full init sequence of this daemon against a real WH-1000XM5. Addresses
and names of paired devices are replaced. Every request got its ACK.

**Identity**

| Request | Reply | Meaning |
|---|---|---|
| T1 `00 00` | `01 00 03 00 20 16 00 00` | protocol version `03002016` (50339862); table 1 and table 2 supported |
| T1 `02 00` | `03 00 03 11 <17 bytes>` | capability counter `03`; the unique ID is the headset's own Bluetooth address |
| T1 `04 02` | `05 02 05 "2.5.1"` | firmware version |
| T1 `04 01` | `05 01 0A "WH-1000XM5"` | model name |
| T1 `04 03` | `05 03 30 03` | series `30` (PREMIUM), color `03` (SILVER) |

**Support function lists**

T1 `06 00` → `07 00 21 ...`, 33 IDs (priority in brackets, `FF` = none):

| ID | Name | ID | Name |
|---|---|---|---|
| `10` | CONCIERGE_DATA | `A1` | PLAYBACK_CONTROLLER_WITH_CALL_VOLUME_ADJUSTMENT (`0B`) |
| `12` | CODEC_INDICATOR | `C1` | ACTION_LOG_NOTIFIER |
| `13` | UPSCALING_INDICATOR | `C2` | TIME_SERIES_OPERATIONLOG_NOTIFIER |
| `14` | BLE_SETUP | `E1` | CONNECTION_MODE_SOUND_QUALITY_CONNECTION_QUALITY (`0D`) |
| `23` | POWER_OFF | `E2` | UPSCALING_AUTO_OFF (`0F`) |
| `25` | AUTO_POWER_OFF_WITH_WEARING_DETECTION (`21`) | `F4` | VOICE_ASSISTANT_SETTINGS (`14`) |
| `20` | BATTERY_LEVEL_INDICATOR | `93` | VOICE_ASSISTANT_ALERT_NOTIFICATION |
| `27` | TANDEM_KEEP_ALIVE | `69` | AMBIENT_SOUND_CONTROL_MODE_SELECT (`1A`) |
| `D1` | GENERAL_SETTING_1 (`19`) | `D2` | GENERAL_SETTING_2 (`12`) |
| `32` | FW_UPDATE_MTK_TRANSFER_WITHOUT_DISCONNECTION (`26`) | `F9` | RESET_SETTINGS (`27`) |
| `50` | PRESET_EQ (`0A`) | `B0` | SAR (`28`) |
| `6B` | MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT (`06`) | `B2` | GATT_CONNECTABLE (`2B`) |
| `70` | AUTO_NCASM (`01`) | `B6` | SAR_OPTIMIZATION_ACCEL_TYPE (`2A`) |
| `90` | FIXED_MESSAGE | `F1` | PLAYBACK_CONTROL_BY_WEARING_REMOVING_HEADPHONE_ON_OFF (`23`) |
| `FD` | QUICK_ACCESS (`1B`) | `FC` | SMART_TALKING_MODE_TYPE2 (`09`) |
| `B8` | INTEGRATED_AUTO_PLAY | `B7` | HEAD_TRACKER_ACCEL_TYPE |
| `19` | PHONE_AND_CONNECTED_DEVICE_INFOMATION_FOR_CLASSIC | | |

T2 `06 00` → `07 00 06 41 25 50 FF F2 FF 32 03 31 FF F8 1E`:

| ID | Name |
|---|---|
| `41` | VOICE_GUIDANCE_SETTING_MTK_TRANSFER_WITHOUT_DISCONNECTION_SUPPORT_LANGUAGE_SWITCH |
| `50` | SAFE_LISTENING_HBS_1 |
| `F2` | QUICK_ACCESS_EASY_SETTING |
| `32` | PAIRING_DEVICE_MANAGEMENT_WITH_BLUETOOTH_CLASS_OF_DEVICE_CLASSIC_BT |
| `31` | SOURCE_SWITCH_CONTROL |
| `F8` | LINK_AUTO_SWITCH_FOR_HEADSETS |

Not advertised: T1 `24`, `28`, `68`, `6D`, `E3`, `ED`, `F3`, `FE`, `F6`, and
T2 `30`, `33`, `40`, `42`..`45`, `F0`.

**Variants the daemon chose**

| Feature | Advertised | Variant |
|---|---|---|
| Battery | T1 `20` | type `00` |
| NC/ASM | T1 `6B` | type `17` |
| Auto power off | T1 `25` | type `05` |
| DSEE | T1 `E2` | type `01`; capability `E1 01 02` = DSEE_HX_AI (DSEE Extreme) |
| Connection quality | T1 `E1` | type `00` |
| General settings | T1 `D1`, `D2` | `D1` = `TOUCH_PANEL_SETTING`, `D2` = `MULTIPOINT_SETTING` |
| Device list | T2 `32` | type `02` |
| Voice guidance | T2 `41` | type `01` |

**Capability and value replies**

| Request | Reply | Meaning |
|---|---|---|
| T1 `D0 D1 01` | `D1 D1 00 01 13 "TOUCH_PANEL_SETTING" 00` | boolean, enum name, empty summary |
| T1 `D0 D2 01` | `D1 D2 00 01 12 "MULTIPOINT_SETTING" 29 "MULTIPOINT_SETTING_SUMMARY_LDAC_AVAILABLE"` | boolean, enum name |
| T1 `D6 D1` | `D7 D1 00 00` | touch panel on |
| T1 `D6 D2` | `D7 D2 00 00` | multipoint on |
| T1 `50 00 01` | `51 00 06 15 0C (PID 00) x 0C` | 6 bands, 21 steps, 12 presets: `00`, `10`..`17`, `A0`, `A1`, `A2`. Every preset name is an empty string |
| T1 `52 00` | `53 00 00` | EQ can be used |
| T1 `56 00` | `57 00 00 06 0A 0A 0A 0A 0A 0A` | preset `00` (off) **with** 6 band steps, all `0A` (level 0). See 5.3 |
| T1 `22 00` | `23 00 35 00` | battery 53 %, not charging |
| T1 `94 00 00` | ACK only | FIXED_MESSAGE alerts enabled |
| T1 `12 02` | `13 02 10` | codec LDAC |
| T1 `A6 20` | `A7 20 12` | volume 18 |
| T1 `66 17` | `67 17 01 00 00 01 14` | NC/ASM off, mode NC, Focus on Voice on, ambient level 20 |
| T2 `32 02` | `33 02 00 00` | pairing mode normal |
| T2 `36 02` | `37 02 03 (ADDR CS COD name) x 3 01` | three paired devices; one has `CS = 01` and COD `6C 01 04`; the two others have `CS = 00` and COD `FF FF FF`; `PB = 01`. Names are UTF-8 and can hold non-ASCII text |
| T2 `36 01` | `37 01 01` | automatic source switch on |
| T1 `F6 0C` | `F7 0C 01 01` | Speak-to-Chat off |
| T1 `FA 0C` | `FB 0C 00 01` | sensitivity auto, timeout standard |
| T1 `E6 00` | `E7 00 00` | connection quality: sound |
| T1 `E2 01` | `E3 01 00` | DSEE available |
| T1 `E6 01` | `E7 01 00` | DSEE off |
| T1 `66 30` | `67 30 01` | NC/AMB button cycles NC → Ambient → Off |
| T1 `26 05` | `27 05 10 00` | auto power off: when taken off |
| T1 `F6 01` | `F7 01 00` | pause when taken off: on |
| T2 `F2 00` | **no reply** (ACK only) | worn status is not answered; also not in the periodic sync. See 5.9 |
| T2 `46 01` | `47 01 00 01` | voice guidance on; the last byte is the language |
| T1 `C4 01 00` | ACK only | operation log enabled |

**Unsolicited packets after init** (the daemon does not parse them):

| Payload | Meaning (mos9527 enums) |
|---|---|
| T1 `15 03 02 00` | COMMON_NTFY_STATUS, inquired type `03` UPSCALING_EFFECT |
| T1 `45 01 00 02` | LEA group (`40`..`4F`), sent twice |

**Features the XM5 does not have**: voice guidance volume (no T2 `42` or
`43`; mos9527 also marks it ❌), button assignment (no T1 `F3` or `FE`, so
`button_function` is unsupported), worn status (no T2 `F0` or T1 `F6`).

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
| `70` | AUTO_NCASM | Adaptive Sound Control; not implemented (section 6) |
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
| `FD` | QUICK_ACCESS | Quick Access (double / triple tap) |
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
| `50` | SAFE_LISTENING_HBS_1 | sound pressure; not implemented (section 6) |
| `F0` | WEARING_STATUS_CHECKER | worn sensor |
| `F2` | QUICK_ACCESS_EASY_SETTING | Quick Access setup from the phone app; not implemented (section 6) |

## 5. Features

Each feature lists the gate, the requests, the reply layout and the value
enums. "Reply" covers both RET (answer to GET) and NTFY (push); they have the
same layout unless a row says otherwise.

### 5.1 Battery and charging

| Item | Value |
|---|---|
| Confidence | **confirmed on WH-1000XM5 fw 2.5.1** (T1 `20`, type `00`); also mos9527 "Battery Life", Gadgetbridge `BatterySingle` |
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
| Confidence | **confirmed on WH-1000XM5 fw 2.5.1**: the XM5 advertises T1 `6B`, so type `17`; GET `66 17` answers `67 17 ...`. Setters: **verified on XM5** (mos9527 "NC/AMB Settings", Gadgetbridge `AmbientSoundControl`), including ambient level and Focus on Voice |
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
| Confidence | reads: **confirmed on WH-1000XM5 fw 2.5.1** (capability with 6 bands, 21 steps, 12 presets; value reply with 6 band steps for preset `00`). Presets: **verified on XM5** (Gadgetbridge `EqualizerWithCustomBands`; mos9527 notes "no way to choose presets" in its UI). Custom bands and Clear Bass: **verified on XM5** (mos9527 "Equalizer", Gadgetbridge) |
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

The band steps describe the **active preset**, whatever its ID. The XM5
fw 2.5.1 answers `57 00 00 06 0A 0A 0A 0A 0A 0A` for preset `00` (off): six
steps, all level 0. mos9527 (`HandleEqEbbParamT1`) stores the band steps for
every preset ID, not only for `A0`. So a reply for a non-custom preset
carries that preset's curve; the stored custom curve cannot be read while
another preset is active.

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
| Confidence | reads: **confirmed on WH-1000XM5 fw 2.5.1** (T1 `E2` advertised, not `ED`; `E1 01 02` = DSEE_HX_AI; status and value answer). SET: **in code, unverified on XM5** (Gadgetbridge marks DSEE as TODO for the XM5) |
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
| Confidence | reads: **confirmed on WH-1000XM5 fw 2.5.1**. On/off: **verified on XM5** (Gadgetbridge `SpeakToChatEnabled`; mos9527 marks it untested). Sensitivity and timeout: **verified on XM5** (Gadgetbridge `SpeakToChatConfig`) |
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
| Confidence | on/off read: **confirmed on WH-1000XM5 fw 2.5.1** (T2 `41`, type `01`; reply `47 01 00 01`, the last byte is the language). On/off SET: **verified on XM5** (Gadgetbridge `VoiceNotifications`). Volume: **not supported on WH-1000XM5 fw 2.5.1** (no T2 `42` or `43`; mos9527 also marks "Voice Guidance Volume ❌") |
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
| Confidence | read: **confirmed on WH-1000XM5 fw 2.5.1** (T1 `25`, type `05`, `27 05 10 00`). SET: `off` and `when_taken_off` **round trip on WH-1000XM5 fw 2.5.1 and WF-1000XM5 fw 6.1.0** (section 10); the timers are refused, see below |
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

**Type `05` has no timers.** The WH-1000XM5 (fw 2.5.1) and the WF-1000XM5
(fw 6.1.0) ACK a timer write such as `28 05 01 01` (30 min) and answer
`29 05 10 00`: "when taken off" stays. Only `11` (off) and `10` (when taken
off) are kept. The daemon therefore offers `off` and `when_taken_off` for
type `05`, and the timers for type `04` only.

### 5.8 NC/AMB button: cycle and assignment

Two settings exist. The cycle setting chooses which modes the NC/AMB button
steps through. The assignment chooses what the button does at all.

**Cycle** (NC_AMB_TOGGLE):

| Item | Value |
|---|---|
| Confidence | read: **confirmed on WH-1000XM5 fw 2.5.1**. SET: **verified on XM5** (mos9527 "NC/AMB Button Setting") |
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
| Confidence | **not supported on WH-1000XM5 fw 2.5.1**: it advertises neither T1 `F3` nor `FE`. The layout is **in code, unverified** for other models |
| Gate | T1 `FE` → type `0E`; else T1 `F3` → type `03` |
| GET capability | T1 `F0 03` → `F1 03 NN (key) x NN`. For type `0E`: `F1 0E LT NN (key) x NN` |
| GET value | T1 `F6 03` → `F7 03 NN (preset) x NN` (NTFY `F9 03 ...`) |
| SET | T1 `F8 03 NN (preset) x NN` |

Each key in the capability is: `KEY TYPE DEFAULT NP (presetCapability) x NP`,
and each preset capability is `PRESET NA NC (ACTION FUNCTION) x NA
(ACTION DEFAULT NF (FUNCTION) x NF) x NC`: both counts come before both
lists (mos9527 `AssignableSettingsPresetCapability::Read`; confirmed by the
WF-1000XM5 capture in 9.3). `KEY` `04` = NC_AMB_KEY, `05` = NC_AMBIENT_KEY,
`02` = CUSTOM; `00` / `01` = left / right side (9.3).

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
| Confidence | read: **confirmed on WH-1000XM5 fw 2.5.1**. SET: **verified on XM5** (Gadgetbridge `PauseWhenTakenOff`) |
| Gate | T1 `F1` |
| GET | T1 `F6 01` → `F7 01 VV` (NTFY `F9 01 VV`) |
| SET | T1 `F8 01 VV` |
| `VV` | `OnOffSettingValue`: `00` on, `01` off |

**Worn status** (read only):

| Item | Value |
|---|---|
| Confidence | **not answered by WH-1000XM5 fw 2.5.1**: it ACKs `F2 00` and sends no reply, three times in a row. It advertises T1 `F1` but not T2 `F0` or T1 `F6`. Other models: **in code, unverified** |
| Gate | table 2 present and (T2 `F0` or T1 `F6`). mos9527 also accepts T1 `F1` alone; the XM5 capture shows that `F1` alone is not enough |
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
| Confidence | read: **confirmed on WH-1000XM5 fw 2.5.1** (T1 `A1`, `A7 20 12`). SET: **verified on XM5** (mos9527 "Volume") |
| Gate | any of T1 `A1`..`A4` |
| GET | T1 `A6 20` → `A7 20 VL` (NTFY `A9 20 VL`) |
| SET | T1 `A8 20 VL` |
| `VL` | 0..30 (mos9527 client slider range) |

### 5.11 Connection quality (sound vs connection)

| Item | Value |
|---|---|
| Confidence | read: **confirmed on WH-1000XM5 fw 2.5.1** (T1 `E1`, type `00`, `E7 00 00`). SET: **in code, unverified on XM5** |
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
| Confidence | **confirmed on WH-1000XM5 fw 2.5.1** (`13 02 10` = LDAC) |
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
| Confidence | read: **confirmed on WH-1000XM5 fw 2.5.1** (slot `D2`, summary `"MULTIPOINT_SETTING_SUMMARY_LDAC_AVAILABLE"`, value `D7 D2 00 00`). SET and the alert: **in code, unverified on XM5** (mos9527 "Multipoint Control ?"; Gadgetbridge lists it as TODO for the XM5) |
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

One change can ask more than once. Turning multipoint **on** on the
WF-1000XM5 (fw 6.1.0, LDAC in use) asks `99 00 07 01` and, after the answer,
`99 00 70 01` (CAUTION_FOR_ENABLING_2_DEVICES_CONNECTION_WITH_LDAC). With
`70` unanswered the earbuds keep multipoint off. The daemon answers every
`AT = 01` alert while the window opened by the write lasts, and each answer
extends the window. The WH-1000XM5 asks `07` only.

### 5.14 Paired devices, connected devices, switch source

| Item | Value |
|---|---|
| Confidence | list read: **confirmed on WH-1000XM5 fw 2.5.1** (T2 `32`, type `02`, three entries, one connected). Switch: **works on WH-1000XM5 fw 2.5.1 and WF-1000XM5 fw 6.1.0** (section 10) |
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
| `COD` | Bluetooth class of device, 3 bytes big-endian. The XM5 sends `FF FF FF` for devices that are not connected |
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
| Confidence | **confirmed on WH-1000XM5 fw 2.5.1** (section 3.4) |
| Gate | none |
| Requests | init steps 3, 4, 5 in section 3.1 |

### 5.16 Power off (extra, not in the contract)

| Item | Value |
|---|---|
| Confidence | **verified on XM5** (mos9527 "Power Off", Gadgetbridge `PowerOffFromPhone`) |
| Gate | T1 `23` POWER_OFF |
| SET | T1 `24 03 01` (POWER_SET_STATUS, POWER_OFF, USER_POWER_OFF) |

### 5.17 Touch panel on/off

The touch sensor control panel is a **general setting** slot, like
multipoint (5.13). Find the slot by its subject, never by its number.

| Item | Value |
|---|---|
| Confidence | read: **confirmed on WH-1000XM5 fw 2.5.1** (slot `D1`, `D7 D1 00 00` = on). SET: **verified on XM5** (mos9527 "Touch Sensor Enable/Disable ✅", which writes the generic GsSetParamBoolean); not yet sent by this project to a real XM5 |
| Gate | T1 `Dn` (n = 1..4) **and** the slot capability subject is `"TOUCH_PANEL_SETTING"` with `ST = 00` (boolean) |
| GET capability | T1 `D0 Dn 01` → `D1 Dn 00 01 13 "TOUCH_PANEL_SETTING" 00` |
| GET value | T1 `D6 Dn` → `D7 Dn 00 VV` (NTFY `D9 Dn 00 VV`) |
| SET | T1 `D8 Dn 00 VV` |
| `VV` | `GsSettingValue`: `00` on, `01` off |

No source describes a confirmation alert for this slot; the daemon does not
open the alert window for it.

### 5.18 Quick Access

Quick Access assigns a phone app to a double or triple press of a key. The
headset stores the assignment; the app runs on the phone.

| Item | Value |
|---|---|
| Confidence | reads: **confirmed on WH-1000XM5 fw 2.5.1** (capability and value below). Layout: mos9527 `SystemRetCapabilityQuickAccess`, `SystemRetParamQuickAccess`, `SystemSetParamQuickAccess` (no handler in mos9527 `libmdr/src`). SET: **verified on XM5** by Gadgetbridge (`QuickAccess` capability in its XM5 coordinator; facts only); not yet sent by this project to a real XM5 |
| Gate | T1 `FD` QUICK_ACCESS |
| GET capability | T1 `F0 0D` → `F1 0D KK KT NA (AC DF NF (FN) x NF) x NA` |
| GET value | T1 `F6 0D` → `F7 0D NN (FN) x NN` (NTFY `F9 0D NN (FN) x NN`) |
| SET | T1 `F8 0D NN (FN) x NN`, one function per action, in capability order |

| Byte | Meaning |
|---|---|
| `KK` | `QuickAccessKey`: `00` L/R key, `01` NC/AMB key, `02` fixed Quick Access key |
| `KT` | key `Type`: `00` touch sensor, `01` button, `02` face tap |
| `AC` | `Action`: `01` double tap, `02` triple tap (others exist in the enum) |
| `DF` | default function of this action |
| `FN` | Quick Access function. `00` off, `01` Spotify (both from Gadgetbridge, which only names these two). Other values have no name in any source; the contract shows them as `other` |

XM5 fw 2.5.1 capture:

| Request | Reply | Meaning |
|---|---|---|
| T1 `F0 0D` | `F1 0D 01 01 02 01 00 04 00 05 01 02 02 00 04 00 05 01 02` | NC/AMB key, a button, two actions. Double tap and triple tap both default to `00` and accept `00`, `05`, `01`, `02` |
| T1 `F6 0D` | `F7 0D 02 00 00` | double tap off, triple tap off |

Gadgetbridge sends `F8 0D 02 DD TT` (double, then triple), which matches the
capability order above. The daemon writes the full list and changes only the
requested action. Functions `02` and `05` are accepted by the XM5 but have no
known meaning, so the daemon does not offer them.

## 6. Features we skip

| Feature | Reason |
|---|---|
| Playback control and track metadata (T1 `A2`/`A4`, `A6 01`) | The desktop already controls playback through MPRIS and AVRCP. mos9527 reports the XM5 metadata notify as broken |
| Safe listening / sound pressure (T2 `50` SAFE_LISTENING_HBS_1; GET `5A 00` → `5B 00 LP EC`) | The XM5 advertises T2 `50`, and mos9527 reads `5A 00` in its periodic sync. But the reply has no stable meaning for us: the level `LP` ("levelPerPeriod") has no unit or period in any source, and `EC` (`SafeListeningErrorCause`) has no "valid" value (`00` not playing, `01` in call, `02` detached). The XM5 fw 2.5.1 answered `5B 00 00 03` twice, 30 s apart, while audio played: level 0 and cause `03`, which is outside the enum. The Sony app builds its Safe Listening data from timestamped log records (`SafeListeningData`, `logCapacity`) over an app session. **Confidence**: capture-based; skipped |
| Adaptive Sound Control (T1 `70` AUTO_NCASM) | The XM5 advertises T1 `70`. In mos9527 the matching command group is SENSE (`70`..`7B`): the phone sends `SENSE_SET_STATUS` (start / end setting) and `SENSE_SET_PARAM` with the NC/ASM values to apply; the headset only answers with NTFY. There is no GET for an on/off state or a sensitivity: the activity detection runs in the phone app. The only NC sensitivity in the protocol (`NoiseAdaptiveSensitivity`) belongs to NC/ASM type `19` (T1 `6D`), which the XM5 does not advertise. mos9527 has no implementation. **Confidence**: from the mos9527 message definitions; skipped |
| Quick Access easy setting (T2 `F2` QUICK_ACCESS_EASY_SETTING) | Only `SYSTEM_SET_PARAM` (GATT setting, preset list, function list) and an NTFY with OK / CANCELED exist. It is the phone app's one-step setup, with no GET. Quick Access itself is read and set through T1 `FD` (5.18) |
| 360 Reality Audio | Needs the Sony app account and ear photo analysis; no MDR setting is useful on a PC |
| Head gestures, listening modes (BGM, cinema) | Not requested for this widget |

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

### Confirmed on WH-1000XM5 fw 2.5.1

Answered by the capture in section 3.4:

| # | Question | Answer |
|---|---|---|
| 1 | Which NC/ASM function does the XM5 advertise: T1 `6B` (type `17`), `68` (type `15`) or `6D` (type `19`)? | T1 `6B`, type `17` |
| 2 | Does the XM5 advertise T1 `E2` or `ED` for DSEE Extreme, and which `UpscalingType` does `E1 01` return? | T1 `E2`; `E1 01 02` = DSEE_HX_AI |
| 3 | Which general setting slot carries `"MULTIPOINT_SETTING"`? | slot `D2`. Slot `D1` carries `"TOUCH_PANEL_SETTING"`. The alert after a toggle is still open (question 11) |
| 4 | Does the XM5 advertise table 2 `30`, `32` or `33`, and `31`? | T2 `32` and `31`. With one connected source: its `CS = 01`, `PB = 01`. Two connected sources are not captured yet |
| 5 | Does the XM5 advertise T1 `E1` or `E3` for connection quality? | T1 `E1` |
| 6 | Does the XM5 advertise T1 `20` or `28` for the battery? | T1 `20` |
| 9 | Does the XM5 advertise `F3` or `FE`? | neither. The XM5 has no button assignment in this protocol |
| 10 | Does the XM5 answer the table 2 worn status `F2 00`? | no. It ACKs and sends no reply |

### Still open

Run `sony-ctl probe` (see the contract) on a real XM5 to answer these:

7. Does the XM5 accept ambient level 0, or only 1..20?
8. Is the headset volume range 0..30 on the XM5? The capture shows 18.
11. Does the XM5 ACK the multipoint write `D8 Dn 00 VV` before it sends the
    alert `99 00 MT 01`, or does it hold the ACK until the host answers? The
    daemon keeps rule 4 of section 1 (one unacknowledged data packet): the
    answer `98 00 MT 01` waits until `D8` is ACKed or its retries run out.
    Sending it earlier needs a seq the rules do not define: seq `S` reads as
    a retransmit of `D8`, and seq `1 - S` is not yet expected. If the XM5
    holds the ACK, `sony-ctl multipoint` returns `ERR device timeout` after
    1.8 s, then the queued answer goes out and the toggle still applies. The
    `recent_frames` of `sony-ctl probe` show which order the XM5 uses.
12. What do `CS` and `PB` look like with two connected sources? The
    WF-1000XM5 capture (section 9.1) shows it for the earbuds: the two
    connected sources have `CS = 01` and `CS = 02`, and `PB = 01`. Not yet
    captured on the WH-1000XM5.

## 9. WF-1000XM5 earbuds

The WF-1000XM5 speaks the same MDR v2 protocol on the same RFCOMM service
(channel 9 in the capture). Framing, ACK rules, the init order (section 3)
and every feature layout in section 5 apply unchanged, unless this section
says otherwise. The features below are gated by the support function lists
exactly as on the WH-1000XM5; the daemon needs no model check to choose a
request.

### 9.1 Capture: WF-1000XM5, firmware 6.1.0

One read-only init sequence of this daemon (the WH-1000XM5 build) against a
real WF-1000XM5. Addresses and names are replaced. Every request got its
ACK.

**Identity**

| Request | Reply | Meaning |
|---|---|---|
| T1 `00 00` | `01 00 03 00 30 18 00 00` | protocol version `03003018` (50343960); table 1 and table 2 supported |
| T1 `02 00` | `03 00 0C 11 <17 bytes>` | capability counter `0C`; the unique ID is the earbuds' own Bluetooth address |
| T1 `04 02` | `05 02 05 "6.1.0"` | firmware version |
| T1 `04 01` | `05 01 0A "WF-1000XM5"` | model name |
| T1 `04 03` | `05 03 30 03` | series `30` (PREMIUM), color `03` |

The earbuds sent one unsolicited T1 `F5 10 01 00` before the protocol info
reply (see "Unsolicited packets" below). The receiver must accept data
packets before the first reply.

**Support function lists**

T1 `06 00` → `07 00 2E ...`, 46 IDs (priority in brackets, none = `FF`):

| ID | Name | ID | Name |
|---|---|---|---|
| `10` | CONCIERGE_DATA | `12` | CODEC_INDICATOR |
| `13` | UPSCALING_INDICATOR | `14` | BLE_SETUP |
| `15` | TUTORIAL_CONTENTS_SELECT_ON_CONCIERGE | `16` | CONNECTION_ESTABLISHED_TIME |
| `17` | UNNECESSARY_AUTO_RECONNECTION | `23` | POWER_OFF |
| `25` | AUTO_POWER_OFF_WITH_WEARING_DETECTION (`36`) | `11` | CONNECTION_STATUS |
| `27` | TANDEM_KEEP_ALIVE | `29` | LR_BATTERY_LEVEL_WITH_THRESHOLD |
| `2A` | CRADLE_BATTERY_LEVEL_WITH_THRESHOLD | `92` | FIXED_MESSAGE_WITH_LR_SELECTION |
| `50` | PRESET_EQ (`11`) | `6B` | MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT (`08`) |
| `70` | AUTO_NCASM (`01`) | `90` | FIXED_MESSAGE |
| `A1` | PLAYBACK_CONTROLLER_WITH_CALL_VOLUME_ADJUSTMENT (`12`) | `C1` | ACTION_LOG_NOTIFIER |
| `C2` | TIME_SERIES_OPERATIONLOG_NOTIFIER | `E2` | UPSCALING_AUTO_OFF (`1B`) |
| `F1` | PLAYBACK_CONTROL_BY_WEARING_REMOVING_HEADPHONE_ON_OFF (`38`) | `FC` | SMART_TALKING_MODE_TYPE2 (`0D`) |
| `93` | VOICE_ASSISTANT_ALERT_NOTIFICATION | `F6` | WEARING_STATUS_DETECTOR (`33`) |
| `F9` | RESET_SETTINGS (`41`) | `D1` | GENERAL_SETTING_1 (`3B`) |
| `94` | LE_AUDIO_ALERT_NOTIFICATION | `40` | TWS_SUPPORTS_A2DP_LEA_UNI_LEA_BROAD_WITH_CTKD (`2E`) |
| `42` | CLASSIC_ONLY_LE_CLASSIC_SETTING (`3A`) | `44` | CHANGE_TANDEM_CONNECTION_PROFILE_FOR_ANDROID |
| `FF` | HEAD_GESTURE_ON_OFF_TRAINING (`2D`) | `19` | PHONE_AND_CONNECTED_DEVICE_INFOMATION_FOR_CLASSIC |
| `F3` | ASSIGNABLE_SETTING (`26`) | `E7` | CONNECTION_MODE_CLASSIC_AUDIO_LE_AUDIO (`1A`) |
| `D2` | GENERAL_SETTING_2 (`20`) | `FD` | QUICK_ACCESS (`2B`) |
| `B6` | SAR_OPTIMIZATION_ACCEL_TYPE (`1D`) | `B0` | SAR (`42`) |
| `B2` | GATT_CONNECTABLE (`43`) | `B8` | INTEGRATED_AUTO_PLAY (`1F`) |
| `B7` | HEAD_TRACKER_ACCEL_TYPE (`15`) | `34` | FW_UPDATE_MTK_TRANSFER_WITHOUT_DISCONNECTION_AUTO_UPDATE (`3F`) |
| `F4` | VOICE_ASSISTANT_SETTINGS (`23`) | `F5` | VOICE_ASSISTANT_WAKE_WORD_ON_OFF (`24`) |

T2 `06 00` → `07 00 0B ...`, 11 IDs:

| ID | Name |
|---|---|
| `42` | VOICE_GUIDANCE_SETTING_MTK_TRANSFER_WITHOUT_DISCONNECTION_SUPPORT_LANGUAGE_SWITCH_AND_VOLUME_ADJUSTMENT (`3D`) |
| `53` | SAFE_LISTENING_TWS_2 |
| `54` | SAFE_VOLUME_CONTROL |
| `F1` | REPEAT_TAP_TRAINING_MODE |
| `60` | LE_AUDIO_CONNECTION_STATE_NOTIFICATION |
| `61` | LE_AUDIO_SWITCH_SUPPORTED_COMPATIBILITY |
| `F2` | QUICK_ACCESS_EASY_SETTING |
| `63` | GET_IDENTITY_RESOLVING_KEY |
| `F8` | LINK_AUTO_SWITCH_FOR_HEADSETS (`30`) |
| `32` | PAIRING_DEVICE_MANAGEMENT_WITH_BLUETOOTH_CLASS_OF_DEVICE_CLASSIC_BT (`04`) |
| `31` | SOURCE_SWITCH_CONTROL |

Not advertised: T1 `20`, `21`, `22`, `24`, `28`, `68`, `69`, `6D`, `E1`,
`E3`, `ED`, `FE`, and T2 `30`, `33`, `40`, `41`, `43`..`45`, `50`, `F0`.

**Variants the daemon chose**

| Feature | Advertised | Variant |
|---|---|---|
| Battery | T1 `29`, `2A` (not `20` or `28`) | the WH-1000XM5 daemon has no request for these; battery stayed unknown. See 9.2 |
| NC/ASM | T1 `6B` | type `17`, as on the WH-1000XM5 |
| Auto power off | T1 `25` | type `05` |
| DSEE | T1 `E2` | type `01`; capability `E1 01 02` = DSEE_HX_AI |
| Connection quality | T1 `E7` (not `E1` or `E3`) | none; see 9.5 |
| General settings | T1 `D1`, `D2` | `D1` = `SIDETONE_SETTING`, `D2` = `MULTIPOINT_SETTING`. No `TOUCH_PANEL_SETTING` slot |
| Button assignment | T1 `F3` | type `03`; per-side touch sensor keys, see 9.3 |
| Device list | T2 `32` | type `02` |
| Voice guidance | T2 `42` | type `01`, with volume (`46 20`) |

**Capability and value replies**

| Request | Reply | Meaning |
|---|---|---|
| T1 `D0 D1 01` | `D1 D1 00 01 10 "SIDETONE_SETTING" 18 "SIDETONE_SETTING_SUMMARY"` | boolean, enum name |
| T1 `D0 D2 01` | `D1 D2 00 01 12 "MULTIPOINT_SETTING" 29 "MULTIPOINT_SETTING_SUMMARY_LDAC_AVAILABLE"` | boolean, enum name |
| T1 `E0 01` | `E1 01 02` | DSEE_HX_AI |
| T1 `F0 03` | `F1 03 02 ...` (79 bytes) | two touch sensor keys, left and right. Full bytes and decoding in 9.3 |
| T1 `50 00 01` | `51 00 06 15 0C (PID 00) x 0C` | 6 bands, 21 steps, 12 presets: `00`, `10`..`17`, `A0`, `A1`, `A2`; empty names. Same as the WH-1000XM5 |
| T1 `F0 0D` | `F1 0D 00 00 02 01 00 00 02 00 00` | Quick Access: L/R key, touch sensor, double tap and triple tap, each with default `00` and **no** selectable function. See 9.6 |
| T1 `94 00 00` | ACK only | FIXED_MESSAGE alerts enabled |
| T1 `D6 D1` | `D7 D1 00 00` | sidetone on |
| T1 `D6 D2` | `D7 D2 00 00` | multipoint on |
| T1 `12 02` | `13 02 10` | codec LDAC |
| T1 `A6 20` | `A7 20 12` | volume 18 |
| T1 `66 17` | `67 17 01 00 00 00 14` | NC/ASM off, mode NC, Focus on Voice off, ambient level 20 |
| T2 `32 02` | `33 02 00 00` | pairing mode normal |
| T2 `36 02` | `37 02 04 (ADDR CS COD name) x 4 01` | four paired devices. This computer: `CS = 01`, COD `6C 01 04`. A phone: `CS = 02`, COD `5A 42 0C`. Two others: `CS = 00`, COD `FF FF FF`. `PB = 01`: this computer plays audio |
| T2 `36 01` | `37 01 01` | automatic source switch on |
| T1 `F6 0C` | `F7 0C 01 01` | Speak-to-Chat off |
| T1 `FA 0C` | `FB 0C 00 01` | sensitivity auto, timeout standard |
| T1 `52 00` | `53 00 00` | EQ can be used |
| T1 `56 00` | `57 00 00 06 0A 0A 0A 0A 0A 0A` | preset off, six band steps at level 0 |
| T1 `E2 01` | `E3 01 00` | DSEE available |
| T1 `E6 01` | `E7 01 00` | DSEE off |
| T1 `F6 03` | `F7 03 02 20 20` | left and right touch sensor: both PLAYBACK_CONTROL |
| T1 `26 05` | `27 05 10 00` | auto power off: when taken off |
| T1 `F6 01` | `F7 01 00` | pause when taken off: on |
| T2 `F2 00` | `F3 00 00` | worn: both earbuds in the ears. The WH-1000XM5 does not answer this request |
| T2 `46 01` | `47 01 00 01` | voice guidance on; the last byte is the language |
| T2 `46 20` | `47 20 00` | voice guidance volume 0 |
| T1 `F6 0D` | `F7 0D 02 00 00` | Quick Access double tap off, triple tap off |
| T1 `C4 01 00` | ACK only | operation log enabled |

**Unsolicited packets**

| Payload | Meaning (mos9527 enums) |
|---|---|
| T1 `C9 01 0B "lUnitRemove" 00 00`, `C9 01 0B "rUnitRemove" 00 00`, `C9 01 09 "rUnitWear" 00 00`, `C9 01 09 "lUnitWear" 00 00` | operation log lines when an earbud is taken out or put in. The first letter is the side (`l`, `r`), and `U` is upper case. The WH-1000XM5 sends `"unitWear"` / `"unitRemove"` |
| T1 `F5 10 01 00`, `F5 10 01 01` | SYSTEM_NTFY_STATUS, inquired type `10` HEAD_GESTURE_TRAINING: `01` = OUT_OF_TEST_MODE, then `EnableDisable`. It flips together with the wear log lines. Ignore it |

### 9.2 Battery: left, right and case

The earbuds report one level per earbud and one for the charging case.
There is no single battery value (no T1 `20` or `28`).

| Item | Value |
|---|---|
| Confidence | **verified on WF-1000XM5** (mos9527 "Battery Life ✅"; replies below from its fw 6.1.0 packet capture). Not yet requested by this daemon |
| Gate L/R | T1 `29` LR_BATTERY_LEVEL_WITH_THRESHOLD → type `09`; else T1 `21` LEFT_RIGHT_BATTERY_LEVEL_INDICATOR → type `01` |
| Gate case | T1 `2A` CRADLE_BATTERY_LEVEL_WITH_THRESHOLD → type `0A`; else T1 `22` CRADLE_BATTERY_LEVEL_INDICATOR → type `02` |
| GET | T1 `22 09` (or `22 01`); T1 `22 0A` (or `22 02`) |
| RET L/R | `23 09 LL LC RL RC LT RT` (type `01`: `23 01 LL LC RL RC`) |
| RET case | `23 0A CL CC CT` (type `02`: `23 02 CL CC`) |
| NTFY | `25` with the same layout as the RET |
| SET | none |

| Byte | Meaning |
|---|---|
| `LL`, `RL`, `CL` | level 0..100 of the left earbud, right earbud, case |
| `LC`, `RC`, `CC` | `BatteryChargingStatus` (5.1): `00` not charging, `01` charging, `02` unknown, `03` charged |
| `LT`, `RT`, `CT` | threshold of the left earbud, right earbud, case. Not used |

mos9527 WF-1000XM5 fw 6.1.0 capture:

| Request | Reply | Meaning |
|---|---|---|
| T1 `22 09` | `23 09 00 00 64 00 64 64` | left 0 %, not charging; right 100 %, not charging; thresholds 100 |
| T1 `22 0A` | `23 0A 35 00 1E` | case 53 %, not charging, threshold 30 |

The capture does not say why the left earbud reads 0 %. It may have been in
the case or switched off. See 9.10, question 1.

Re-read L/R and case battery in the periodic sync (3.3), as for T1 `20`.

### 9.3 Touch sensor function per earbud (ASSIGNABLE_SETTING)

The WF-1000XM5 has a touch sensor on each earbud. The user chooses one
**preset** per side; the preset decides what a tap, double tap, triple tap
and hold do. This is the same message as 5.8 "Assignment", with two keys.

| Item | Value |
|---|---|
| Confidence | capability and value read: **confirmed on WF-1000XM5 fw 6.1.0**. SET: **verified on WF-1000XM5** (mos9527 "Touch Sensor Gesture ✅"; its capture reads `F7 03 02 35 20`); not yet sent by this project |
| Gate | T1 `F3` → type `03` (T1 `FE` → type `0E`, layout below with `LT`) |
| GET capability | T1 `F0 03` → `F1 03 NK (key) x NK` (type `0E`: `F1 0E LT NK (key) x NK`) |
| GET value | T1 `F6 03` → `F7 03 NN (PRESET) x NN` (NTFY `F9 03 NN (PRESET) x NN`) |
| SET | T1 `F8 03 NN (PRESET) x NN`: one preset per key, in capability key order. Always send the full list |

The section 5.8 key layout was incomplete. The exact layout (mos9527
`AssignableSettingsPresetCapability::Read`) is:

```
key    = KEY TYPE DEFAULT NP (preset) x NP
preset = PRESET NA NC (ACTION FUNCTION) x NA (ACTION DEFAULT NF (FUNCTION) x NF) x NC
```

`NA` and `NC` both come **before** the two lists. `NA` = fixed actions,
`NC` = customizable actions (the user picks one of `NF` functions). The
daemon's parser at the time of the capture rejected this reply ("unhandled
payload"), so `sony-ctl probe` showed `assignable.type: null`. It must
parse this layout.

| Field | Values (mos9527 `t1` enums) |
|---|---|
| `KEY` (`Key`) | `00` LEFT_SIDE, `01` RIGHT_SIDE, `02` CUSTOM, `03` C, `04` NC_AMB_KEY, `05` NC_AMBIENT_KEY |
| `TYPE` | `00` touch sensor, `01` button |
| `ACTION` (`Action`) | `00` single tap, `01` double tap, `02` triple tap, `03` repeat tap, `10` single tap and hold, `11` double tap and hold, `21` long press then activate, `22` long press during activate |
| `FUNCTION` (`Function`) | `00` none, `01` NC → Ambient → Off, `02` NC → Ambient, `03` NC → Off, `04` Ambient → Off, `10` Quick Attention, `20` play/pause, `21` next track, `22` previous track, `23` volume up, `24` volume down, `43` Quick Access 1, `44` Quick Access 2 (others in the enum) |
| `PRESET` (`Preset`) | see the table below |

Capture (fw 6.1.0), one line per key:

```
F1 03 02
00 00 35 03  35 04 01 01 43 02 44 22 10 03 24 00 02 04 01 02 03 04  20 04 00 00 20 01 21 02 22 03 24  FF 01 00 00 00
01 00 20 03  35 04 01 01 43 02 44 22 10 03 23 00 02 04 01 02 03 04  20 04 00 00 20 01 21 02 22 03 23  FF 01 00 00 00
```

Decoded:

| Key | Type | Default | Preset | Fixed actions | Customizable actions |
|---|---|---|---|---|---|
| `00` left | touch | `35` | `35` | double tap → `43`, triple tap → `44`, long press during activate → `10`, repeat tap → `24` | single tap: default `02`, one of `01`, `02`, `03`, `04` |
| | | | `20` | single → `20`, double → `21`, triple → `22`, repeat tap → `24` | none |
| | | | `FF` | single → `00` | none |
| `01` right | touch | `20` | `35` | same as left, but repeat tap → `23` | same as left |
| | | | `20` | single → `20`, double → `21`, triple → `22`, repeat tap → `23` | none |
| | | | `FF` | single → `00` | none |

So each earbud accepts three presets: Ambient Sound Control (with Quick
Access on double and triple tap), Playback Control, and none. Repeat tap
changes the volume (left down, right up).

Preset IDs and contract values (contract `touch_function_left` /
`touch_function_right`):

| `PRESET` | mos9527 name | Contract value |
|---|---|---|
| `00` | AMBIENT_SOUND_CONTROL | `ambient_sound_control` |
| `35` | AMBIENT_SOUND_CONTROL_QUICK_ACCESS | `ambient_sound_control` |
| `10` | VOLUME_CONTROL | `volume_control` |
| `20` | PLAYBACK_CONTROL | `playback_control` |
| `30` | VOICE_RECOGNITION | `voice_recognition` |
| `31` | GOOGLE_ASSIST | `google_assistant` |
| `32` | AMAZON_ALEXA | `amazon_alexa` |
| `36` | QUICK_ACCESS | `quick_access` |
| `FF` | NO_FUNCTION | `none` |
| other | | `other` |

Two IDs map to `ambient_sound_control`. For a SET, the daemon sends the ID
that the capability lists for that key; if a key lists both, it sends `35`.

SET example, left to Ambient Sound Control, right kept at Playback Control:
`F8 03 02 35 20`. The device answers with NTFY `F9 03 02 35 20`; read `F6 03`
again if no NTFY arrives.

The customizable single-tap function of preset `35` (which NC/ASM modes a
single tap cycles through) has no known GET or SET in mos9527. It is not in
the contract.

### 9.4 Wearing status per earbud

| Item | Value |
|---|---|
| Confidence | read: **confirmed on WF-1000XM5 fw 6.1.0** (`F3 00 00`). Per-side codes `02`, `03`: **in code, unverified on WF** (mos9527 `WearingStatusCode`) |
| Gate | as 5.9: table 2 present and (T2 `F0` or T1 `F6`). The WF-1000XM5 advertises T1 `F6`, not T2 `F0` |
| GET | T2 `F2 00` → `F3 00 WS` (NTFY `F5 00 WS`) |

| `WS` | Meaning | `worn` | `worn_left` | `worn_right` |
|---|---|---|---|---|
| `00` | both earbuds in the ears | `true` | `true` | `true` |
| `01` | sensor error | `null` | `null` | `null` |
| `02` | left earbud not worn | `true` | `false` | `true` |
| `03` | right earbud not worn | `true` | `true` | `false` |
| `04` | neither earbud worn | `false` | `false` | `false` |

T1 `F6` WEARING_STATUS_DETECTOR is the earpiece fit test (mos9527
`SystemRetParamWearingStatusDetector`: fitting results, earpiece series and
size), not the wear sensor. It only satisfies the gate; the daemon never
sends its requests.

Re-read trigger: the earbuds send the log lines `"lUnitWear"`,
`"rUnitWear"`, `"lUnitRemove"`, `"rUnitRemove"` (9.1). The daemon's match
for `"unitWear"` / `"unitRemove"` must ignore case, else it misses them.

### 9.5 Connection mode: classic audio / LE audio

| Item | Value |
|---|---|
| Confidence | **in code, unverified on WF**. The daemon did not send these requests. mos9527 has the message layouts but no handler and no names for `V1`, `V2` |
| Gate | T1 `E7` CONNECTION_MODE_CLASSIC_AUDIO_LE_AUDIO → audio inquired type `05` |
| GET capability | T1 `E0 05` → `E1 05 NP (PM) x NP NL (LX) x NL`: accepted `PriorMode` values, then LDAC exclusive features (`00` GATT_CONNECTABLE) |
| GET status | T1 `E2 05` → `E3 05 V1 V2` (NTFY `E5 05 V1 V2`), both `EnableDisable` |
| SET | T1 `E8 05 PM V2` (`PriorMode` as in 5.11, then `EnableDisable`) |

The WF-1000XM5 advertises T1 `E7` instead of `E1` / `E3`, so
`connection_quality` (5.11) is unsupported on it. The Sony app's "sound
quality / stable connection" choice for the earbuds probably lives in the
`PM` byte of this message, and LE audio on/off in `V1` or `V2`. No source
confirms either, and mos9527 has no GET_PARAM reply layout for type `05`.
The connection from this computer is always classic Bluetooth (RFCOMM, LDAC
in the capture). The contract therefore does not expose this message yet.
`sony-ctl probe` should include the raw `E1 05` and `E3 05` replies so a
device run can name the bytes (9.10, question 3).

### 9.6 Quick Access on the WF-1000XM5

The WF-1000XM5 advertises T1 `FD`, but its capability lists no selectable
function: `F1 0D 00 00 02 01 00 00 02 00 00` = key `00` (L/R key), type `00`
(touch sensor), two actions (double tap, triple tap), each with default
`00` and `NF = 00`. The value reply `F7 0D 02 00 00` reads off / off.

On the earbuds, Quick Access is part of the touch preset `35` (double tap →
`43` Quick Access 1, triple tap → `44` Quick Access 2, 9.3); the phone app
assigns the app through T2 `F2` QUICK_ACCESS_EASY_SETTING (section 6). The
contract hides Quick Access when every action in the capability has
`NF = 00`.

**Confidence**: **confirmed on WF-1000XM5 fw 6.1.0** (capability and value
read).

### 9.7 Sidetone (capture voice during a call)

Sidetone plays the user's own voice in the earbuds during a phone call. It
is a general setting slot (5.13), found by its subject.

| Item | Value |
|---|---|
| Confidence | read: **confirmed on WF-1000XM5 fw 6.1.0** (slot `D1`, `D7 D1 00 00` = on). SET: **verified on WF-1000XM5** (mos9527 "Capture Voice During a Phone Call ✅", generic GsSetParamBoolean); not yet sent by this project |
| Gate | T1 `Dn` (n = 1..4) **and** the slot capability subject is `"SIDETONE_SETTING"` with `ST = 00` (boolean) |
| GET capability | T1 `D0 Dn 01` → `D1 Dn 00 01 10 "SIDETONE_SETTING" 18 "SIDETONE_SETTING_SUMMARY"` |
| GET value | T1 `D6 Dn` → `D7 Dn 00 VV` (NTFY `D9 Dn 00 VV`) |
| SET | T1 `D8 Dn 00 VV` |
| `VV` | `GsSettingValue`: `00` on, `01` off |

No source describes a confirmation alert for this slot.

### 9.8 Existing features on the WF-1000XM5

| Feature (section) | On the WF-1000XM5 | Confidence |
|---|---|---|
| Battery (5.1) | no T1 `20` / `28`; use 9.2 | verified on WF-1000XM5 |
| NC/ASM (5.2) | same, type `17` | read confirmed on WF-1000XM5 fw 6.1.0; setters verified on WF-1000XM5 (mos9527 "NC/AMB Settings") |
| EQ (5.3) | same capability and value layout, presets incl. `user1`, `user2` | read confirmed on WF-1000XM5 fw 6.1.0; SET verified on WF-1000XM5 (mos9527 "Equalizer") |
| DSEE (5.4) | same, DSEE_HX_AI | read confirmed on WF-1000XM5 fw 6.1.0 |
| Speak-to-Chat (5.5) | same | read confirmed on WF-1000XM5 fw 6.1.0; verified on WF-1000XM5 (mos9527 "Speak to Chat") |
| Voice guidance (5.6) | T2 `42`: on/off **and** volume (`47 20 00`) | read confirmed on WF-1000XM5 fw 6.1.0; volume SET verified on WF-1000XM5 (mos9527 "Voice Guidance Volume") |
| Auto power off (5.7) | same, type `05` | read confirmed on WF-1000XM5 fw 6.1.0 |
| NC/AMB button cycle (5.8) | no T1 `69`; unsupported | confirmed on WF-1000XM5 fw 6.1.0 |
| Button assignment (5.8) | no NC/AMB key; per-side touch keys instead (9.3). `button_function` stays unsupported | confirmed on WF-1000XM5 fw 6.1.0 |
| Pause when taken off (5.9) | same | read confirmed on WF-1000XM5 fw 6.1.0 |
| Worn status (5.9) | answered, per side (9.4) | confirmed on WF-1000XM5 fw 6.1.0 |
| Volume (5.10) | same | read confirmed on WF-1000XM5 fw 6.1.0; SET verified on WF-1000XM5 (mos9527 "Volume") |
| Connection quality (5.11) | no T1 `E1` / `E3`; see 9.5 | confirmed on WF-1000XM5 fw 6.1.0 |
| Codec (5.12) | same | confirmed on WF-1000XM5 fw 6.1.0 |
| Multipoint (5.13) | same, slot `D2` | read confirmed on WF-1000XM5 fw 6.1.0 |
| Devices, source switch (5.14) | same, T2 `32` and `31` | list read confirmed on WF-1000XM5 fw 6.1.0; switch verified on WF-1000XM5 (mos9527 "Multipoint Control (device change)") |
| Power off (5.16) | T1 `23` | verified on WF-1000XM5 (mos9527 "Power Off") |
| Touch panel on/off (5.17) | no `TOUCH_PANEL_SETTING` slot; unsupported | confirmed on WF-1000XM5 fw 6.1.0 |
| Quick Access (5.18) | advertised, but no selectable function (9.6) | confirmed on WF-1000XM5 fw 6.1.0 |

### 9.9 WF-1000XM5 features we skip

| Feature | Reason |
|---|---|
| Safe listening (T2 `53` SAFE_LISTENING_TWS_2; GET `5A 03` → `5B 03 LP EC`) | Same layout and same problem as SAFE_LISTENING_HBS_1 in section 6: `LP` has no unit or period in any source, and `EC` has no "valid" value. The mos9527 WF-1000XM5 capture answers `5B 03 FF 00` (level `FF`, cause "not playing"). No defined readable meaning. **Confidence**: from the mos9527 capture; skipped |
| Safe volume control (T2 `54`) | mos9527 names the inquired type (`04`) but has no message layout for it |
| LE audio state and compatibility (T2 `60`, `61`, `63`; T1 `40`, `42`, `44`, `94`) | Notifications about the LE audio link to the phone. The link to this computer is classic Bluetooth |
| Connection mode classic / LE audio (T1 `E7`) | Byte meanings unknown (9.5). Probe only |
| Earpiece fit test (T1 `F6`), repeat tap training (T2 `F1`), head gestures (T1 `FF`) | Phone app procedures with no lasting state that a bar widget should show |
| Fixed message with L/R selection (T1 `92`) | An alert variant that names a side. No contract feature opens such an alert. The daemon answers it like `99` only if a device run shows that a SET triggers it |
| Adaptive Sound Control (T1 `70`) | As section 6 |

### 9.10 Open questions for a real WF-1000XM5

1. What does `23 09` report for an earbud that is in the closed case or out
   of range: level `00` (as in the mos9527 capture), or a charging status
   `02` (unknown)? The contract writes the reported values; the UI rule
   for the bar battery depends on the answer. Partly answered by 9.11: an
   earbud that is out of the ear (log line `"rUnitRemove"`, worn code `03`)
   read level `00` with charging status `00`, not `02`. That earbud was
   out of the ear and not in the case. The closed-case case is still open.
2. Does `23 0A` (case) answer while both earbuds are out of the case and
   the lid is closed, and is the value then stale?
3. What do `E1 05` and `E3 05 V1 V2` (9.5) return with LE audio on and off,
   and with sound quality versus stable connection selected in the Sony
   app? Partly answered by 9.11 (one setting only).
4. Does `F8 03 02 LL RR` apply at once and answer with NTFY `F9 03`?
5. Does `F8 03` accept preset `00` (plain AMBIENT_SOUND_CONTROL), or only
   the IDs the capability lists?
6. Does the WF-1000XM5 send NTFY `25 09` / `25 0A` when a level changes, or
   only when the charging status changes?
7. Does `F3 00` ever report `02` or `03` (one earbud out), or does it report
   `00` while at least one earbud is worn? Answered by 9.11: it reports
   `03` with only the left earbud in the ear.

### 9.11 Capture: WF-1000XM5 fw 6.1.0, schema 3 daemon (read-only)

One init sequence of the schema 3 daemon against the same earbuds as 9.1,
read requests only. The left earbud was in the ear, the right one was not.

| Request | Reply | Meaning |
|---|---|---|
| T1 `22 09` | `23 09 61 00 00 00 64 64` | left 97 %, not charging; right 0 %, not charging; thresholds 100 |
| T1 `22 0A` | `23 0A 57 00 1E` | case 87 %, not charging, threshold 30 |
| T2 `F2 00` | `F3 00 03` | right earbud not worn: `worn` true, `worn_left` true, `worn_right` false |
| T1 `E0 05` | `E1 05 03 00 01 02 00` | `NP = 03`: `PriorMode` `00`, `01`, `02` accepted; `NL = 00` |
| T1 `E2 05` | `E3 05 00 00` | `V1 = 00`, `V2 = 00` (both ENABLE). The Sony app setting at the time is not known |
| (unsolicited) | T1 `C9 01 0B "rUnitRemove" 00 00` | operation log line; the daemon matches it without case and reads `F2 00` again (9.4) |

The corrected assignable layout (9.3) parsed the capability; the touch
function of both earbuds read `20` (playback control). Every request got
its ACK.

The right earbud was out of the ear and not in the case, and it read level
`00` with status `00`. The daemon therefore reads level `00` with status
`00` as "level unknown" and writes `battery_right: null`
(backend-contract.md 3.3); the charging status is written as reported.

The daemon finds connected headsets by polling BlueZ: every 2 s it runs a
blocking `GetManagedObjects` on the system bus (backend-contract.md 7,
"Known limitation").

## 10. Round trip on real hardware

Each value was written with `sony-ctl`, the headset was disconnected and
connected again with `bluetoothctl`, and the value was read from the new
session's init. Then every setting was written back to its original value
and checked the same way. Devices: WH-1000XM5 fw 2.5.1 and WF-1000XM5
fw 6.1.0, both connected to the computer at the same time, LDAC in use.

| Setting | Values written | WH-1000XM5 | WF-1000XM5 |
|---|---|---|---|
| Noise mode | `ambient`, `off` | kept | kept |
| Ambient level | 12, 20 | kept | kept |
| Focus on Voice | off/on | kept | kept |
| EQ preset | `bright`, `off` | kept | kept |
| Custom EQ and Clear Bass | `3 -2 0 4 -5`, CB 6 (WH); `2 0 -3 1 4`, CB -4 (WF); zeros | kept, preset reads `custom` | kept |
| DSEE Extreme | on/off | kept | kept |
| Speak-to-Chat | on/off; `high long`, `auto standard` | kept | kept |
| Voice guidance | off/on | kept | kept |
| Voice guidance volume | 2, 0 | not supported | kept |
| Auto power off | `off`, `when_taken_off` | kept | kept |
| Auto power off timers | `30min`, `5min` | refused (5.7) | refused (5.7) |
| NC/AMB button | `nc_ambient`, `nc_ambient_off` | kept | not supported |
| Pause when taken off | off/on | kept | kept |
| Volume | 10 and back | kept | kept |
| Connection quality | `connection`, `sound` | kept; the codec changed LDAC → AAC → LDAC | not supported |
| Touch panel | off/on | kept | not supported |
| Quick Access | `spotify` for double and triple, `off` | kept | not supported |
| Touch function L/R | `ambient_sound_control` / `none`, `playback_control` | not supported | kept |
| Sidetone | off/on | not supported | kept |
| Multipoint | off/on | kept | off kept; on needed the second alert (5.13) |
| Audio source (5.14) | the phone, then the computer (WH); the computer, then the phone (WF) | `active` moved each time | `active` moved each time |

The source switch was checked with a phone connected to both headsets next
to the computer; `active` was read from the device list about 4 s after
each switch, without a reconnect.

Not tested here: the NC/AMB button function on models with an assignable
button, and the paired device actions (disconnect, unpair).
