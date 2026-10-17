# sony-ctl

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
running. The full grammar, the error texts and the status format are in
[backend-contract.md](backend-contract.md).
