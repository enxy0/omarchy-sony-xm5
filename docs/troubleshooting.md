# Troubleshooting

**The panel stays on "reading headset settings".** A headset allows one
control session at a time. Close the Sony app on your phone.

**The headset does not appear.** The daemon does not connect headsets; connect
it in the Omarchy Bluetooth panel or with `bluetoothctl connect <MAC>`. Check
that the service runs: `systemctl --user status sony-headphones`.

**The panel says the daemon is not installed or older than the widget.**
Press **Install** or **Update** in the panel, or run
`~/.config/omarchy/plugins/io.github.enxy0.sonyxm5/setup`.

**Daemon log:** `journalctl --user -u sony-headphones -f`.

## Reporting a problem

With the headset connected, run:

```bash
sony-ctl probe > probe.json
sony-ctl status > status.json
journalctl --user -u sony-headphones -b > daemon.log
```

`probe` prints the model, the firmware, the features the headset reports and
its last 64 protocol frames. If a control does not work, use it once and run
`probe` again. Attach the three files to the issue together with what you did
and what happened. The files contain Bluetooth addresses; you can replace
them with `XX:XX:XX:XX:XX:XX`.
