# Sony XM5 for Omarchy

English | [Русский](README.ru.md)

An [Omarchy](https://omarchy.org) bar widget for the Sony WH-1000XM5
headphones and WF-1000XM5 earbuds. It shows the battery and controls noise
cancelling, the equalizer and the other headset settings, without the Sony
app. A background daemon talks to the headset over Bluetooth, and the
`sony-ctl` command works with the same daemon.

| WH-1000XM5 | WF-1000XM5 |
|---|---|
| ![Panel with the WH-1000XM5](docs/screenshots/wh-1000xm5.png) | ![Panel with the WF-1000XM5](docs/screenshots/wf-1000xm5.png) |

This is an unofficial project. It is not affiliated with or endorsed by Sony.
"Sony", "WH-1000XM5" and "WF-1000XM5" are trademarks of Sony Corporation.

## Features

Tested on a WH-1000XM5 (firmware 2.5.1) and a WF-1000XM5 (firmware 6.1.0):
every setting below was changed, read back after a Bluetooth reconnect and
restored. The results are in [docs/protocol.md](docs/protocol.md), section 10.
The panel shows only the controls the connected headset reports.

| Setting | WH-1000XM5 | WF-1000XM5 |
|---|---|---|
| Battery | yes | left, right and case |
| Noise cancelling, ambient sound, off; ambient level; Focus on Voice | yes | yes |
| Equalizer presets, custom 5-band equalizer, Clear Bass | yes | yes |
| DSEE Extreme | yes | yes |
| Speak-to-Chat, with sensitivity and timeout | yes | yes |
| Connection priority: sound quality or stable connection | yes | no |
| Headset volume | yes | yes |
| Voice guidance | yes | yes, with volume |
| Auto power off: off or when taken off | yes | yes |
| Pause when taken off | yes | yes |
| NC/AMB button: which modes it cycles through | yes | no |
| Touch panel on or off | yes | no |
| Quick Access: Spotify on a double or triple press | yes | no |
| Touch function of each earbud | no | yes |
| Hear your own voice during calls | no | yes |
| Multipoint, connected devices, switching the audio source | yes | yes |
| Codec, model and firmware | yes | yes |

Both models can be connected at the same time. The panel then has a tab for
each one.

### In the bar

![Bar tooltip with two headsets](docs/screenshots/bar-tooltip.png)

- The icon shows the headset that plays from this computer. If both or
  neither do, it shows the one connected last. Headphones and earbuds have
  different icons.
- The icon turns green while charging and red below 20 %. It hides 3 seconds
  after the last headset disconnects.
- The tooltip has one line per headset; `(bar)` marks the one the icon shows.
- Left click opens the panel. Right click switches the noise mode of that
  headset: noise cancelling, ambient sound, off.

Keys in the panel:

| Key | Action |
|---|---|
| `j` `k` or arrows | move between controls |
| `h` `l` | change a value, open or close a section |
| `Enter` `Space` | select |
| `[` `]` | previous or next headset |
| `Esc` | close |

## Install

You need Omarchy and a headset that is paired and connected (in the Omarchy
Bluetooth panel or with `bluetoothctl`).

1. Add the widget:

   ```bash
   omarchy plugin add https://github.com/enxy0/omarchy-sony-xm5 --enable
   ```

2. Click the headphones icon in the bar and press **Install**. A terminal
   opens and runs `setup`. You can also run it yourself:

   ```bash
   ~/.config/omarchy/plugins/io.github.enxy0.sonyxm5/setup
   ```

`setup` does the following:

- installs the build packages that are missing (`cmake`, `ninja`, `gcc`,
  `bluez-libs`, `dbus`) with pacman; sudo asks for your password only then;
- builds the daemon and `sony-ctl` in `~/.cache/omarchy-sony-xm5`;
- puts `sony-headphones-daemon` and `sony-ctl` in `~/.local/bin`;
- installs the `sony-headphones.service` systemd user unit, enables it and
  starts it.

The daemon runs as your user. Apart from the build packages, nothing is
installed outside your home directory.

### Update

```bash
omarchy plugin update io.github.enxy0.sonyxm5
```

If the daemon is now older than the widget, the panel shows "Update the
daemon" with an **Update** button. It runs `setup` again.

### Remove

```bash
~/.config/omarchy/plugins/io.github.enxy0.sonyxm5/setup --uninstall
omarchy plugin remove io.github.enxy0.sonyxm5
```

`setup --uninstall` stops the service and removes the daemon, `sony-ctl`,
the unit, the status file and the build directory.

### Arch package instead of setup

If you want pacman to own the daemon, build the package from the
`PKGBUILD` in this repository. It installs into `/usr`. The widget still
comes from `omarchy plugin add`.

```bash
git clone https://github.com/enxy0/omarchy-sony-xm5
cd omarchy-sony-xm5
makepkg -si
systemctl --user enable --now sony-headphones.service
```

Use either the package or `setup`, not both. To remove the package:
`systemctl --user disable --now sony-headphones.service`, then
`sudo pacman -Rns omarchy-sony-xm5`.

### Which headsets the daemon uses

The daemon takes every connected headset whose Bluetooth name contains
`WH-1000XM5` or `WF-1000XM5`. It never connects a headset itself: connect it
as usual and the daemon picks it up within a few seconds. To limit it to
specific headsets, pass `--device <MAC>` (once per headset) in a systemd
drop-in for `sony-headphones.service` that overrides `ExecStart`.

## Command line

```
sony-ctl [-s <socket>] [-H <headset>] <verb> [args...]
```

`-H` takes a Bluetooth address or a model (`wh-1000xm5`, `wf-1000xm5`).
Without it, the command goes to the headset the bar icon shows.

```bash
sony-ctl status                      # every headset as one JSON line
sony-ctl noise anc
sony-ctl -H wf-1000xm5 ambient-level 12
sony-ctl eq custom 2 1 0 2 3 1       # 5 bands and Clear Bass, -10..10
```

| Verb | Arguments |
|---|---|
| `status`, `probe` | |
| `noise` | `anc`, `ambient`, `off` |
| `ambient-level` | `1`..`20` (also switches to ambient sound) |
| `focus-voice` | `on`, `off` |
| `eq` | a preset: `off`, `bright`, `excited`, `mellow`, `relaxed`, `vocal`, `treble`, `bass`, `speech`, `custom`, `user1`, `user2` |
| `eq custom` | `<400Hz> <1k> <2.5k> <6.3k> <16k> <clear_bass>`, each `-10`..`10` |
| `dsee` | `on`, `off` |
| `speak-to-chat` | `on`, `off` |
| `speak-to-chat-config` | `auto`, `high` or `low`, then `short`, `standard`, `long` or `off` |
| `voice-guidance` | `on`, `off` |
| `voice-guidance-volume` | `-2`..`2` |
| `auto-power-off` | `off`, `when_taken_off` |
| `nc-amb-button` | `nc_ambient_off`, `nc_ambient`, `nc_off`, `ambient_off` |
| `ear-detect` | `on`, `off` (pause when taken off) |
| `volume` | `0`..`30` |
| `connection-quality` | `sound`, `connection` |
| `touch-panel` | `on`, `off` |
| `quick-access` | `double` or `triple`, then `off` or `spotify` |
| `touch-function` | `left` or `right`, then `ambient_sound_control`, `playback_control` or `none` |
| `sidetone` | `on`, `off` |
| `multipoint` | `on`, `off` (the headset may reconnect) |
| `source` | the Bluetooth address of a connected device |

A setter prints `OK` once the headset acknowledges it. Exit codes: `0`
success, `1` bad arguments or an error from the daemon, `2` the daemon is not
running. The full grammar and the status format are in
[docs/backend-contract.md](docs/backend-contract.md).

## How it works

```
Omarchy bar widget  ─── runs ───►  sony-ctl
      │                               │
    reads                        UNIX socket
      ▼                               ▼
status.json  ◄─── writes ───  sony-headphones-daemon
                                      │
                          Bluetooth RFCOMM, MDR v2
                                      ▼
                           WH-1000XM5, WF-1000XM5
```

- Status file: `$XDG_STATE_HOME/sony-headphones/status.json`
  (`~/.local/state/...` by default).
- Socket: `$XDG_RUNTIME_DIR/sony-headphones.sock`.
- Daemon log: `journalctl --user -u sony-headphones -f`.

A headset allows one control session at a time, so close the Sony app on
your phone if the widget stays on "reading headset settings".

## Reporting a problem

With the headset connected, run:

```bash
sony-ctl probe > probe.json
sony-ctl status > status.json
journalctl --user -u sony-headphones -b > daemon.log
```

`probe` prints the model, the firmware, the features the headset reports
and its last 64 protocol frames. If a control does not work, use it once and
run `probe` again. Attach the three files to the issue together with what you
did and what happened. The files contain Bluetooth addresses; you can replace
them with `XX:XX:XX:XX:XX:XX`.

## Development

```bash
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure    # protocol, sessions, IPC
node tests/model.test.js                      # widget logic
tests/integration.sh                          # daemon, CLI and widget logic end to end
tests/slider_wheel.sh                         # needs qmltestrunner and the Omarchy shell
```

None of them need Bluetooth. `--mock` runs the daemon against simulated
headsets (`--mock-profile both` gives one of each):

```bash
./build/daemon/sony-headphones-daemon --mock --mock-profile both \
  --state-dir /tmp/sony-mock/state --runtime-dir /tmp/sony-mock/run
./build/cli/sony-ctl -s /tmp/sony-mock/run/sony-headphones.sock status
```

Running `./setup` from a clone installs the daemon and copies the widget into
`~/.config/omarchy/plugins`. The documentation of the protocol, the status
format and the panel is in [docs/](docs/).

Branches: `main` holds releases, `develop` is the integration branch, and
each change is a `feature/<name>` branch from `develop`.

## Credits

- The project started as a fork of
  [andROYdified/omarchy-sony](https://github.com/andROYdified/omarchy-sony)
  (MIT) and has since been largely rewritten.
- The protocol knowledge comes from
  [mos9527/SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient)
  (MIT); the function name tables in `daemon/src/MDRProtocol.cpp` are taken
  from it.
- Some facts about the XM5 come from
  [Gadgetbridge](https://codeberg.org/Freeyourgadget/Gadgetbridge). No
  Gadgetbridge code is used.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the license texts.

## License

MIT. See [LICENSE](LICENSE).
