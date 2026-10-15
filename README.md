# Sony WH-1000XM5 for Omarchy

An Omarchy bar widget, a background daemon and a command-line tool
(`sony-ctl`) to control the Sony WH-1000XM5 headset on Linux.

The daemon talks to the headset over Bluetooth RFCOMM with the Sony MDR
protocol, version 2. The widget and `sony-ctl` talk only to the daemon.

> Screenshot: not available yet.

This is an unofficial project. It is not affiliated with, authorized,
maintained, sponsored, or endorsed by Sony Corporation or any of its
subsidiaries. "Sony", "WH-1000XM5", and related marks are registered
trademarks of Sony Corporation.

## Credits

- This project started as a fork of
  [andROYdified/omarchy-sony](https://github.com/andROYdified/omarchy-sony)
  (MIT, Copyright (c) 2026 Roy Kevin De Jesus). Most of the code is now
  rewritten: the init sequence, the ACK and sequence handling, feature
  detection from the headset's support list, the status file (schema 2),
  the new features and the panel.
- The protocol knowledge comes from
  [mos9527/SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient)
  (MIT). The function name tables in `daemon/src/MDRProtocol.cpp` are taken
  from it.
- Some facts (the XM5 feature list and a few byte values) come from
  [Gadgetbridge](https://codeberg.org/Freeyourgadget/Gadgetbridge). No
  Gadgetbridge code or text is used.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the license texts.

## Features

The daemon asks the headset which features it supports. The widget shows
only those controls. If the headset does not answer, the daemon uses a
fixed XM5 list (the first group below).

All byte values come from other projects. This project has not yet
confirmed them with captures from a real XM5. The groups below follow the
**Confidence** values in [docs/protocol.md](docs/protocol.md).

### Verified on an XM5 by other projects

mos9527/SonyHeadphonesClient users or the Gadgetbridge XM5 support confirm
these features:

- Battery level and charging state
- Noise control: noise cancelling, ambient sound, off
- Ambient sound level (1 to 20) and Focus on Voice
- Equalizer presets
- Custom 5-band equalizer and Clear Bass
- Speak-to-Chat on/off, sensitivity and timeout
- Voice guidance on/off
- Auto power off (timers and "when taken off")
- NC/AMB button cycle (which modes the button steps through)
- Pause when taken off
- Headset volume (0 to 30)

### Implemented, needs confirmation on a real XM5

The code for these features exists, but no source confirms them on an XM5.
They can fail or be absent:

- DSEE Extreme
- Voice guidance volume
- NC/AMB button function (ambient control, assistants, Quick Access)
- Quick Access function for a double or triple press (off or Spotify)
- Touch controls on/off
- Worn status (headset on the head or not)
- Connection priority (sound quality or stable connection)
- Current codec
- Multipoint on/off
- Paired device list and switching the active audio source
- Model name and firmware version

### Widget

- The bar shows a headphone icon, the same size as the built-in icon
  widgets. The icon turns green while charging and red below 20 %. The
  tooltip shows the battery level.
- Left click opens the panel. Right click cycles the noise mode
  (ANC, Ambient, Off).
- Keyboard in the panel: `j`/`k` or arrows to move, `h`/`l` to change a
  value or to open and close a section, `Enter` or `Space` to select,
  `Esc` to close.

## How it works

```
Panel.qml / Service.qml / Model.js  (Omarchy bar widget)
    |  reads                     |  runs
    v                            v
status.json                  sony-ctl
    ^                            |
    |  writes                    |  UNIX socket
    |                            v
sony-headphones-daemon  <--------+
    |
    |  Bluetooth RFCOMM, MDR protocol v2
    v
WH-1000XM5
```

| Item | Path |
|---|---|
| Status file | `$XDG_STATE_HOME/sony-headphones/status.json`, else `~/.local/state/sony-headphones/status.json` |
| Daemon socket | `$XDG_RUNTIME_DIR/sony-headphones.sock` |

Documentation:

- [docs/protocol.md](docs/protocol.md): byte-level protocol reference.
- [docs/backend-contract.md](docs/backend-contract.md): status.json schema
  and the full `sony-ctl` grammar.
- [docs/ui-spec.md](docs/ui-spec.md): widget layout and behavior.

## Requirements

- Omarchy (for the bar widget)
- BlueZ, with the headset paired to this computer
- systemd user session
- Build tools: CMake 3.20 or newer, Ninja, pkg-config, a C++20 compiler
  (GCC or Clang)
- Libraries: BlueZ (`bluez` pkg-config module) and D-Bus (`dbus-1`)

Arch Linux / Omarchy:

```bash
sudo pacman -S --needed base-devel cmake ninja bluez-libs dbus
```

On Arch, `setup` checks for `bluez-libs` (or `bluez`), `dbus`, `cmake`,
`ninja` and `gcc` (or `clang`), and installs missing ones. On other
distributions it skips this check. On Debian or Ubuntu the packages are
`build-essential cmake ninja-build pkg-config libbluetooth-dev
libdbus-1-dev`.

## Install

```bash
git clone <repo-url>
cd <repo-dir>
./setup
```

`setup` has no options except `-h` / `--help`. It does these steps:

1. Checks the build dependencies (Arch only, see above).
2. Builds the daemon and the CLI:
   `cmake -B build -G Ninja && cmake --build build`.
3. Installs `sony-headphones-daemon` and `sony-ctl` into `~/.local/bin`.
4. Copies `daemon/sony-headphones.service` into `~/.config/systemd/user/`,
   runs `systemctl --user daemon-reload` and
   `systemctl --user enable sony-headphones.service`.
5. Copies the plugin files (`manifest.json` and every `*.qml` and `*.js`
   file in the repository root) into `~/.config/omarchy/plugins/<plugin-id>`,
   and runs `omarchy plugin enable <plugin-id>`. It does not write into
   `~/.local/share/omarchy`: that tree belongs to Omarchy, and
   `omarchy refresh` replaces it. If an older `setup` left a copy in
   `~/.local/share/omarchy/shell/plugins/<plugin-id>`, `setup` removes it.

`<plugin-id>` is the `id` value in `manifest.json`.

To build without installing:

```bash
cmake -B build -G Ninja
cmake --build build
```

### Start the daemon

`setup` enables the service but does not start it. The service starts with
the next graphical session. To start it now:

```bash
systemctl --user start sony-headphones.service
```

Check the service and its log:

```bash
systemctl --user status sony-headphones.service
journalctl --user -u sony-headphones.service -f
```

The daemon picks a paired headset whose Bluetooth name contains
`WH-1000XM5`, and prefers one that is connected. It does not pick other Sony
models (for example WF-1000XM5 earbuds), even though they offer the same
service. A paired device without a known name is used only when it offers
the Sony service and no WH-1000XM5 is paired. To use a specific headset, run
the daemon with `--device <MAC>` (for example in a systemd drop-in that
overrides `ExecStart`).

### Enable the widget

`setup` enables the widget if the `omarchy` command exists. To do it by
hand:

```bash
omarchy plugin enable <plugin-id>
```

### Uninstall

`setup` has no uninstall step. Remove the files by hand:

```bash
omarchy plugin disable <plugin-id>
systemctl --user disable --now sony-headphones.service
rm ~/.config/systemd/user/sony-headphones.service
systemctl --user daemon-reload
rm ~/.local/bin/sony-headphones-daemon ~/.local/bin/sony-ctl
rm -r ~/.config/omarchy/plugins/<plugin-id>
rm -r ~/.local/state/sony-headphones
```

## Command-line tool

```
sony-ctl [-s <socket>] <verb> [args...]
```

| Verb | Arguments | Effect |
|---|---|---|
| `status` | | Print the status as one JSON line |
| `probe` | | Print debug data about the headset (see below) |
| `noise` | `anc`, `ambient`, `off` | Noise control mode |
| `ambient-level` | `1`..`20` | Ambient level; also switches to ambient mode |
| `focus-voice` | `on`, `off` | Focus on Voice |
| `eq` | `off`, `bright`, `excited`, `mellow`, `relaxed`, `vocal`, `treble`, `bass`, `speech`, `custom`, `user1`..`user5` | Equalizer preset |
| `eq custom` | `<b1> <b2> <b3> <b4> <b5> <clear_bass>`, each `-10`..`10` | Custom equalizer: 400 Hz, 1 kHz, 2.5 kHz, 6.3 kHz, 16 kHz, then Clear Bass |
| `dsee` | `on`, `off` | DSEE Extreme |
| `speak-to-chat` | `on`, `off` | Speak-to-Chat |
| `speak-to-chat-config` | `auto\|high\|low` `short\|standard\|long\|off` | Sensitivity and timeout |
| `voice-guidance` | `on`, `off` | Voice guidance |
| `voice-guidance-volume` | `-2`..`2` | Voice guidance volume |
| `auto-power-off` | `off`, `5min`, `15min`, `30min`, `60min`, `180min`, `when_taken_off` | Auto power off |
| `nc-amb-button` | `nc_ambient_off`, `nc_ambient`, `nc_off`, `ambient_off` | NC/AMB button cycle |
| `button-function` | `ambient_sound_control`, `voice_recognition`, `google_assistant`, `amazon_alexa`, `quick_access`, `none` | NC/AMB button function |
| `ear-detect` | `on`, `off` | Pause when taken off |
| `volume` | `0`..`30` | Headset volume |
| `connection-quality` | `sound`, `connection` | Connection priority |
| `multipoint` | `on`, `off` | Multipoint; the headset reconnects |
| `source` | Bluetooth address | Make a connected device the audio source |
| `touch-panel` | `on`, `off` | Touch sensor control panel |
| `quick-access` | `double` or `triple`, then `off` or `spotify` | Quick Access function for a double or triple press of the NC/AMB button |

Exit codes: `0` success, `1` bad arguments or an error from the daemon,
`2` the daemon socket is not available. A setter prints `OK` when the
headset acknowledges the packet. The full grammar, the error texts and the
status.json fields are in
[docs/backend-contract.md](docs/backend-contract.md).

## Hardware debugging and bug reports

Most features need confirmation on a real XM5. Reports from XM5 owners help
a lot.

1. Connect the headset and wait until the widget shows its settings.
2. Run:

   ```bash
   sony-ctl probe > probe.json
   sony-ctl status > status.json
   journalctl --user -u sony-headphones.service -b > daemon.log
   ```

   `probe` prints the model, the firmware version, the support function
   lists the headset reports, and the last 64 protocol frames. It returns
   `ERR not connected` when the headset is not connected.
3. If a control does not work, use it once, then run `sony-ctl probe` again.
   The new frames show what the headset answered.

Put this in the bug report:

- The three files above.
- What you did, what you expected, and what happened.
- The headset firmware version (it is also in `probe.json`).

The files contain Bluetooth addresses of the headset and of paired devices.
You can replace them with `XX:XX:XX:XX:XX:XX`. Do not change other values.

Section 8 of [docs/protocol.md](docs/protocol.md) lists the open questions
that probe output can answer.

## Development

### Tests

Build first, then run the C++ tests:

```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the widget model tests (Node.js or Deno):

```bash
node tests/model.test.js
```

Run the slider wheel test. It sends real wheel, click and drag events to
the stock Omarchy `PanelSlider` in a row with the panel's wheel layer on top. It
needs `qmltestrunner` (`qt6-declarative`) and the Omarchy shell; without
them it skips:

```bash
tests/slider_wheel.sh
```

Run the end-to-end suite. It needs `python3` and `jq`, and does not use
Bluetooth:

```bash
tests/integration.sh
```

The suite uses `build/daemon/sony-headphones-daemon --mock` and
`build/cli/sony-ctl` when they exist. Otherwise it falls back to the Python
mocks in `tests/`. It also runs the model tests if `node` or `deno` exists.

The suite creates its sandbox in `/tmp`. Set `SANDBOX_PARENT` to use another
directory, for example when `/tmp` is not writable. Keep the path short:
`<SANDBOX_PARENT>/e2e.XXXXXX/run/sony-headphones.sock` must fit the UNIX
socket limit of 107 characters, else the daemon does not start.

```bash
SANDBOX_PARENT=/path/to/short/dir tests/integration.sh
```

### Mock daemon

`--mock` runs the daemon against a simulated headset. No Bluetooth is used.
Use separate directories so the real daemon is not affected. Keep the
runtime directory path short (see the socket limit above):

```bash
./build/daemon/sony-headphones-daemon --mock \
  --state-dir /tmp/sony-mock/state --runtime-dir /tmp/sony-mock/run
./build/cli/sony-ctl -s /tmp/sony-mock/run/sony-headphones.sock status
```

The status file is then `/tmp/sony-mock/state/sony-headphones/status.json`.

### Branches

The project uses git-flow:

- `main`: released versions.
- `develop`: integration branch. New work starts here.
- `feature/<name>`: one branch per change, created from `develop` and
  merged back into `develop`.

## License

MIT. See [LICENSE](LICENSE).
