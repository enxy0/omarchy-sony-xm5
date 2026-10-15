// tst_slider_wheel.qml
// Real wheel events against the stock Omarchy PanelSlider with the
// SliderWheel layer from Panel.qml inside it (docs/ui-spec.md, rule 8).
// Run through tests/slider_wheel.sh, which extracts SliderWheel.qml.
import QtQuick
import QtTest
import qs.Ui

Item {
  id: root
  width: 300
  height: 240

  property int releases: 0
  property real lastReleased: NaN

  Flickable {
    id: flick
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: 200
    contentWidth: width
    contentHeight: column.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    interactive: contentHeight > height

    Column {
      id: column
      width: flick.width

      Item { width: 1; height: 150 }

      PanelSlider {
        id: slider
        width: parent.width
        value: 5
        minimum: 0
        maximum: 10
        step: 1
        integer: true
        onReleased: function(v) {
          root.releases += 1
          root.lastReleased = v
          slider.value = v
        }

        SliderWheel {}
      }

      Item { width: 1; height: 100 }
    }
  }

  PanelSlider {
    id: bare
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    value: 5
    minimum: 0
    maximum: 10
    step: 1
    integer: true
    onReleased: function(v) { bare.value = v }
  }

  TestCase {
    name: "SliderWheel"
    when: windowShown

    readonly property real notch: Qt.styleHints.wheelScrollLines * 24

    function init() {
      flick.contentHeight = Qt.binding(function() { return column.implicitHeight })
      flick.contentY = 0
      slider.value = 5
      root.releases = 0
      root.lastReleased = NaN
    }

    function sliderPoint() {
      return slider.mapToItem(root, slider.width / 2, slider.height / 2)
    }

    function wheelOnSlider(dx, dy, modifiers) {
      var p = slider.mapToItem(root, slider.width / 2, slider.height / 2)
      mouseWheel(root, p.x, p.y, dx, dy, Qt.NoButton, modifiers)
    }

    function maxY() {
      return flick.contentHeight - flick.height
    }

    function test_plainWheelScrollsPanel() {
      verify(maxY() > 0)
      wheelOnSlider(0, -120, Qt.NoModifier)
      tryCompare(flick, "contentY", Math.min(notch, maxY()))
      compare(slider.value, 5)
      compare(root.releases, 0)
      // Wheel up comes back by the same step.
      wheelOnSlider(0, 120, Qt.NoModifier)
      tryCompare(flick, "contentY", 0)
      compare(root.releases, 0)
    }

    function test_plainWheelClampsAtTop() {
      flick.contentY = 10
      wheelOnSlider(0, 120, Qt.NoModifier)
      tryCompare(flick, "contentY", 0)
      wait(200)
      compare(flick.contentY, 0)
      compare(root.releases, 0)
    }

    function test_plainWheelClampsAtBottom() {
      flick.contentY = maxY() - 10
      wheelOnSlider(0, -120, Qt.NoModifier)
      tryCompare(flick, "contentY", maxY())
      wait(200)
      compare(flick.contentY, maxY())
      compare(root.releases, 0)
    }

    // Control: the stock slider without the layer takes the plain wheel.
    // This proves the harness delivers wheel events to PanelSlider.
    function test_bareSliderTakesPlainWheel() {
      bare.value = 5
      var p = bare.mapToItem(root, bare.width / 2, bare.height / 2)
      mouseWheel(root, p.x, p.y, 0, 120, Qt.NoButton, Qt.NoModifier)
      compare(bare.value, 6)
    }

    function test_noScrollWhenContentFits() {
      flick.contentHeight = flick.height
      wheelOnSlider(0, -120, Qt.NoModifier)
      wait(200)
      compare(flick.contentY, 0)
      compare(slider.value, 5)
      compare(root.releases, 0)
    }

    function test_shiftWheelStepsValue() {
      wheelOnSlider(0, 120, Qt.ShiftModifier)
      compare(root.releases, 1)
      compare(root.lastReleased, 6)
      wheelOnSlider(0, -120, Qt.ShiftModifier)
      compare(root.releases, 2)
      compare(root.lastReleased, 5)
      wait(200)
      compare(flick.contentY, 0)
    }

    function test_shiftWheelHorizontalUsesX() {
      wheelOnSlider(120, 0, Qt.ShiftModifier)
      compare(root.lastReleased, 6)
      wheelOnSlider(-120, 0, Qt.ShiftModifier)
      compare(root.lastReleased, 5)
    }

    function test_shiftWheelClampsAtBound() {
      slider.value = 10
      wheelOnSlider(0, 120, Qt.ShiftModifier)
      compare(root.lastReleased, 10)
    }

    function test_clickStillSetsValue() {
      var p = slider.mapToItem(root, slider.width * 0.9, slider.height / 2)
      mouseClick(root, p.x, p.y)
      compare(root.releases, 1)
      verify(root.lastReleased >= 8)
    }

    function test_dragStillWorks() {
      var a = slider.mapToItem(root, 2, slider.height / 2)
      var b = slider.mapToItem(root, slider.width - 2, slider.height / 2)
      mousePress(root, a.x, a.y)
      verify(slider.dragging)
      mouseMove(root, b.x, b.y)
      mouseRelease(root, b.x, b.y)
      verify(!slider.dragging)
      compare(root.lastReleased, 10)
    }

    function test_rightClickReachesSlider() {
      var spy = createTemporaryQmlObject(
        "import QtTest; SignalSpy { signalName: 'rightClicked' }", root)
      spy.target = slider
      var p = sliderPoint()
      mouseClick(root, p.x, p.y, Qt.RightButton)
      compare(spy.count, 1)
      compare(root.releases, 0)
    }

    function test_hoverReachesSlider() {
      var p = sliderPoint()
      mouseMove(root, p.x, p.y)
      tryVerify(function() { return slider._hot })
    }
  }
}
