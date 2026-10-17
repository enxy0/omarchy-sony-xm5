# Install, update, remove

## With setup (the default)

```bash
omarchy plugin add https://github.com/enxy0/omarchy-sony-xm5 --enable
```

Then press **Install** in the panel, or run setup yourself:

```bash
~/.config/omarchy/plugins/io.github.enxy0.sonyxm5/setup
```

`setup`:

- installs the build packages that are missing (`cmake`, `ninja`, `gcc`,
  `bluez-libs`, `dbus`) with pacman; sudo asks for your password only then;
- builds the daemon and `sony-ctl` in `~/.cache/omarchy-sony-xm5`;
- puts `sony-headphones-daemon` and `sony-ctl` in `~/.local/bin`;
- installs the `sony-headphones.service` systemd user unit, enables it and
  starts it.

The daemon runs as your user. Apart from the build packages, nothing is
installed outside your home directory.

**Update:** `omarchy plugin update io.github.enxy0.sonyxm5`. If the daemon is
now older than the widget, the panel shows "Update the daemon" with an
**Update** button, which runs `setup` again.

**Remove:**

```bash
~/.config/omarchy/plugins/io.github.enxy0.sonyxm5/setup --uninstall
omarchy plugin remove io.github.enxy0.sonyxm5
```

`setup --uninstall` stops the service and removes the daemon, `sony-ctl`,
the unit, the status file and the build directory.

## With the Arch package

If you want pacman to own the daemon, build the package from the `PKGBUILD`
in the repository. It installs into `/usr`. The widget still comes from
`omarchy plugin add`. Use either the package or `setup`, not both.

```bash
git clone https://github.com/enxy0/omarchy-sony-xm5
cd omarchy-sony-xm5
makepkg -si
systemctl --user enable --now sony-headphones.service
```

Remove: `systemctl --user disable --now sony-headphones.service`, then
`sudo pacman -Rns omarchy-sony-xm5`.

## Which headsets the daemon uses

The daemon takes every connected headset whose Bluetooth name contains
`WH-1000XM5` or `WF-1000XM5`. It never connects a headset itself: connect it
as usual and the daemon picks it up within a few seconds. To limit it to
specific headsets, pass `--device <MAC>` (once per headset) in a systemd
drop-in for `sony-headphones.service` that overrides `ExecStart`.

## Files

| What | Where |
|---|---|
| Status file | `$XDG_STATE_HOME/sony-headphones/status.json` (`~/.local/state/...` by default) |
| Socket | `$XDG_RUNTIME_DIR/sony-headphones.sock` |
| Daemon log | `journalctl --user -u sony-headphones -f` |

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
