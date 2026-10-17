// SonyIcon.qml
// Headset glyph with state tints: Material Design "headphones" (U+F02CB) or
// "earbuds" (U+F184F) from the Nerd Font, the icon set of the first-party
// bar widgets. It paints through OpticalGlyph, the component BarIconButton
// uses, so in a box of Style.bar.iconCanvas at Style.bar.iconFont it sits on
// the same vertical center as the Audio and Bluetooth glyphs.
import QtQuick
import qs.Commons
import qs.Ui
import "Model.js" as Model

Item {
  id: root

  // "headphones" or "earbuds" (status.json `kind`).
  property string kind: Model.KIND_HEADPHONES
  // Font pixel size of the glyph; the box defaults to the same size.
  property real iconSize: Style.font.icon
  property real boxSize: iconSize
  property string fontFamily: Style.font.family
  property int batteryLevel: -1
  property bool charging: false

  property color normalColor: Color.foreground
  property color alertColor: Color.urgent
  property color chargingColor: "#2ecc71"

  readonly property color effectiveColor: {
    if (charging) return chargingColor
    if (batteryLevel >= 0 && batteryLevel < 20) return alertColor
    return normalColor
  }

  property color color: effectiveColor

  width: boxSize
  height: boxSize
  implicitWidth: boxSize
  implicitHeight: boxSize

  Behavior on color {
    ColorAnimation { duration: 180 }
  }

  OpticalGlyph {
    anchors.fill: parent
    text: Model.headsetGlyph(root.kind)
    fontFamily: root.fontFamily
    fontSize: root.iconSize
    color: root.color
  }
}
