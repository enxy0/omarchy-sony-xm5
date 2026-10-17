// Service.qml
// Headset state for the bar widget: watches status.json (schema 3, also 1
// and 2), keeps optimistic values per headset and runs sony-ctl one command
// at a time. All decisions live in Model.js; this file only wires timers and
// processes.
import QtQuick
import Quickshell
import Quickshell.Io
import "Model.js" as Model

Item {
  id: root

  // sony-ctl as setup installs it (~/.local/bin) or as the Arch package does
  // (/usr/bin); daemonProbe finds which one exists.
  readonly property string cliBinary: {
    if (daemonProbe && daemonProbe.path) return daemonProbe.path
    var home = Quickshell.env("HOME")
    return home ? (home + "/.local/bin/sony-ctl") : "sony-ctl"
  }

  // The plugin directory; `setup` sits there when the widget came from
  // `omarchy plugin add`.
  readonly property string pluginDir: {
    var url = Qt.resolvedUrl(".").toString()
    url = url.indexOf("file://") === 0 ? decodeURIComponent(url.substring(7)) : url
    return url.length > 1 && url.charAt(url.length - 1) === "/" ? url.substring(0, url.length - 1) : url
  }
  // pluginDir with $HOME as ~: shorter in the notice, and the shell expands
  // it when the copied command is pasted.
  readonly property string pluginDirShort: {
    var home = Quickshell.env("HOME")
    return home && pluginDir.indexOf(home + "/") === 0 ? "~" + pluginDir.substring(home.length) : pluginDir
  }

  // { path, version } of the installed sony-ctl, path null when none is
  // installed; null until the first lookup finishes.
  property var daemonProbe: null
  property string widgetVersion: ""
  readonly property string daemonState: Model.daemonState(realStatus, daemonProbe, widgetVersion)
  readonly property string daemonCommand: Model.daemonCommand(daemonState, daemonProbe, pluginDirShort)
  readonly property string daemonNotice: Model.daemonNotice(daemonState, realStatus, daemonProbe, widgetVersion)

  function copyText(text) {
    if (text) Quickshell.execDetached(["wl-copy", "--", text])
  }

  function probeDaemon() {
    if (!probeProcess.running) probeProcess.running = true
  }

  Process {
    id: probeProcess
    running: false
    command: ["sh", "-c",
      'for p in "$HOME/.local/bin/sony-ctl" /usr/bin/sony-ctl; do ' +
      'if [ -x "$p" ]; then echo "$p"; "$p" --version; exit 0; fi; done; exit 1']
    stdout: StdioCollector { id: probeOut; waitForEnd: true }
    onExited: function(exitCode, exitStatus) {
      var lines = String(probeOut.text).trim().split("\n")
      root.daemonProbe = exitCode === 0 && lines[0]
        ? { path: lines[0], version: lines.length > 1 ? lines[1].replace(/^sony-ctl /, "") : null }
        : { path: null, version: null }
    }
  }

  FileView {
    id: manifestView
    path: root.pluginDir + "/manifest.json"
    printErrors: false
    onLoaded: {
      try {
        var m = JSON.parse(typeof manifestView.text === "function" ? manifestView.text() : manifestView.text)
        root.widgetVersion = typeof m.version === "string" ? m.version : ""
      } catch (_e) {
        root.widgetVersion = ""
      }
    }
  }

  // $XDG_STATE_HOME/sony-headphones/status.json, else ~/.local/state/...
  readonly property string statusPath: {
    var xdg = Quickshell.env("XDG_STATE_HOME")
    var home = Quickshell.env("HOME")
    var base = xdg ? xdg : (home ? home + "/.local/state" : "/tmp")
    return base + "/sony-headphones/status.json"
  }

  // Parsed status.json as the daemon wrote it: the view of the primary
  // headset, with every headset in `headsets`.
  property var realStatus: Model.parseStatus("")
  // Optimistic values: "<headset>|<field>" -> { value, id, headset, field }.
  property var pending: ({})
  // What the UI shows: the real status with the optimistic values applied.
  readonly property var status: Model.applyPending(realStatus, pending)

  // Queued commands: [{ args, id, action, headset }]. One process runs at a
  // time.
  property var queue: []
  property var runningCommand: null
  property int _nextId: 1
  readonly property bool busy: runningCommand !== null || queue.length > 0
  // True once a command has been in flight for 300 ms ("Applying…").
  property bool busyVisible: false
  property string commandError: ""
  // The headset the error belongs to; the popup shows it only for that one.
  property var commandErrorHeadset: null

  // True while a command for headset `id` waits or runs.
  function busyFor(id) {
    if (runningCommand !== null && runningCommand.headset === id) return true
    if (_debounced !== null && _debounced.headset === id) return true
    for (var i = 0; i < queue.length; i++) if (queue[i].headset === id) return true
    return false
  }

  // The error text for headset `id`, or "".
  function errorFor(id) {
    return commandError !== "" && (commandErrorHeadset === null || commandErrorHeadset === id) ? commandError : ""
  }

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
    onTriggered: {
      root.commandError = ""
      root.commandErrorHeadset = null
    }
  }

  Timer {
    id: debounceTimer
    interval: 400
    onTriggered: root.flushDebounced()
  }

  // Builds a command for the headset of `view` (default: the primary one)
  // and shows its optimistic values. Returns the queue item, or null after
  // showing the error.
  function prepare(action, value, view) {
    var target = view ? view : status
    var built = Model.buildCommand(action, value, target)
    if (built.error) {
      showError(built.error, target ? target.id : null)
      return null
    }
    var id = _nextId++
    pending = Model.addPending(pending, built.pending, id, built.headset)
    return { args: built.args, id: id, action: action, headset: built.headset }
  }

  // Validates and sends one change. Returns true when the command was queued.
  function send(action, value, view) {
    flushDebounced()
    var item = prepare(action, value, view)
    if (item === null) return false
    enqueue(item)
    return true
  }

  // Shows the value now and sends it after a 400 ms pause. A step for
  // another action or another headset sends the waiting one first.
  function sendDebounced(action, value, view) {
    var target = view ? view : status
    if (_debounced !== null && (_debounced.action !== action || _debounced.headset !== target.id)) flushDebounced()
    var previous = _debounced
    var item = prepare(action, value, target)
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

  function showError(message, headset) {
    commandError = Model.elideError(message)
    commandErrorHeadset = headset === undefined ? null : headset
    errorTimer.restart()
  }

  // `didNotStart`: the watchdog found no process; the collectors hold
  // output of an older run then, so ignore them.
  function finishCommand(exitCode, didNotStart) {
    var item = runningCommand
    if (item === null) return
    runningCommand = null
    watchdog.stop()
    // A command whose headset left `headsets` finishes; its result is
    // ignored (ui-spec 5).
    var stillThere = !realStatus.ok || Model.findHeadset(realStatus, item.headset) !== null
    if (exitCode !== 0) {
      pending = Model.dropPending(pending, item.id)
      if (stillThere) {
        showError(didNotStart
          ? Model.commandErrorText(127, "", "")
          : Model.commandErrorText(exitCode, ctlStderr.text, ctlStdout.text), item.headset)
      }
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

  // A new file: drop the optimistic values the daemon now reports, and the
  // queued commands and values of headsets that left `headsets`.
  function applyStatus(raw) {
    var parsed = Model.parseStatus(raw)
    var next = Model.reconcilePending(pending, parsed)
    var pruned = Model.pruneQueue(queue, parsed)
    for (var i = 0; i < pruned.dropped.length; i++) next = Model.dropPending(next, pruned.dropped[i])
    if (_debounced !== null && parsed.ok && Model.findHeadset(parsed, _debounced.headset) === null) {
      debounceTimer.stop()
      next = Model.dropPending(next, _debounced.id)
      _debounced = null
    }
    queue = pruned.queue
    pending = next
    // A daemon that appeared, stopped or changed version: look for sony-ctl
    // again, so the install notice follows a setup run while the shell runs.
    var recheck = parsed.ok !== realStatus.ok || parsed.daemonVersion !== realStatus.daemonVersion
    realStatus = parsed
    if (recheck) probeDaemon()
  }

  // Right click on the bar button: the next noise mode of the primary
  // headset.
  function cycleNoiseMode() {
    var s = status
    if (!Model.isEnabled(s, "noise_control", s.noiseMode)) return
    send("noise", Model.cycleNoiseMode(s.noiseMode), s)
  }

  function refresh() {
    fileView.reload()
  }

  Component.onCompleted: {
    fileView.reload()
    probeDaemon()
  }
}
