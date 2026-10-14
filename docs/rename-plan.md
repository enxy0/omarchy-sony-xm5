# Rename plan: omarchy-sony-xm5

Status: plan only. Nothing is renamed yet. Do the rename in one change,
after the schema 3 daemon and widget work.

## 1. Decisions

| Item | Today | After | Reason |
|---|---|---|---|
| Repository | `omarchy-sony-wh1000xm5` (fork of `andROYdified/omarchy-sony`) | `omarchy-sony-xm5` | The project manages the WH-1000XM5 and the WF-1000XM5 |
| CMake project | `omarchy-sony` | `omarchy-sony-xm5` | Match the repository |
| Plugin id | `io.github.androydified.omasony` | `io.github.enxy0.sonyxm5` | The fork has its own owner. No `-` in the id: `Panel.qml` uses the id as a QML `moduleName`, and QML module names cannot hold `-` |
| Plugin name, bar widget display name | `Sony Headphones` | `Sony XM5` | Short, names both models |
| Plugin description | `Sony WH-1000XM5 Headphone Manager` | `Sony WH-1000XM5 and WF-1000XM5 manager` | |
| Binaries | `sony-headphones-daemon`, `sony-ctl` | **unchanged** | See below |
| systemd unit | `sony-headphones.service` | **unchanged** | See below |
| Socket | `$XDG_RUNTIME_DIR/sony-headphones.sock` | **unchanged** | See below |
| State directory | `$XDG_STATE_HOME/sony-headphones/` | **unchanged** | See below |

Why the binaries, the unit, the socket and the state path keep their names:

- Earbuds are headphones too; "sony-headphones" is still correct.
- Users have `systemctl --user enable sony-headphones.service`, possibly a
  drop-in `sony-headphones.service.d/` with `--device`, and journal
  commands in their notes. A new unit name needs a migration step in
  `setup` and breaks those.
- The socket and state paths are shared by the daemon, the CLI, the widget
  and the tests. Renaming them changes the contract for no user benefit.

## 2. Plugin id migration

A new plugin id is a new plugin for Omarchy:

1. `setup` deploys to `~/.config/omarchy/plugins/io.github.enxy0.sonyxm5`
   and removes the old directory
   `~/.config/omarchy/plugins/io.github.androydified.omasony` (and the older
   `~/.local/share/omarchy/shell/plugins/io.github.androydified.omasony`,
   which `setup` already removes).
2. The bar entry in `~/.config/omarchy/shell.json` is stored under the old
   id. `setup` prints that the user must add the "Sony XM5" widget to the
   bar again. `setup` does not edit `shell.json` itself: the format belongs
   to Omarchy.

Open question for the user: keep the old id instead and skip the migration?
The cost is only a foreign name in the id.

## 3. Every place to change

| File | What changes |
|---|---|
| `manifest.json` | `id`, `name`, `description`, `barWidget.displayName`, `barWidget.description` |
| `Panel.qml` | `moduleName`, `ipcTarget` (the plugin id); header comment "Sony WH-1000XM5" |
| `Model.js` | header comment; the fallback name `"WH-1000XM5"` in `deviceTitle` becomes model-aware; texts "The sony-headphones daemon ..." stay (daemon name unchanged) |
| `setup` | header comments and help text "omarchy-sony (Sony WH-1000XM5 Plugin ...)", log lines "omarchy-sony setup ...", plus the migration of section 2 |
| `CMakeLists.txt` | `project(omarchy-sony ...)` and `DESCRIPTION "Sony WH-1000XM5 Headless ..."` |
| `cli/CMakeLists.txt` | `DESCRIPTION "Sony WH-1000XM5 Headless Daemon Control CLI"` |
| `daemon/sony-headphones.service` | `Description=Sony WH-1000XM5 Headless Bluetooth Daemon` (the file name stays) |
| `daemon/src/main.cpp` | help text "managing Sony WH-1000XM5 headphones"; `--device` help (repeatable); default alias `"WH-1000XM5"` (schema 3 work) |
| `daemon/src/HeadphoneState.hpp`, `HeadphoneState.cpp` | default `deviceName` `"WH-1000XM5"` becomes per model (schema 3 work, contract 3.3) |
| `daemon/src/BluetoothManager.cpp`, `.hpp` | `isWh1000xm5` selection and comments (schema 3 work, contract 2.2); the mock device names stay |
| `daemon/src/FakeHeadset.cpp` | model name reply per mock profile (schema 3 work) |
| `README.md` | title, intro, upstream link line (keep the credit to `andROYdified/omarchy-sony`), the `omarchy bar set <id> ...` command, the device selection section (both models, connected only, repeatable `--device`), clone URL |
| `docs/ui-spec.md` | the `omarchy bar set io.github.androydified.omasony ...` command in "Display setting" |
| `docs/protocol.md`, `docs/backend-contract.md` | no change needed (already name both models) |
| `tests/integration.sh` | header comment and banner "omarchy-sony" |
| `tests/challenger_stress.py`, `tests/stress_ipc.py` | header docstrings "OMARCHY-SONY", "Omarchy-Sony" |
| temp prefixes `omasony_*` in `tests/*.py`, `tests/stress_multi_process.cpp`, `daemon/tests/test_ipc.cpp` | optional; they are private temp names. Leave them unless the change touches the file anyway |
| GitHub | rename the repository on GitHub (it redirects the old URL); update the `origin` remote and the Forgejo push URL (`forgejo-pushurl.sh`) |

Not affected: `sony-ctl`, `sony-headphones-daemon`, the socket path, the
state path, `Service.qml` (it uses the state path only), the fixtures.
