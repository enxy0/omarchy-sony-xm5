// Panel.qml
// Bar button and popup panel for the Sony WH-1000XM5 headphones and
// WF-1000XM5 earbuds (docs/ui-spec.md). The bar button follows the primary
// headset; the popup shows one headset at a time, with a switcher row when
// two or more are connected. Logic lives in Model.js; state and commands in
// Service.qml. Helper components are inline so the plugin stays a fixed set
// of files.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui
import "Model.js" as Model

Panel {
  id: root
  moduleName: "io.github.enxy0.sonyxm5"
  ipcTarget: "io.github.enxy0.sonyxm5"
  manageIpc: false

  // ---- inline components ---------------------------------------------------

  // Section title with a summary and a chevron; opens and closes a section.
  component CollapsibleHeader: CursorSurface {
    id: header

    property string text: ""
    property string summary: ""
    property bool expanded: false
    property string fontFamily: Style.font.family

    signal toggled()
    signal hovered(bool isHovered)

    implicitHeight: Math.max(headerTitle.implicitHeight, chevron.implicitHeight) + Style.space(8)

    PanelSectionHeader {
      id: headerTitle
      anchors.left: parent.left
      anchors.leftMargin: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      text: header.text
      foreground: header.foreground
      fontFamily: header.fontFamily
    }

    Text {
      anchors.right: chevron.left
      anchors.rightMargin: Style.space(8)
      anchors.left: headerTitle.right
      anchors.leftMargin: Style.space(8)
      anchors.verticalCenter: parent.verticalCenter
      horizontalAlignment: Text.AlignRight
      elide: Text.ElideRight
      textFormat: Text.PlainText
      text: header.summary
      color: Qt.darker(header.foreground, 1.4)
      font.family: header.fontFamily
      font.pixelSize: Style.font.caption
    }

    Text {
      id: chevron
      anchors.right: parent.right
      anchors.rightMargin: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      textFormat: Text.PlainText
      text: header.expanded ? "\u{F0140}" : "\u{F0142}"
      color: Qt.darker(header.foreground, 1.2)
      font.family: header.fontFamily
      font.pixelSize: Style.font.body
    }

    MouseArea {
      anchors.fill: parent
      cursorShape: Qt.PointingHandCursor
      onClicked: header.toggled()
    }

    HoverHandler {
      onHoveredChanged: header.hovered(hovered)
    }
  }

  // Pick-one chips in equal-width cells. options: [{ value, label, tooltip }]
  component ChoiceGrid: Grid {
    id: grid

    property var options: []
    property var value: null
    property int cursorIndex: -1
    property color foreground: Color.foreground
    property string fontFamily: Style.font.family
    property real fontSize: Style.font.caption

    signal picked(int index)
    signal hoveredIndex(int index, bool isHovered)

    spacing: Style.space(6)
    readonly property real cellWidth: columns > 0 ? (width - spacing * (columns - 1)) / columns : width

    Repeater {
      model: grid.options

      Button {
        required property var modelData
        required property int index

        width: grid.cellWidth
        text: String(modelData.label)
        tooltipText: modelData.tooltip ? String(modelData.tooltip) : ""
        fontSize: grid.fontSize
        foreground: grid.foreground
        fontFamily: grid.fontFamily
        bordered: true
        selected: modelData.value === grid.value
        hasCursor: grid.cursorIndex === index
        horizontalPadding: Style.space(4)
        verticalPadding: Style.space(6)
        onClicked: grid.picked(index)
        onHovered: function(h) { grid.hoveredIndex(index, h) }
      }
    }
  }

  // A caption on the left, one row of chips on the right (one column per
  // option). options: [{ value, label, tooltip }]
  component PressRow: Item {
    id: pressRow

    property string label: ""
    property real labelWidth: Style.space(80)
    property var options: []
    property var value: null
    property int cursorIndex: -1
    property color foreground: Color.foreground
    property string fontFamily: Style.font.family

    signal picked(int index)
    signal hoveredIndex(int index, bool isHovered)

    implicitHeight: Math.max(pressLabel.implicitHeight, pressGrid.implicitHeight)

    Text {
      id: pressLabel
      anchors.left: parent.left
      anchors.leftMargin: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      width: pressRow.labelWidth
      textFormat: Text.PlainText
      text: pressRow.label
      color: Qt.darker(pressRow.foreground, 1.2)
      font.family: pressRow.fontFamily
      font.pixelSize: Style.font.caption
    }

    ChoiceGrid {
      id: pressGrid
      anchors.left: pressLabel.right
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      columns: Math.max(1, pressRow.options.length)
      options: pressRow.options
      value: pressRow.value
      cursorIndex: pressRow.cursorIndex
      foreground: pressRow.foreground
      fontFamily: pressRow.fontFamily
      onPicked: function(i) { pressRow.picked(i) }
      onHoveredIndex: function(i, h) { pressRow.hoveredIndex(i, h) }
    }
  }

  // Wheel layer for a slider row. Put it last in the row (the highlighted
  // cursor surface) and set `slider`: declared after the slider, it is on
  // top of the slider's own MouseArea and gets the wheel first over the
  // whole row. PanelSlider always takes the wheel and commits a step, and
  // has no switch for this. A layer with `wheel.accepted = false` does not
  // help: the slider then still gets the event. So this layer accepts every
  // wheel event and acts itself:
  // - plain wheel: scrolls the nearest Flickable ancestor, about one
  //   Flickable wheel step per notch (wheelScrollLines * 24 px), clamped
  //   to 0..contentHeight-height. The slider value does not change.
  // - Shift+wheel: one slider step, the same path as the stock wheel
  //   (liveValue, moved, released). Some systems send Shift+wheel as a
  //   horizontal scroll, so x gives the direction when y is 0.
  // It takes no buttons and no hover, so click, drag, right click and the
  // pointer cursor still reach the slider.
  component SliderWheel: MouseArea {
    id: wheelLayer

    property PanelSlider slider: null
    property Flickable flickable: null
    property real targetY: 0

    anchors.fill: parent
    acceptedButtons: Qt.NoButton
    hoverEnabled: false

    function findFlickable() {
      for (var p = wheelLayer.parent; p; p = p.parent) {
        if (p instanceof Flickable) return p
      }
      return null
    }

    function scrollBy(pixels, animate) {
      var f = wheelLayer.flickable || wheelLayer.findFlickable()
      if (!f) return
      wheelLayer.flickable = f
      var maxY = Math.max(0, f.contentHeight - f.height)
      if (maxY <= 0) return
      var from = scrollAnim.running ? wheelLayer.targetY : f.contentY
      var to = Math.max(0, Math.min(maxY, from + pixels))
      scrollAnim.stop()
      f.cancelFlick()
      wheelLayer.targetY = to
      if (!animate) {
        f.contentY = to
        return
      }
      scrollAnim.target = f
      scrollAnim.from = f.contentY
      scrollAnim.to = to
      scrollAnim.start()
    }

    function stepSlider(direction) {
      var s = wheelLayer.slider
      if (!s) return
      var delta = direction > 0 ? s.step : -s.step
      var next = Math.max(s.minimum, Math.min(s.maximum, s.liveValue + delta))
      if (s.integer) next = Math.round(next)
      s.liveValue = next
      s.moved(next)
      s.released(next)
    }

    onWheel: function(wheel) {
      wheel.accepted = true
      var dy = wheel.angleDelta.y
      var dx = wheel.angleDelta.x
      if (wheel.modifiers & Qt.ShiftModifier) {
        var direction = dy !== 0 ? dy : dx
        if (direction !== 0) wheelLayer.stepSlider(direction)
        return
      }
      // A touchpad sends pixel deltas inside a scroll phase: follow them 1:1.
      // A mouse wheel can also carry pixel deltas, but with no phase; it uses
      // the angle step. Flickable picks the path the same way.
      if (wheel.phase !== Qt.NoScrollPhase && wheel.pixelDelta.y !== 0)
        wheelLayer.scrollBy(-wheel.pixelDelta.y, false)
      else if (dy !== 0) wheelLayer.scrollBy(-dy / 120 * Qt.styleHints.wheelScrollLines * 24, true)
    }

    NumberAnimation {
      id: scrollAnim
      property: "contentY"
      duration: 100
      easing.type: Easing.OutQuad
    }
  }

  // Cursor row: title and value on one line, a slider under it.
  // Commits on release (drag) and per Shift+wheel notch.
  component SliderRow: CursorSurface {
    id: sliderRow

    property QtObject bar: null
    property string title: ""
    property string valueText: ""
    property var format: function(v) { return String(Math.round(v)) }
    property real value: 0
    property real minimum: 0
    property real maximum: 1
    property bool dimmed: false
    property string fontFamily: Style.font.family

    signal committed(real value)
    signal hovered(bool isHovered)

    implicitHeight: sliderContent.implicitHeight + Style.space(10)

    Column {
      id: sliderContent
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      anchors.leftMargin: Style.space(6)
      anchors.rightMargin: Style.space(6)
      spacing: Style.space(4)

      Item {
        width: parent.width
        implicitHeight: Math.max(sliderTitle.implicitHeight, sliderValue.implicitHeight)

        PanelSectionHeader {
          id: sliderTitle
          anchors.left: parent.left
          anchors.verticalCenter: parent.verticalCenter
          text: sliderRow.title
          foreground: sliderRow.foreground
          fontFamily: sliderRow.fontFamily
        }

        Text {
          id: sliderValue
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          textFormat: Text.PlainText
          text: rowSlider.dragging ? sliderRow.format(rowSlider.liveValue) : sliderRow.valueText
          color: sliderRow.foreground
          font.family: sliderRow.fontFamily
          font.pixelSize: Style.font.caption
          font.bold: true
        }
      }

      PanelSlider {
        id: rowSlider
        width: parent.width
        bar: sliderRow.bar
        value: sliderRow.value
        minimum: sliderRow.minimum
        maximum: sliderRow.maximum
        step: 1
        integer: true
        opacity: sliderRow.dimmed ? 0.6 : 1
        // Skip no-op commits: a Shift+wheel notch at a bound releases the same value.
        onReleased: function(v) {
          if (Math.round(v) !== Math.round(sliderRow.value)) sliderRow.committed(Math.round(v))
        }
      }
    }

    HoverHandler {
      onHoveredChanged: sliderRow.hovered(hovered)
    }

    SliderWheel { slider: rowSlider }
  }

  // Compact cursor row for an EQ band: label, slider, signed value.
  component BandRow: CursorSurface {
    id: bandRow

    property QtObject bar: null
    property string label: ""
    property real value: 0
    property string fontFamily: Style.font.family

    signal committed(real value)
    signal hovered(bool isHovered)

    implicitHeight: Math.max(bandSlider.implicitHeight, bandLabel.implicitHeight) + Style.space(4)

    Text {
      id: bandLabel
      anchors.left: parent.left
      anchors.leftMargin: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      width: Style.space(64)
      textFormat: Text.PlainText
      text: bandRow.label
      color: Qt.darker(bandRow.foreground, 1.2)
      font.family: bandRow.fontFamily
      font.pixelSize: Style.font.caption
    }

    PanelSlider {
      id: bandSlider
      anchors.left: bandLabel.right
      anchors.right: bandValue.left
      anchors.rightMargin: Style.space(8)
      anchors.verticalCenter: parent.verticalCenter
      bar: bandRow.bar
      value: bandRow.value
      minimum: Model.EQ_LEVEL_MIN
      maximum: Model.EQ_LEVEL_MAX
      step: 1
      integer: true
      tickCount: 3
      onReleased: function(v) {
        if (Math.round(v) !== Math.round(bandRow.value)) bandRow.committed(Math.round(v))
      }
    }

    Text {
      id: bandValue
      anchors.right: parent.right
      anchors.rightMargin: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      width: Style.space(28)
      horizontalAlignment: Text.AlignRight
      textFormat: Text.PlainText
      text: Model.formatSigned(bandSlider.dragging ? bandSlider.liveValue : bandRow.value)
      color: bandRow.foreground
      font.family: bandRow.fontFamily
      font.pixelSize: Style.font.caption
      font.bold: true
    }

    HoverHandler {
      onHoveredChanged: bandRow.hovered(hovered)
    }

    SliderWheel { slider: bandSlider }
  }

  // One paired source device. Activating a connected, idle device makes it
  // the audio source.
  component DeviceRow: CursorSurface {
    id: deviceRow

    property var device: null
    property bool canSwitch: false
    property string fontFamily: Style.font.family

    signal clicked()
    signal hovered(bool isHovered)

    current: !!device && device.active
    implicitHeight: deviceText.implicitHeight + Style.space(10)

    Column {
      id: deviceText
      anchors.left: parent.left
      anchors.right: deviceState.left
      anchors.leftMargin: Style.space(8)
      anchors.rightMargin: Style.space(8)
      anchors.verticalCenter: parent.verticalCenter
      spacing: Style.space(1)

      Text {
        width: parent.width
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: Model.deviceLabel(deviceRow.device)
        color: deviceRow.foreground
        font.family: deviceRow.fontFamily
        font.pixelSize: Style.font.body
        font.bold: !!deviceRow.device && deviceRow.device.active
      }

      Text {
        width: parent.width
        visible: !!deviceRow.device && deviceRow.device.name !== ""
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: deviceRow.device ? deviceRow.device.address : ""
        color: Qt.darker(deviceRow.foreground, 1.5)
        font.family: deviceRow.fontFamily
        font.pixelSize: Style.font.caption
      }
    }

    Text {
      id: deviceState
      anchors.right: parent.right
      anchors.rightMargin: Style.space(8)
      anchors.verticalCenter: parent.verticalCenter
      textFormat: Text.PlainText
      text: Model.deviceStateLabel(deviceRow.device)
      color: !!deviceRow.device && deviceRow.device.connected ? deviceRow.foreground : Qt.darker(deviceRow.foreground, 1.6)
      font.family: deviceRow.fontFamily
      font.pixelSize: Style.font.caption
      font.bold: !!deviceRow.device && deviceRow.device.active
    }

    MouseArea {
      anchors.fill: parent
      enabled: deviceRow.canSwitch
      cursorShape: deviceRow.canSwitch ? Qt.PointingHandCursor : Qt.ArrowCursor
      onClicked: deviceRow.clicked()
    }

    HoverHandler {
      onHoveredChanged: deviceRow.hovered(hovered)
    }
  }

  // One tab of the headset switcher: kind glyph, name, bar battery. Drawn as
  // a bordered chip; the shown headset's tab is the selected chip.
  component HeadsetTab: Button {
    id: tab

    property var tabData: null

    bordered: true
    selected: !!tabData && tabData.selected
    horizontalPadding: Style.space(8)
    verticalPadding: Style.space(6)

    // Button reserves the widest border any of its states paints (normal,
    // hover-cursor, selected for a bordered, non-focusable chip), so the
    // content does not move when the state changes. Same reservation here,
    // from the public Border API.
    readonly property var _borderSpecs: [
      Border.controlSpec("normal", foreground, accent),
      Border.controlSpec("hover-cursor", foreground, accent),
      Border.controlSpec("selected", foreground, accent)
    ]
    function _reserved(side) {
      var w = 0
      for (var i = 0; i < _borderSpecs.length; i++) w = Math.max(w, Border[side](_borderSpecs[i]))
      return w
    }
    readonly property real insetLeft: horizontalPadding + _reserved("left")
    readonly property real insetRight: horizontalPadding + _reserved("right")
    readonly property real insetTop: verticalPadding + _reserved("top")
    readonly property real insetBottom: verticalPadding + _reserved("bottom")

    implicitHeight: tabContent.implicitHeight + insetTop + insetBottom

    // Same text color as Button's own label: the selected-state color.
    readonly property color contentColor: selected ? Style.selectedStateColor(foreground, accent) : foreground

    Item {
      id: tabContent
      anchors.fill: parent
      anchors.leftMargin: tab.insetLeft
      anchors.rightMargin: tab.insetRight
      anchors.topMargin: tab.insetTop
      anchors.bottomMargin: tab.insetBottom
      implicitHeight: Math.max(tabIcon.height, tabName.implicitHeight)

      SonyIcon {
        id: tabIcon
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        kind: tab.tabData ? tab.tabData.kind : Model.KIND_HEADPHONES
        iconSize: Style.space(16)
        fontFamily: tab.fontFamily
        normalColor: tab.contentColor
      }

      Text {
        id: tabName
        anchors.left: tabIcon.right
        anchors.leftMargin: Style.space(6)
        anchors.right: tabBattery.left
        anchors.rightMargin: Style.space(6)
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: tab.tabData ? tab.tabData.label : ""
        color: tab.contentColor
        font.family: tab.fontFamily
        font.pixelSize: Style.font.bodySmall
        font.bold: tab.selected
      }

      Text {
        id: tabBattery
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: tab.tabData ? tab.tabData.battery + (tab.tabData.charging ? " " + Model.GLYPH_BOLT : "") : ""
        color: tab.contentColor
        font.family: tab.fontFamily
        font.pixelSize: Style.font.bodySmall
      }
    }
  }

  // Dim one-line hint under a control.
  component Caption: Text {
    property color foreground: Color.foreground
    property string fontFamily: Style.font.family

    width: parent ? parent.width : implicitWidth
    visible: text !== ""
    textFormat: Text.PlainText
    wrapMode: Text.WordWrap
    color: Qt.darker(foreground, 1.5)
    font.family: fontFamily
    font.pixelSize: Style.font.caption
  }

  // ---- theme ---------------------------------------------------------------

  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color urgent: bar ? bar.urgent : Color.urgent
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
  readonly property real disabledOpacity: 0.45

  // ---- state ---------------------------------------------------------------

  Service {
    id: sony
  }

  // The headset the popup shows (ui-spec 8): kept for the shell session.
  property var selectedId: null
  // The bar button follows the primary headset.
  readonly property var barSt: sony.status
  // The popup follows the shown headset; every section reads `st`.
  readonly property var st: Model.shownHeadset(sony.status, selectedId)
  readonly property string shownId: st && st.id ? st.id : ""
  readonly property bool live: Model.isLive(st)
  readonly property bool reading: Model.isReading(st)
  readonly property bool earbuds: Model.isEarbuds(st)
  readonly property bool hasSwitcher: Model.switcherVisible(sony.status)

  // For read-only fields that have no cursor target (codec, device list).
  function shown(key, value) { return Model.isVisible(root.st, key, value) }

  // "Applying…" and the error line belong to the shown headset only.
  readonly property string commandErrorText: sony.errorFor(root.st.id)
  readonly property string statusLine: {
    if (commandErrorText !== "") return commandErrorText
    if (reading) return "Reading headset settings…"
    if (sony.busyVisible && sony.busyFor(root.st.id)) return "Applying…"
    return ""
  }

  // Shows headset `id` (tab click, Enter on a tab, `[` / `]`).
  function selectHeadset(id) {
    if (id === null || id === undefined) return
    selectedId = id
  }

  function stepHeadset(delta) {
    selectHeadset(Model.stepHeadset(sony.status, root.shownId, delta))
  }

  // After the shown headset changes, the cursor goes to the switcher row.
  onShownIdChanged: {
    if (!cursorActive) return
    var i = Model.headsetIndex(sony.status, shownId)
    if (hasSwitcher && i >= 0) {
      focusSection = Model.SWITCHER_TARGET
      selectedIndex = i
    }
  }

  // Collapsible sections; kept for the shell session.
  property var expanded: ({ sound: true, customEq: false, speakToChat: false, headset: false, multipoint: false })

  function setExpanded(name, value) {
    var next = {}
    for (var k in expanded) next[k] = expanded[k]
    next[name] = value
    expanded = next
  }

  function toggleExpanded(name) {
    setExpanded(name, !expanded[name])
  }

  // Targets are described once in Model.TARGETS; these wrap it for bindings.
  function shownFor(id) { return Model.targetVisible(root.st, id) }
  function enabledFor(id) { return Model.targetEnabled(root.st, id) }
  function choicesFor(id) { return Model.targetChoices(root.st, id) }

  // Click, Enter or Space on target `id` (chip `index` for chip rows).
  function activate(id, index) {
    var i = index === undefined ? 0 : index
    setCursor(id, i)
    if (id === Model.SWITCHER_TARGET) {
      selectHeadset(Model.switcherActivation(sony.status, i))
      return
    }
    var a = Model.targetActivation(st, id, i)
    if (!a) return
    if (a.expand) toggleExpanded(a.expand)
    else sony.send(a.action, a.value, root.st)
  }

  // Slider release on target `id`.
  function commit(id, value) {
    if (enabledFor(id)) sony.send(Model.TARGETS[id].action, value, root.st)
  }

  // ---- cursor model (first-party recipe, logic in Model.js) ----------------

  property bool cursorActive: false
  property string focusSection: ""
  property int selectedIndex: 0
  readonly property var cursorSections: Model.panelCursorSections(st, expanded)

  function hasCursor(id, index) {
    return cursorActive && focusSection === id && selectedIndex === (index === undefined ? 0 : index)
  }

  function cursorIndexFor(id) {
    return cursorActive && focusSection === id ? selectedIndex : -1
  }

  function setCursor(id, index) {
    cursorActive = true
    focusSection = id
    selectedIndex = index === undefined ? 0 : index
  }

  onCursorSectionsChanged: {
    if (!cursorActive) return
    var c = Model.cursorClamp({ section: focusSection, index: selectedIndex }, cursorSections)
    focusSection = c.section
    selectedIndex = c.index
  }

  function moveCursor(dx, dy) {
    if (cursorSections.length === 0) return
    if (!cursorActive) {
      var start = Model.cursorClamp({ section: focusSection, index: selectedIndex }, cursorSections)
      setCursor(start.section, start.index)
    } else {
      var r = Model.cursorMove({ section: focusSection, index: selectedIndex }, dx, dy, cursorSections)
      focusSection = r.section
      selectedIndex = r.index
      if (r.action && r.action.type === "adjust") adjustCursor(r.action.delta)
      else if (r.action && r.action.type === "expand") setExpanded(Model.TARGETS[focusSection].section, r.action.value)
    }
    Qt.callLater(function() { root.ensureCursorVisible(root.cursorItem(root.focusSection, root.selectedIndex)) })
  }

  function activateCursor() {
    if (cursorActive) activate(focusSection, selectedIndex)
  }

  function adjustCursor(delta) {
    var a = Model.targetAdjustment(st, focusSection, selectedIndex, delta)
    if (a) sony.sendDebounced(a.action, a.value, root.st)
  }

  function cursorItem(id, index) {
    switch (id) {
    case "switcher": return switcherRow
    case "noise": return noiseRow
    case "ambient": return ambientRow
    case "focusVoice": return focusVoiceToggle
    case "sound": return soundHeader
    case "eqPreset": return eqGrid
    case "dsee": return dseeToggle
    case "connectionQuality": return connectionQualityBlock
    case "customEq": return customEqHeader
    case "eqBands": return bandRepeater.itemAt(index)
    case "speakToChatHeader": return stcHeader
    case "speakToChat": return stcToggle
    case "stcSensitivity": return stcSensitivityBlock
    case "stcTimeout": return stcTimeoutBlock
    case "headset": return headsetHeader
    case "volume": return volumeRow
    case "voiceGuidance": return voiceGuidanceToggle
    case "voiceGuidanceVolume": return guidanceVolumeRow
    case "autoPowerOff": return autoPowerOffBlock
    case "ncAmbButton": return ncAmbBlock
    case "buttonFunction": return buttonFunctionBlock
    case "quickAccessDouble": return quickAccessDoubleRow
    case "quickAccessTriple": return quickAccessTripleRow
    case "earDetection": return earDetectionToggle
    case "touchPanel": return touchPanelToggle
    case "touchFunctionLeft": return touchLeftRow
    case "touchFunctionRight": return touchRightRow
    case "sidetone": return sidetoneToggle
    case "multipointHeader": return multipointHeader
    case "multipoint": return multipointToggle
    case "devices": return deviceRepeater.itemAt(index)
    default: return null
    }
  }

  function ensureCursorVisible(item) {
    if (!item || !panelFlick) return
    var margin = Style.space(6)
    var maxY = Math.max(0, panelFlick.contentHeight - panelFlick.height)
    if (maxY <= 0) return
    var top = item.mapToItem(panelFlick.contentItem, 0, 0).y
    var bottom = top + (item.height || 0)
    if (top < panelFlick.contentY + margin) panelFlick.contentY = Math.max(0, Math.min(maxY, top - margin))
    else if (bottom > panelFlick.contentY + panelFlick.height - margin)
      panelFlick.contentY = Math.max(0, Math.min(maxY, bottom + margin - panelFlick.height))
  }

  onOpenedChanged: {
    if (opened) {
      selectedId = Model.selectionOnOpen(sony.status, selectedId)
      cursorActive = false
      focusSection = ""
      selectedIndex = 0
      if (panelFlick) panelFlick.contentY = 0
      sony.refresh()
      Qt.callLater(function() { keyCatcher.forceActiveFocus() })
    } else {
      sony.flushDebounced()
    }
  }

  IpcHandler {
    target: root.ipcTarget
    function open(): void { root.open() }
    function close(): void { root.close() }
    function show(): void { root.open() }
    function hide(): void { root.close() }
    function toggle(): void { root.toggle() }
    function refresh(): string { sony.refresh(); return "ok" }
    function cycleNoise(): string { sony.cycleNoiseMode(); return sony.status.noiseMode || "unknown" }
  }

  // No connected headset: hide the bar button after a 3 s grace, as the
  // wlmouse widget does (ui-spec 1). Error states keep the button.
  readonly property bool noHeadset: Model.barNoHeadset(sony.status)
  property bool graceExpired: false
  onNoHeadsetChanged: {
    if (noHeadset) graceTimer.restart()
    else {
      graceTimer.stop()
      graceExpired = false
    }
  }
  Component.onCompleted: if (noHeadset) graceTimer.start()

  Timer {
    id: graceTimer
    interval: 3000
    onTriggered: root.graceExpired = true
  }

  visible: !(noHeadset && graceExpired)
  onVisibleChanged: if (!visible && opened) close()
  implicitWidth: visible ? button.implicitWidth : 0
  implicitHeight: visible ? button.implicitHeight : 0

  // ---- bar button ----------------------------------------------------------

  // BarIconButton gives the first-party icon slot: Style.bar.iconSlot along
  // the bar, the bar thickness across it.
  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    hasVisualContent: true
    slotSize: Style.bar.iconSlot
    tooltipText: Model.barTooltip(root.barSt, sony.daemonState)

    readonly property color contentColor: button.active ? button.activeColor : button.foreground

    // One optical canvas centered in the button, as BarIconButton places its
    // canvas, so the glyph box sits where the Audio and Bluetooth glyph boxes
    // sit.
    SonyIcon {
      anchors.centerIn: parent
      kind: root.barSt.kind
      boxSize: Style.bar.iconCanvas
      iconSize: Style.bar.iconFont
      fontFamily: button.fontFamily
      batteryLevel: Model.barBatteryLevel(root.barSt)
      charging: Model.isCharging(root.barSt)
      // effectiveColor picks charging, low battery and disconnected tints.
      normalColor: button.contentColor
    }

    onPressed: function(buttonCode) {
      if (buttonCode === Qt.RightButton) sony.cycleNoiseMode()
      else root.toggle()
    }
  }

  // ---- popup ---------------------------------------------------------------

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened && root.visible
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(380))
    contentHeight: panel.fittedContentHeight(panelColumn.implicitHeight, Style.space(640))

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      onCloseRequested: root.close()
      onMoveRequested: function(dx, dy) { root.moveCursor(dx, dy) }
      onActivateRequested: root.activateCursor()
      onTabRequested: function(direction) { root.switchPanel(direction) }
      // `[` / `]`: previous or next headset, from any target.
      onTextKey: function(text) {
        if (text === "[") root.stepHeadset(-1)
        else if (text === "]") root.stepHeadset(1)
      }

      Flickable {
        id: panelFlick
        anchors.fill: parent
        contentWidth: width
        contentHeight: panelColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        interactive: contentHeight > height
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
          id: panelColumn
          width: panelFlick.width
          spacing: Style.space(12)

          // -- headset switcher (two or more headsets) ------------------------
          Row {
            id: switcherRow
            width: parent.width
            visible: root.hasSwitcher
            spacing: Style.space(6)

            readonly property var tabs: Model.switcherTabs(sony.status, root.shownId)
            readonly property real cellWidth: tabs.length > 0 ? (width - spacing * (tabs.length - 1)) / tabs.length : width

            // An int model, as bandRepeater uses: a status write builds a new
            // tabs array, and an array model would recreate every tab.
            Repeater {
              model: root.hasSwitcher ? Model.headsetCount(sony.status) : 0

              HeadsetTab {
                required property int index
                width: switcherRow.cellWidth
                tabData: switcherRow.tabs[index] || null
                tooltipText: tabData ? tabData.id : ""
                foreground: root.foreground
                fontFamily: root.fontFamily
                hasCursor: root.hasCursor(Model.SWITCHER_TARGET, index)
                onClicked: root.activate(Model.SWITCHER_TARGET, index)
                onHovered: function(h) { if (h) root.setCursor(Model.SWITCHER_TARGET, index) }
              }
            }
          }

          // -- hero -----------------------------------------------------------
          Item {
            width: parent.width
            implicitHeight: Math.max(heroIcon.height, heroLabels.implicitHeight, heroBattery.implicitHeight, heroEarbuds.implicitHeight)

            SonyIcon {
              id: heroIcon
              anchors.left: parent.left
              anchors.verticalCenter: parent.verticalCenter
              kind: root.st.kind
              iconSize: Style.space(28)
              fontFamily: root.fontFamily
              batteryLevel: Model.barBatteryLevel(root.st)
              charging: Model.isCharging(root.st)
            }

            Column {
              id: heroLabels
              anchors.left: heroIcon.right
              anchors.leftMargin: Style.space(12)
              anchors.right: root.earbuds ? heroEarbuds.left : heroBattery.left
              anchors.rightMargin: Style.space(8)
              anchors.verticalCenter: parent.verticalCenter
              spacing: Style.space(2)

              Text {
                width: parent.width
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: Model.deviceTitle(root.st)
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.title
                font.bold: true
              }

              Text {
                width: parent.width
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: Model.heroMeta(root.st).toUpperCase()
                color: Qt.darker(root.foreground, 1.4)
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                font.bold: true
                font.letterSpacing: 1.2
              }
            }

            // Headphones: one level and "Charging".
            Column {
              id: heroBattery
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              spacing: Style.space(2)
              visible: !root.earbuds && root.shownFor("battery")

              Row {
                anchors.right: parent.right
                spacing: Style.space(4)

                Text {
                  anchors.verticalCenter: parent.verticalCenter
                  textFormat: Text.PlainText
                  text: Model.batteryIcon(root.st.batteryLevel, root.st.batteryCharging === true)
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.subtitle
                }

                Text {
                  anchors.verticalCenter: parent.verticalCenter
                  textFormat: Text.PlainText
                  text: Model.formatBattery(root.st.batteryLevel)
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.subtitle
                  font.bold: true
                }
              }

              Text {
                anchors.right: parent.right
                visible: Model.isCharging(root.st)
                textFormat: Text.PlainText
                text: "Charging"
                color: "#2ecc71"
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
              }
            }

            // Earbuds: "L 70%  R 65%" (a low side in the urgent color, a bolt
            // after a charging side), then "Case 53%".
            Column {
              id: heroEarbuds
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              spacing: Style.space(2)
              visible: root.earbuds && (root.shownFor("batteryLr") || root.shownFor("batteryCase"))

              Row {
                anchors.right: parent.right
                spacing: Style.space(10)
                visible: root.shownFor("batteryLr")

                Repeater {
                  model: Model.earbudBatteryParts(root.st)

                  Text {
                    required property var modelData
                    textFormat: Text.PlainText
                    text: modelData.text + (modelData.charging ? " " + Model.GLYPH_BOLT : "")
                    color: modelData.low ? root.urgent : root.foreground
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.subtitle
                    font.bold: true
                  }
                }
              }

              Text {
                anchors.right: parent.right
                visible: root.shownFor("batteryCase")
                textFormat: Text.PlainText
                text: Model.caseBatteryText(root.st) + (Model.caseCharging(root.st) ? " " + Model.GLYPH_BOLT : "")
                color: Model.caseCharging(root.st) ? "#2ecc71" : Qt.darker(root.foreground, 1.4)
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
              }
            }
          }

          // -- status line ----------------------------------------------------
          Text {
            width: parent.width
            visible: text !== ""
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            text: root.statusLine
            color: root.commandErrorText !== "" ? root.urgent : Qt.darker(root.foreground, 1.5)
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
          }

          // -- daemon not installed or older than the widget ------------------
          Column {
            width: parent.width
            visible: sony.daemonNotice !== ""
            spacing: Style.space(6)

            PanelSeparator { foreground: root.foreground }

            Text {
              width: parent.width
              textFormat: Text.PlainText
              wrapMode: Text.WordWrap
              text: sony.daemonNotice
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
            }

            Item {
              width: parent.width
              visible: sony.daemonCommand !== ""
              implicitHeight: Math.max(commandText.implicitHeight, copyButton.implicitHeight)

              Text {
                id: commandText
                anchors.left: parent.left
                anchors.right: copyButton.left
                anchors.rightMargin: Style.space(6)
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                text: sony.daemonCommand
                color: root.foreground
                font.family: "monospace"
                font.pixelSize: Style.font.caption
              }

              Button {
                id: copyButton
                property bool copied: false
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: copied ? "Copied" : "Copy"
                fontSize: Style.font.caption
                foreground: root.foreground
                fontFamily: root.fontFamily
                bordered: true
                horizontalPadding: Style.space(8)
                verticalPadding: Style.space(6)
                onClicked: {
                  sony.copyText(sony.daemonCommand)
                  copied = true
                  copiedTimer.restart()
                }

                Timer {
                  id: copiedTimer
                  interval: 2000
                  onTriggered: copyButton.copied = false
                }
              }
            }
          }

          // -- no controls: error, too new, disconnected ----------------------
          Column {
            width: parent.width
            visible: !root.live && sony.daemonState !== "missing"
            spacing: Style.space(6)

            PanelSeparator { foreground: root.foreground }

            Text {
              width: parent.width
              textFormat: Text.PlainText
              wrapMode: Text.WordWrap
              text: Model.emptyStateText(root.st, sony.daemonState)
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
            }

            Caption {
              text: root.st.ok || root.st.schemaTooNew ? "" : root.st.lastError
              foreground: root.foreground
              fontFamily: root.fontFamily
            }
          }

          // -- noise control --------------------------------------------------
          PanelSeparator {
            visible: noiseSection.visible
            foreground: root.foreground
          }

          Column {
            id: noiseSection
            width: parent.width
            spacing: Style.space(8)
            visible: root.shownFor("noise")

            PanelSectionHeader {
              text: "NOISE CONTROL"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            Row {
              id: noiseRow
              width: parent.width
              spacing: Style.space(6)
              enabled: root.enabledFor("noise")
              opacity: enabled ? 1 : root.disabledOpacity

              readonly property real cellWidth: (width - spacing * (Model.NOISE_MODES.length - 1)) / Model.NOISE_MODES.length

              Repeater {
                model: Model.NOISE_MODES

                Button {
                  required property var modelData
                  required property int index
                  width: noiseRow.cellWidth
                  iconText: Model.noiseModeIcon(modelData)
                  iconSize: Style.font.title
                  text: Model.noiseModeLabel(modelData)
                  fontSize: Style.font.bodySmall
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  bordered: true
                  selected: root.st.noiseMode === modelData
                  hasCursor: root.hasCursor("noise", index)
                  onClicked: root.activate("noise", index)
                  onHovered: function(h) { if (h) root.setCursor("noise", index) }
                }
              }
            }

            SliderRow {
              id: ambientRow
              width: parent.width
              bar: root.bar
              title: "AMBIENT SOUND LEVEL"
              valueText: root.st.ambientSoundLevel === null ? "—" : root.st.ambientSoundLevel + " / 20"
              format: function(v) { return Math.round(v) + " / 20" }
              value: root.st.ambientSoundLevel === null ? Model.AMBIENT_MIN : root.st.ambientSoundLevel
              minimum: Model.AMBIENT_MIN
              maximum: Model.AMBIENT_MAX
              dimmed: root.st.noiseMode !== Model.NOISE_AMBIENT
              foreground: root.foreground
              fontFamily: root.fontFamily
              hasCursor: root.hasCursor("ambient")
              enabled: root.enabledFor("ambient")
              opacity: enabled ? 1 : root.disabledOpacity
              onCommitted: function(v) { root.commit("ambient", v) }
              onHovered: function(h) { if (h) root.setCursor("ambient") }
            }

            Toggle {
              id: focusVoiceToggle
              width: parent.width
              visible: root.shownFor("focusVoice")
              label: "Focus on Voice"
              description: "Lets voices through in Ambient Sound mode"
              checked: root.st.focusOnVoice === true
              hasCursor: root.hasCursor("focusVoice")
              foreground: root.foreground
              fontFamily: root.fontFamily
              enabled: root.enabledFor("focusVoice")
              opacity: enabled ? 1 : root.disabledOpacity
              onClicked: root.activate("focusVoice")
              onHovered: function(h) { if (h) root.setCursor("focusVoice") }
            }
          }

          // -- sound ----------------------------------------------------------
          PanelSeparator {
            visible: soundSection.visible
            foreground: root.foreground
          }

          Column {
            id: soundSection
            width: parent.width
            spacing: Style.space(8)
            visible: Model.sectionVisible(root.st, "sound")

            CollapsibleHeader {
              id: soundHeader
              width: parent.width
              text: "SOUND"
              summary: root.st.eqPreset !== null && root.shownFor("eqPreset") ? Model.eqPresetName(root.st.eqPreset) : ""
              expanded: root.expanded.sound === true
              foreground: root.foreground
              fontFamily: root.fontFamily
              hasCursor: root.hasCursor("sound")
              onToggled: root.activate("sound")
              onHovered: function(h) { if (h) root.setCursor("sound") }
            }

            Column {
              width: parent.width
              spacing: Style.space(8)
              visible: root.expanded.sound === true

              Column {
                width: parent.width
                spacing: Style.space(6)
                visible: root.shownFor("eqPreset")

                PanelSectionHeader {
                  text: "EQUALIZER"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                ChoiceGrid {
                  id: eqGrid
                  width: parent.width
                  columns: 5
                  options: root.choicesFor("eqPreset")
                  value: root.st.eqPreset
                  cursorIndex: root.cursorIndexFor("eqPreset")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  enabled: root.enabledFor("eqPreset")
                  opacity: enabled ? 1 : root.disabledOpacity
                  onPicked: function(i) { root.activate("eqPreset", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("eqPreset", i) }
                }
              }

              Toggle {
                id: dseeToggle
                width: parent.width
                visible: root.shownFor("dsee")
                label: "DSEE Extreme"
                description: "Upscales compressed music"
                checked: root.st.dseeExtreme === true
                hasCursor: root.hasCursor("dsee")
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: root.enabledFor("dsee")
                opacity: enabled ? 1 : root.disabledOpacity
                onClicked: root.activate("dsee")
                onHovered: function(h) { if (h) root.setCursor("dsee") }
              }

              Column {
                id: connectionQualityBlock
                width: parent.width
                spacing: Style.space(6)
                visible: root.shownFor("connectionQuality")

                PanelSectionHeader {
                  text: "CONNECTION PRIORITY"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                ChoiceGrid {
                  width: parent.width
                  columns: 2
                  options: root.choicesFor("connectionQuality")
                  value: root.st.connectionQuality
                  cursorIndex: root.cursorIndexFor("connectionQuality")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  fontSize: Style.font.bodySmall
                  enabled: root.enabledFor("connectionQuality")
                  opacity: enabled ? 1 : root.disabledOpacity
                  onPicked: function(i) { root.activate("connectionQuality", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("connectionQuality", i) }
                }

                Caption {
                  text: root.st.connectionQuality === "other"
                    ? "The headset uses another mode. The headset may reconnect after a change."
                    : "The headset may reconnect after a change."
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }
              }

              Caption {
                visible: root.shown("codec", root.st.codec)
                text: "Codec: " + Model.codecLabel(root.st.codec)
                foreground: root.foreground
                fontFamily: root.fontFamily
              }
            }
          }

          // -- custom EQ ------------------------------------------------------
          PanelSeparator {
            visible: customEqSection.visible
            foreground: root.foreground
          }

          Column {
            id: customEqSection
            width: parent.width
            spacing: Style.space(4)
            visible: Model.sectionVisible(root.st, "customEq")

            CollapsibleHeader {
              id: customEqHeader
              width: parent.width
              text: "CUSTOM EQ"
              summary: Model.eqBandsSummary(root.st)
              expanded: root.expanded.customEq === true
              foreground: root.foreground
              fontFamily: root.fontFamily
              hasCursor: root.hasCursor("customEq")
              onToggled: root.activate("customEq")
              onHovered: function(h) { if (h) root.setCursor("customEq") }
            }

            Column {
              width: parent.width
              spacing: Style.space(2)
              visible: root.expanded.customEq === true
              enabled: root.enabledFor("eqBands")
              opacity: enabled ? 1 : root.disabledOpacity

              Repeater {
                id: bandRepeater
                model: 6

                BandRow {
                  required property int index
                  width: parent.width
                  bar: root.bar
                  label: index < 5 ? Model.EQ_BAND_LABELS[index] : "Clear Bass"
                  value: {
                    var s = root.st
                    if (index === 5) return s.clearBass === null ? 0 : s.clearBass
                    return Array.isArray(s.eqCustomBands) ? s.eqCustomBands[index] : 0
                  }
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  hasCursor: root.hasCursor("eqBands", index)
                  onCommitted: function(v) { root.commit("eqBands", { index: index, level: v }) }
                  onHovered: function(h) { if (h) root.setCursor("eqBands", index) }
                }
              }

              Caption {
                text: "A change selects the Custom preset."
                foreground: root.foreground
                fontFamily: root.fontFamily
              }
            }
          }

          // -- speak-to-chat --------------------------------------------------
          PanelSeparator {
            visible: stcSection.visible
            foreground: root.foreground
          }

          Column {
            id: stcSection
            width: parent.width
            spacing: Style.space(8)
            visible: Model.sectionVisible(root.st, "speakToChat")

            CollapsibleHeader {
              id: stcHeader
              width: parent.width
              text: "SPEAK-TO-CHAT"
              summary: root.st.speakToChat === null ? "" : Model.onOffLabel(root.st.speakToChat)
              expanded: root.expanded.speakToChat === true
              foreground: root.foreground
              fontFamily: root.fontFamily
              hasCursor: root.hasCursor("speakToChatHeader")
              onToggled: root.activate("speakToChatHeader")
              onHovered: function(h) { if (h) root.setCursor("speakToChatHeader") }
            }

            Column {
              width: parent.width
              spacing: Style.space(8)
              visible: root.expanded.speakToChat === true

              Toggle {
                id: stcToggle
                width: parent.width
                label: "Speak-to-Chat"
                description: "Pauses music when you start talking"
                checked: root.st.speakToChat === true
                hasCursor: root.hasCursor("speakToChat")
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: root.enabledFor("speakToChat")
                opacity: enabled ? 1 : root.disabledOpacity
                onClicked: root.activate("speakToChat")
                onHovered: function(h) { if (h) root.setCursor("speakToChat") }
              }

              Column {
                id: stcSensitivityBlock
                width: parent.width
                visible: root.shownFor("stcSensitivity")
                spacing: Style.space(6)
                enabled: root.enabledFor("stcSensitivity")
                opacity: enabled ? 1 : root.disabledOpacity

                PanelSectionHeader {
                  text: "VOICE DETECTION"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                ChoiceGrid {
                  width: parent.width
                  columns: 3
                  options: root.choicesFor("stcSensitivity")
                  value: root.st.stcSensitivity
                  cursorIndex: root.cursorIndexFor("stcSensitivity")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  fontSize: Style.font.bodySmall
                  onPicked: function(i) { root.activate("stcSensitivity", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("stcSensitivity", i) }
                }
              }

              Column {
                id: stcTimeoutBlock
                width: parent.width
                visible: root.shownFor("stcTimeout")
                spacing: Style.space(6)
                enabled: root.enabledFor("stcTimeout")
                opacity: enabled ? 1 : root.disabledOpacity

                PanelSectionHeader {
                  text: "END AFTER SILENCE"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                ChoiceGrid {
                  width: parent.width
                  columns: 4
                  options: root.choicesFor("stcTimeout")
                  value: root.st.stcTimeout
                  cursorIndex: root.cursorIndexFor("stcTimeout")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  fontSize: Style.font.bodySmall
                  onPicked: function(i) { root.activate("stcTimeout", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("stcTimeout", i) }
                }

                Caption {
                  text: root.st.stcTimeout === "off" ? "Speak-to-Chat stays on until you press a button." : ""
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }
              }
            }
          }

          // -- headset --------------------------------------------------------
          PanelSeparator {
            visible: headsetSection.visible
            foreground: root.foreground
          }

          Column {
            id: headsetSection
            width: parent.width
            spacing: Style.space(8)
            visible: Model.sectionVisible(root.st, "headset")

            CollapsibleHeader {
              id: headsetHeader
              width: parent.width
              text: "HEADSET"
              summary: root.st.volume !== null ? "Volume " + root.st.volume : ""
              expanded: root.expanded.headset === true
              foreground: root.foreground
              fontFamily: root.fontFamily
              hasCursor: root.hasCursor("headset")
              onToggled: root.activate("headset")
              onHovered: function(h) { if (h) root.setCursor("headset") }
            }

            Column {
              width: parent.width
              spacing: Style.space(8)
              visible: root.expanded.headset === true

              SliderRow {
                id: volumeRow
                width: parent.width
                visible: root.shownFor("volume")
                bar: root.bar
                title: "VOLUME"
                valueText: Model.formatValue(root.st.volume, " / 30")
                format: function(v) { return Math.round(v) + " / 30" }
                value: root.st.volume === null ? 0 : root.st.volume
                minimum: Model.VOLUME_MIN
                maximum: Model.VOLUME_MAX
                foreground: root.foreground
                fontFamily: root.fontFamily
                hasCursor: root.hasCursor("volume")
                enabled: root.enabledFor("volume")
                opacity: enabled ? 1 : root.disabledOpacity
                onCommitted: function(v) { root.commit("volume", v) }
                onHovered: function(h) { if (h) root.setCursor("volume") }
              }

              Toggle {
                id: voiceGuidanceToggle
                width: parent.width
                visible: root.shownFor("voiceGuidance")
                label: "Voice guidance"
                description: "Spoken battery and mode notices"
                checked: root.st.voiceGuidance === true
                hasCursor: root.hasCursor("voiceGuidance")
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: root.enabledFor("voiceGuidance")
                opacity: enabled ? 1 : root.disabledOpacity
                onClicked: root.activate("voiceGuidance")
                onHovered: function(h) { if (h) root.setCursor("voiceGuidance") }
              }

              SliderRow {
                id: guidanceVolumeRow
                width: parent.width
                visible: root.shownFor("voiceGuidanceVolume")
                bar: root.bar
                title: "VOICE GUIDANCE VOLUME"
                valueText: Model.formatSigned(root.st.voiceGuidanceVolume)
                format: function(v) { return Model.formatSigned(v) }
                value: root.st.voiceGuidanceVolume === null ? 0 : root.st.voiceGuidanceVolume
                minimum: Model.GUIDANCE_VOLUME_MIN
                maximum: Model.GUIDANCE_VOLUME_MAX
                foreground: root.foreground
                fontFamily: root.fontFamily
                hasCursor: root.hasCursor("voiceGuidanceVolume")
                enabled: root.enabledFor("voiceGuidanceVolume")
                opacity: enabled ? 1 : root.disabledOpacity
                onCommitted: function(v) { root.commit("voiceGuidanceVolume", v) }
                onHovered: function(h) { if (h) root.setCursor("voiceGuidanceVolume") }
              }

              Column {
                id: autoPowerOffBlock
                width: parent.width
                spacing: Style.space(6)
                visible: root.shownFor("autoPowerOff")
                enabled: root.enabledFor("autoPowerOff")
                opacity: enabled ? 1 : root.disabledOpacity

                PanelSectionHeader {
                  text: "AUTO POWER OFF"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                ChoiceGrid {
                  width: parent.width
                  columns: 4
                  options: root.choicesFor("autoPowerOff")
                  value: root.st.autoPowerOff
                  cursorIndex: root.cursorIndexFor("autoPowerOff")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onPicked: function(i) { root.activate("autoPowerOff", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("autoPowerOff", i) }
                }
              }

              Column {
                id: ncAmbBlock
                width: parent.width
                spacing: Style.space(6)
                visible: root.shownFor("ncAmbButton")
                enabled: root.enabledFor("ncAmbButton")
                opacity: enabled ? 1 : root.disabledOpacity

                PanelSectionHeader {
                  text: "NC/AMB BUTTON CYCLE"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                ChoiceGrid {
                  width: parent.width
                  columns: 4
                  options: root.choicesFor("ncAmbButton")
                  value: root.st.ncAmbButton
                  cursorIndex: root.cursorIndexFor("ncAmbButton")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onPicked: function(i) { root.activate("ncAmbButton", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("ncAmbButton", i) }
                }
              }

              Column {
                id: buttonFunctionBlock
                width: parent.width
                spacing: Style.space(6)
                visible: root.shownFor("buttonFunction")
                enabled: root.enabledFor("buttonFunction")
                opacity: enabled ? 1 : root.disabledOpacity

                PanelSectionHeader {
                  text: "NC/AMB BUTTON FUNCTION"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                ChoiceGrid {
                  width: parent.width
                  columns: 3
                  options: root.choicesFor("buttonFunction")
                  value: root.st.buttonFunction
                  cursorIndex: root.cursorIndexFor("buttonFunction")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onPicked: function(i) { root.activate("buttonFunction", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("buttonFunction", i) }
                }

                Caption {
                  text: root.st.buttonFunction === "other" ? "The button uses a function this panel does not name." : ""
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }
              }

              // Quick Access: one chip row per key press (double, triple).
              Column {
                width: parent.width
                spacing: Style.space(6)
                visible: root.shownFor("quickAccessDouble") || root.shownFor("quickAccessTriple")

                PanelSectionHeader {
                  text: "QUICK ACCESS"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                PressRow {
                  id: quickAccessDoubleRow
                  width: parent.width
                  label: "Double tap"
                  visible: root.shownFor("quickAccessDouble")
                  enabled: root.enabledFor("quickAccessDouble")
                  opacity: enabled ? 1 : root.disabledOpacity
                  options: root.choicesFor("quickAccessDouble")
                  value: root.st.quickAccessDoubleTap
                  cursorIndex: root.cursorIndexFor("quickAccessDouble")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onPicked: function(i) { root.activate("quickAccessDouble", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("quickAccessDouble", i) }
                }

                PressRow {
                  id: quickAccessTripleRow
                  width: parent.width
                  label: "Triple tap"
                  visible: root.shownFor("quickAccessTriple")
                  enabled: root.enabledFor("quickAccessTriple")
                  opacity: enabled ? 1 : root.disabledOpacity
                  options: root.choicesFor("quickAccessTriple")
                  value: root.st.quickAccessTripleTap
                  cursorIndex: root.cursorIndexFor("quickAccessTriple")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onPicked: function(i) { root.activate("quickAccessTriple", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("quickAccessTriple", i) }
                }

                Caption {
                  text: Model.quickAccessOtherText(root.st)
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }
              }

              Toggle {
                id: earDetectionToggle
                width: parent.width
                visible: root.shownFor("earDetection")
                label: "Pause when taken off"
                description: "Resumes when you put the headset on"
                checked: root.st.earDetection === true
                hasCursor: root.hasCursor("earDetection")
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: root.enabledFor("earDetection")
                opacity: enabled ? 1 : root.disabledOpacity
                onClicked: root.activate("earDetection")
                onHovered: function(h) { if (h) root.setCursor("earDetection") }
              }

              Toggle {
                id: touchPanelToggle
                width: parent.width
                visible: root.shownFor("touchPanel")
                label: "Touch controls"
                description: "Tap and swipe the ear cup to play, skip and change the volume"
                checked: root.st.touchPanel === true
                hasCursor: root.hasCursor("touchPanel")
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: root.enabledFor("touchPanel")
                opacity: enabled ? 1 : root.disabledOpacity
                onClicked: root.activate("touchPanel")
                onHovered: function(h) { if (h) root.setCursor("touchPanel") }
              }

              // Earbud touch sensor presets: one chip row per earbud.
              Column {
                width: parent.width
                spacing: Style.space(6)
                // Bind to the targets, not to the rows' `visible`: a child of a
                // hidden item reads as hidden, so that binding never recovers.
                visible: root.shownFor("touchFunctionLeft") || root.shownFor("touchFunctionRight")

                PanelSectionHeader {
                  text: "TOUCH FUNCTIONS"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                PressRow {
                  id: touchLeftRow
                  width: parent.width
                  label: "Left earbud"
                  labelWidth: Style.space(76)
                  visible: root.shownFor("touchFunctionLeft")
                  enabled: root.enabledFor("touchFunctionLeft")
                  opacity: enabled ? 1 : root.disabledOpacity
                  options: root.choicesFor("touchFunctionLeft")
                  value: root.st.touchFunctionLeft
                  cursorIndex: root.cursorIndexFor("touchFunctionLeft")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onPicked: function(i) { root.activate("touchFunctionLeft", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("touchFunctionLeft", i) }
                }

                PressRow {
                  id: touchRightRow
                  width: parent.width
                  label: "Right earbud"
                  labelWidth: Style.space(76)
                  visible: root.shownFor("touchFunctionRight")
                  enabled: root.enabledFor("touchFunctionRight")
                  opacity: enabled ? 1 : root.disabledOpacity
                  options: root.choicesFor("touchFunctionRight")
                  value: root.st.touchFunctionRight
                  cursorIndex: root.cursorIndexFor("touchFunctionRight")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onPicked: function(i) { root.activate("touchFunctionRight", i) }
                  onHoveredIndex: function(i, h) { if (h) root.setCursor("touchFunctionRight", i) }
                }

                Caption {
                  text: "What a tap, double tap and hold on each earbud do."
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                Caption {
                  text: Model.touchFunctionOtherText(root.st)
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }
              }

              Toggle {
                id: sidetoneToggle
                width: parent.width
                visible: root.shownFor("sidetone")
                label: "Hear your voice in calls"
                description: "Plays your own voice in the earbuds during a call"
                checked: root.st.sidetone === true
                hasCursor: root.hasCursor("sidetone")
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: root.enabledFor("sidetone")
                opacity: enabled ? 1 : root.disabledOpacity
                onClicked: root.activate("sidetone")
                onHovered: function(h) { if (h) root.setCursor("sidetone") }
              }
            }
          }

          // -- multipoint -----------------------------------------------------
          PanelSeparator {
            visible: multipointSection.visible
            foreground: root.foreground
          }

          Column {
            id: multipointSection
            width: parent.width
            spacing: Style.space(8)
            visible: Model.sectionVisible(root.st, "multipoint")

            CollapsibleHeader {
              id: multipointHeader
              width: parent.width
              text: "MULTIPOINT"
              summary: root.st.multipoint === null ? "" : Model.onOffLabel(root.st.multipoint)
              expanded: root.expanded.multipoint === true
              foreground: root.foreground
              fontFamily: root.fontFamily
              hasCursor: root.hasCursor("multipointHeader")
              onToggled: root.activate("multipointHeader")
              onHovered: function(h) { if (h) root.setCursor("multipointHeader") }
            }

            Column {
              width: parent.width
              spacing: Style.space(6)
              visible: root.expanded.multipoint === true

              Toggle {
                id: multipointToggle
                width: parent.width
                visible: root.shownFor("multipoint")
                label: "Connect to 2 devices"
                description: "The headset reconnects after this change"
                checked: root.st.multipoint === true
                hasCursor: root.hasCursor("multipoint")
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: root.enabledFor("multipoint")
                opacity: enabled ? 1 : root.disabledOpacity
                onClicked: root.activate("multipoint")
                onHovered: function(h) { if (h) root.setCursor("multipoint") }
              }

              Column {
                width: parent.width
                spacing: Style.space(2)
                visible: root.shown("devices", root.st.devices)
                enabled: root.enabledFor("devices")
                opacity: enabled ? 1 : root.disabledOpacity

                PanelSectionHeader {
                  text: "PAIRED DEVICES"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                Repeater {
                  id: deviceRepeater
                  model: Array.isArray(root.st.devices) ? root.st.devices : []

                  DeviceRow {
                    required property var modelData
                    required property int index
                    width: parent.width
                    device: modelData
                    canSwitch: Model.canSwitchTo(root.st, modelData)
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    hasCursor: root.hasCursor("devices", index)
                    onClicked: root.activate("devices", index)
                    onHovered: function(h) { if (h) root.setCursor("devices", index) }
                  }
                }

                Caption {
                  text: Array.isArray(root.st.devices) && root.st.devices.length === 0
                    ? "No paired devices."
                    : (Model.isSupported(root.st, "source_switch") ? "Select a connected device to play audio from it." : "")
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }
              }
            }
          }

          // -- info footer ------------------------------------------------------
          PanelSeparator { foreground: root.foreground }

          Column {
            id: infoFooter
            width: parent.width
            spacing: Style.space(6)

            Column {
              width: parent.width
              spacing: Style.space(2)
              visible: footerRepeater.count > 0

              Repeater {
                id: footerRepeater
                model: Model.footerLines(root.st)

                Caption {
                  required property var modelData
                  text: String(modelData)
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }
              }
            }
          }
        }
      }
    }
  }
}
