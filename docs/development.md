# Development

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
`~/.config/omarchy/plugins`. After a change to `Service.qml` or `Model.js`,
run `omarchy restart shell`: the shell caches them on a hot reload.

The version lives in `manifest.json` only. CMake reads it, the daemon and
`sony-ctl` report it, and the widget compares it with the running daemon.
`pkgver` in `PKGBUILD` must match it.

| Document | Content |
|---|---|
| [protocol.md](protocol.md) | the Sony MDR protocol as this project uses it, captures and hardware test results |
| [backend-contract.md](backend-contract.md) | `status.json`, the socket and the `sony-ctl` grammar |
| [ui-spec.md](ui-spec.md) | the bar button and the panel |

Branches: `main` holds releases, `develop` is the integration branch, and
each change is a `feature/<name>` branch from `develop`.
