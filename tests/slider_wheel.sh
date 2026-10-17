#!/usr/bin/env bash
# tests/slider_wheel.sh
# Runs tests/slider_wheel/tst_slider_wheel.qml with qmltestrunner against the
# stock PanelSlider.qml of the installed Omarchy shell. The SliderWheel
# inline component is cut out of Panel.qml into a temporary SliderWheel.qml,
# so the test checks the shipped code. qs.Commons and BorderSurface come from
# tests/slider_wheel/stubs: the real ones need the Quickshell plugin, which
# loads only inside quickshell.
#
# Needs: qmltestrunner (qt6-declarative) and the Omarchy shell in
# $OMARCHY_SHELL (default ~/.local/share/omarchy/shell).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SHELL_DIR="${OMARCHY_SHELL:-$HOME/.local/share/omarchy/shell}"
RUNNER="${QMLTESTRUNNER:-/usr/lib/qt6/bin/qmltestrunner}"

if [[ ! -x "$RUNNER" ]]; then
  echo "SKIP: qmltestrunner not found at $RUNNER" >&2
  exit 0
fi
if [[ ! -f "$SHELL_DIR/Ui/PanelSlider.qml" ]]; then
  echo "SKIP: Omarchy shell not found at $SHELL_DIR" >&2
  exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# qs.* imports resolve through an import path that holds a "qs" directory.
mkdir -p "$WORK/qs" "$WORK/tests"
cp -r "$SCRIPT_DIR/slider_wheel/stubs/Commons" "$SCRIPT_DIR/slider_wheel/stubs/Ui" "$WORK/qs/"
cp "$SHELL_DIR/Ui/PanelSlider.qml" "$WORK/qs/Ui/"
cp "$SCRIPT_DIR/slider_wheel/tst_slider_wheel.qml" "$WORK/tests/"

{
  printf 'import QtQuick\nimport qs.Ui\n\n'
  awk '
    /^  component SliderWheel: MouseArea \{$/ { on = 1; print "MouseArea {"; next }
    on && /^  \}$/ { print "}"; exit }
    on { sub(/^  /, ""); print }
  ' "$REPO_ROOT/Panel.qml"
} > "$WORK/tests/SliderWheel.qml"

if ! grep -q "onWheel" "$WORK/tests/SliderWheel.qml"; then
  echo "FAIL: cannot extract SliderWheel from Panel.qml" >&2
  exit 1
fi

QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
  "$RUNNER" -import "$WORK" -input "$WORK/tests/tst_slider_wheel.qml"
