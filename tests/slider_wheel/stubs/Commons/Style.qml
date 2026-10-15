// Test stub: the part of qs.Commons Style that PanelSlider reads. The real
// singleton needs the Quickshell plugin, which loads only inside quickshell.
pragma Singleton
import QtQuick

QtObject {
  readonly property QtObject spacing: QtObject {
    readonly property int controlHeight: 28
    readonly property int md: 8
  }

  function space(px) { return px }
  function selectedFillFor(foreground, accent) { return accent }
}
