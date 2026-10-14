// Service.qml
// Headset state for the bar widget: watches status.json (schema 2), keeps
// optimistic values and runs sony-ctl one command at a time.
// All decisions live in Model.js; this file only wires timers and processes.
import QtQuick
import Quickshell
import Quickshell.Io
import "Model.js" as Model

Item {
  id: root

  property var settings: ({})

  readonly property string cliBinary: {
    var home = Quickshell.env("HOME")
    return home ? (home + "/.local/bin/sony-ctl") : "sony-ctl"
  }

  // $XDG_STATE_HOME/sony-headphones/status.json, else ~/.local/state/...
  readonly property string statusPath: {
    var xdg = Quickshell.env("XDG_STATE_HOME")
    var home = Quickshell.env("HOME")
    var base = xdg ? xdg : (home ? home + "/.local/state" : "/tmp")
    return base + "/sony-headphones/status.json"
  }

  // Parsed status.json as the daemon wrote it.
  property var realStatus: Model.parseStatus("")
  // Optimistic values: field -> { value, id }.
  property var pending: ({})
  // What the UI shows: the real status with the optimistic values applied.
  readonly property var status: Model.applyPending(realStatus, pending)

  // Queued commands: [{ args, id }]. One process runs at a time.
  property var queue: []
  property var runningCommand: null
  property int _nextId: 1
  readonly property bool busy: runningCommand !== null || queue.length > 0
  // True once a command has been in flight for 300 ms ("Applying…").
  property bool busyVisible: false
  property string commandError: ""

  // A keyboard step waits here for 400 ms so a held key sends one command.
  property var _debounced: null

  onBusyChanged: {
    if (busy) busyDelay.restart()
    else {
      busyDelay.stop()
      busyVisible = false
    }
  }

  Timer {
    id: busyDelay
    interval: 300
    onTriggered: root.busyVisible = root.busy
  }

  // Optimistic values expire 4 s after the last command finished.
  Timer {
    id: settleTimer
    interval: 4000
    onTriggered: {
      if (root.busy || root._debounced !== null) restart()
      else root.pending = ({})
    }
  }

  Timer {
    id: errorTimer
    interval: 6000
    onTriggered: root.commandError = ""
  }

  Timer {
    id: debounceTimer
    interval: 400
    onTriggered: root.flushDebounced()
  }

  // Builds a command and shows its optimistic values. Returns the queue
  // item, or null after showing the error.
  function prepare(action, value) {
    var built = Model.buildCommand(action, value, status)
    if (built.error) {
      showError(built.error)
      return null
    }
    var id = _nextId++
    pending = Model.addPending(pending, built.pending, id)
    return { args: built.args, id: id, action: action }
  }

  // Validates and sends one change. Returns true when the command was queued.
  function send(action, value) {
    flushDebounced()
    var item = prepare(action, value)
    if (item === null) return false
    enqueue(item)
    return true
  }

  // Shows the value now and sends it after a 400 ms pause. A step for
  // another action sends the waiting one first.
  function sendDebounced(action, value) {
    if (_debounced !== null && _debounced.action !== action) flushDebounced()
    var previous = _debounced
    var item = prepare(action, value)
    if (item === null) return false
    // Fields the new step overwrote already carry its id.
    if (previous !== null) pending = Model.dropPending(pending, previous.id)
    _debounced = item
    debounceTimer.restart()
    settleTimer.restart()
    return true
  }

  function flushDebounced() {
    debounceTimer.stop()
    if (_debounced === null) return
    var item = _debounced
    _debounced = null
    enqueue(item)
  }

  function enqueue(item) {
    var r = Model.enqueueCommand(queue, item)
    var next = pending
    for (var i = 0; i < r.dropped.length; i++) next = Model.dropPending(next, r.dropped[i])
    pending = next
    queue = r.queue
    settleTimer.restart()
    dispatchNext()
  }

  function dispatchNext() {
    if (runningCommand !== null || queue.length === 0) return
    var item = queue[0]
    queue = queue.slice(1)
    runningCommand = item
    _runTicks = 0
    _idleTicks = 0
    watchdog.restart()
    ctlProcess.command = [cliBinary].concat(item.args)
    ctlProcess.running = true
  }

  function showError(message) {
    commandError = Model.elideError(message)
    errorTimer.restart()
  }

  // `didNotStart`: the watchdog found no process; the collectors hold
  // output of an older run then, so ignore them.
  function finishCommand(exitCode, didNotStart) {
    var item = runningCommand
    if (item === null) return
    runningCommand = null
    watchdog.stop()
    if (exitCode !== 0) {
      pending = Model.dropPending(pending, item.id)
      showError(didNotStart
        ? Model.commandErrorText(127, "", "")
        : Model.commandErrorText(exitCode, ctlStderr.text, ctlStdout.text))
    }
    settleTimer.restart()
    // Start the next process outside the exited handler of this one.
    Qt.callLater(root.dispatchNext)
  }

  // Watchdog. Without Quickshell sources at hand it is not certain that
  // `exited` fires when the binary cannot start, so a command that is not
  // running for 2 s without an exit is finished as "did not start". A
  // process still running after 10 s (the CLI times out at 2 s) is stopped;
  // its exit then finishes the command.
  property int _runTicks: 0
  property int _idleTicks: 0

  Timer {
    id: watchdog
    interval: 1000
    repeat: true
    onTriggered: {
      if (root.runningCommand === null) {
        stop()
        return
      }
      if (ctlProcess.running) {
        root._runTicks++
        if (root._runTicks >= 10) ctlProcess.running = false
      } else {
        root._idleTicks++
        if (root._idleTicks >= 2) root.finishCommand(127, true)
      }
    }
  }

  Process {
    id: ctlProcess
    running: false
    stdout: StdioCollector { id: ctlStdout; waitForEnd: true }
    stderr: StdioCollector { id: ctlStderr; waitForEnd: true }
    onExited: function(exitCode, exitStatus) { root.finishCommand(exitCode, false) }
  }

  // Reactive file watcher; no polling.
  FileView {
    id: fileView
    path: root.statusPath
    watchChanges: true
    atomicWrites: true
    printErrors: false
    onLoaded: root.applyStatus(typeof fileView.text === "function" ? fileView.text() : fileView.text)
    onLoadFailed: root.applyStatus("")
    onFileChanged: reload()
  }

  function applyStatus(raw) {
    var parsed = Model.parseStatus(raw)
    pending = Model.reconcilePending(pending, parsed)
    realStatus = parsed
  }

  function cycleNoiseMode() {
    var s = status
    if (!Model.isEnabled(s, "noise_control", s.noiseMode)) return
    send("noise", Model.cycleNoiseMode(s.noiseMode))
  }

  function refresh() {
    fileView.reload()
  }

  Component.onCompleted: fileView.reload()
}
