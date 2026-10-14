#!/usr/bin/env python3
"""
tests/mock_daemon.py — Python stand-in for sony-headphones-daemon (status schema 3).

Used by tests/integration.sh when the daemon binary is not built. It follows
docs/backend-contract.md: status.json schema 3 (`headsets`, `primary`), the
`@<selector>` request token, the sony-ctl verbs and their ERR texts.

Profiles (--mock-profile): xm5 (one WH-1000XM5, every headphone feature),
both (a WH-1000XM5 and a WF-1000XM5). Other names fall back to xm5.

Signals:
    SIGTERM / SIGINT: clean shutdown (unlinks socket and status.json)
    SIGUSR1: simulate disconnect of every headset (headsets: [])
    SIGUSR2: simulate reconnect
"""

import argparse
import copy
import json
import os
import re
import select
import signal
import socket
import sys
import time

ON_OFF = ["on", "off"]
EQ_PRESETS = ["off", "bright", "excited", "mellow", "relaxed", "vocal", "treble", "bass", "speech", "custom"]
EQ_ALL = EQ_PRESETS + ["user1", "user2", "user3", "user4", "user5"]
APO = ["off", "5min", "15min", "30min", "60min", "180min", "when_taken_off"]
NC_AMB = ["nc_ambient_off", "nc_ambient", "nc_off", "ambient_off"]
BUTTON_ALL = ["ambient_sound_control", "voice_recognition", "google_assistant", "amazon_alexa", "quick_access", "none"]
BUTTON_DEVICE = ["ambient_sound_control", "google_assistant", "amazon_alexa", "quick_access"]
TOUCH_ALL = ["ambient_sound_control", "playback_control", "volume_control", "voice_recognition",
             "google_assistant", "amazon_alexa", "quick_access", "none"]
TOUCH_DEVICE = ["ambient_sound_control", "playback_control", "none"]
ADDRESS = re.compile(r"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")
ADAPTER = "F0:1D:BC:AA:00:01"
WH_ID = "AC:80:0A:12:34:56"
WF_ID = "AC:80:0A:65:43:21"

FEATURES = ["battery", "battery_lr", "battery_case", "noise_control", "eq_preset", "eq_custom", "dsee",
            "speak_to_chat", "voice_guidance", "voice_guidance_volume", "auto_power_off", "nc_amb_button",
            "button_function", "touch_function", "ear_detection", "worn", "volume", "connection_quality", "codec",
            "multipoint", "devices", "source_switch", "touch_panel", "sidetone", "quick_access"]

USAGE = {
    "noise": "noise anc|ambient|off",
    "ambient-level": "ambient-level <n>",
    "focus-voice": "focus-voice on|off",
    "eq": "eq <preset>",
    "eq custom": "eq custom <b1> <b2> <b3> <b4> <b5> <clear_bass>",
    "dsee": "dsee on|off",
    "speak-to-chat": "speak-to-chat on|off",
    "speak-to-chat-config": "speak-to-chat-config <sensitivity> <timeout>",
    "voice-guidance": "voice-guidance on|off",
    "voice-guidance-volume": "voice-guidance-volume <n>",
    "auto-power-off": "auto-power-off <value>",
    "nc-amb-button": "nc-amb-button <value>",
    "button-function": "button-function <value>",
    "touch-function": "touch-function left|right <value>",
    "ear-detect": "ear-detect on|off",
    "volume": "volume <n>",
    "connection-quality": "connection-quality sound|connection",
    "multipoint": "multipoint on|off",
    "source": "source <address>",
    "touch-panel": "touch-panel on|off",
    "sidetone": "sidetone on|off",
    "quick-access": "quick-access double|triple <value>",
}

FEATURE_OF = {
    "noise": "noise_control", "ambient-level": "noise_control", "focus-voice": "noise_control",
    "eq": "eq_preset", "eq custom": "eq_custom", "dsee": "dsee", "speak-to-chat": "speak_to_chat",
    "speak-to-chat-config": "speak_to_chat", "voice-guidance": "voice_guidance",
    "voice-guidance-volume": "voice_guidance_volume", "auto-power-off": "auto_power_off",
    "nc-amb-button": "nc_amb_button", "button-function": "button_function", "touch-function": "touch_function",
    "ear-detect": "ear_detection", "volume": "volume", "connection-quality": "connection_quality",
    "multipoint": "multipoint", "source": "source_switch", "touch-panel": "touch_panel", "sidetone": "sidetone",
    "quick-access": "quick_access",
}


def parse_int(text, lo, hi):
    if not re.fullmatch(r"-?[0-9]+", text):
        return None
    value = int(text)
    return value if lo <= value <= hi else None


def base_headset(hid, model, since):
    entry = {k: None for k in [
        "battery_level", "battery_charging", "battery_left", "battery_left_charging", "battery_right",
        "battery_right_charging", "battery_case", "battery_case_charging", "noise_mode", "ambient_sound_level",
        "voice_passthrough", "eq_preset", "eq_presets", "eq_custom_bands", "clear_bass", "dsee_extreme",
        "connection_quality", "codec", "volume", "speak_to_chat", "speak_to_chat_sensitivity",
        "speak_to_chat_timeout", "voice_guidance", "voice_guidance_volume", "auto_power_off",
        "auto_power_off_options", "nc_amb_button", "button_function", "button_functions", "touch_function_left",
        "touch_function_right", "touch_function_left_options", "touch_function_right_options", "ear_detection",
        "worn", "worn_left", "worn_right", "multipoint", "devices", "touch_panel", "sidetone",
        "quick_access_double_tap", "quick_access_triple_tap", "quick_access_double_tap_options",
        "quick_access_triple_tap_options"]}
    entry.update({
        "id": hid, "model": model, "kind": "earbuds" if model == "wf-1000xm5" else "headphones",
        "connected_since": since, "device_name": model.upper(), "address": hid, "model_name": model.upper(),
        "firmware_version": "6.1.0" if model == "wf-1000xm5" else "2.3.1", "supported_source": "device",
    })
    entry["supported"] = {k: False for k in FEATURES}
    return entry


def common(entry, features):
    for k in features + ["noise_control", "eq_preset", "eq_custom", "dsee", "speak_to_chat", "voice_guidance",
                         "voice_guidance_volume", "auto_power_off", "ear_detection", "worn", "volume", "codec",
                         "multipoint", "devices", "source_switch"]:
        entry["supported"][k] = True
    entry.update({
        "noise_mode": "anc", "ambient_sound_level": 10, "voice_passthrough": False, "eq_preset": "off",
        "eq_presets": list(EQ_PRESETS), "eq_custom_bands": [0, 0, 0, 0, 0], "clear_bass": 0, "dsee_extreme": True,
        "codec": "LDAC", "volume": 12, "speak_to_chat": False, "speak_to_chat_sensitivity": "auto",
        "speak_to_chat_timeout": "standard", "voice_guidance": True, "voice_guidance_volume": 0,
        "auto_power_off": "when_taken_off", "auto_power_off_options": list(APO), "ear_detection": True,
        "worn": True, "multipoint": False,
    })


def make_wh(since, active_here=True):
    e = base_headset(WH_ID, "wh-1000xm5", since)
    common(e, ["battery", "nc_amb_button", "button_function", "connection_quality"])
    e.update({
        "battery_level": 85, "battery_charging": False, "connection_quality": "sound",
        "nc_amb_button": "nc_ambient_off", "button_function": "ambient_sound_control",
        "button_functions": list(BUTTON_DEVICE),
        "devices": [{"address": ADAPTER, "name": "laptop", "connected": True, "active": active_here}],
    })
    if not active_here:
        e["devices"].append({"address": "5C:E9:1E:BB:00:02", "name": "phone", "connected": True, "active": True})
    return e


def make_wf(since):
    e = base_headset(WF_ID, "wf-1000xm5", since)
    common(e, ["battery_lr", "battery_case", "touch_function", "sidetone"])
    e.update({
        "battery_left": 70, "battery_left_charging": False, "battery_right": 65, "battery_right_charging": False,
        "battery_case": 53, "battery_case_charging": False, "touch_function_left": "ambient_sound_control",
        "touch_function_right": "playback_control", "touch_function_left_options": list(TOUCH_DEVICE),
        "touch_function_right_options": list(TOUCH_DEVICE), "worn_left": True, "worn_right": True,
        "sidetone": True, "devices": [{"address": ADAPTER, "name": "laptop", "connected": True, "active": True}],
    })
    return e


def plays_here(entry):
    if entry["supported"]["devices"] and entry["devices"] is not None:
        return any(d["active"] and d["address"] == ADAPTER for d in entry["devices"])
    return entry["codec"] not in (None, "NONE")


class MockDaemon:
    def __init__(self, state_dir=None, runtime_dir=None, profile="xm5"):
        self.state_dir = state_dir or os.environ.get("XDG_STATE_HOME", os.path.expanduser("~/.local/state"))
        self.runtime_dir = runtime_dir or os.environ.get("XDG_RUNTIME_DIR", f"/tmp/run-{os.getuid()}")
        self.sony_state_dir = os.path.join(self.state_dir, "sony-headphones")
        self.status_file = os.path.join(self.sony_state_dir, "status.json")
        self.socket_path = os.path.join(self.runtime_dir, "sony-headphones.sock")
        self.running = True
        self.server_sock = None
        now = int(time.time())
        if profile == "both":
            self.known = {WH_ID: make_wh(now, active_here=False), WF_ID: make_wf(now - 1)}
        else:
            self.known = {WH_ID: make_wh(now)}
        self.attached = set(self.known)

    def listed(self):
        order = sorted((h for i, h in self.known.items() if i in self.attached),
                       key=lambda h: (h["kind"] != "headphones", h["id"]))
        return order

    def primary(self):
        listed = self.listed()
        pool = [h for h in listed if plays_here(h)] or listed
        best = None
        for h in pool:
            if best is None or h["connected_since"] > best["connected_since"]:
                best = h
        return best["id"] if best else None

    def document(self):
        listed = self.listed()
        return {"schema_version": 3, "connected": bool(listed), "last_updated": int(time.time()),
                "primary": self.primary(), "headsets": copy.deepcopy(listed)}

    def setup_directories(self):
        os.makedirs(self.sony_state_dir, mode=0o700, exist_ok=True)
        os.makedirs(self.runtime_dir, mode=0o700, exist_ok=True)

    def write_status(self):
        tmp_file = f"{self.status_file}.tmp.{os.getpid()}"
        with open(tmp_file, "w") as f:
            json.dump(self.document(), f, indent=2)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp_file, 0o600)
        os.replace(tmp_file, self.status_file)

    def resolve(self, selector, listed_only=True):
        pool = [h for h in self.known.values() if h["id"] in self.attached or not listed_only]
        if not pool:
            return None, "ERR not connected"
        if selector is None:
            primary = self.primary()
            match = [h for h in pool if h["id"] == primary]
            return (match[0], None) if match else (None, "ERR not connected")
        if ADDRESS.match(selector):
            match = [h for h in pool if h["id"] == selector.upper()]
        else:
            match = [h for h in pool if h["model"] == selector.lower()]
        if not match:
            return None, f"ERR no headset {selector}"
        if len(match) > 1:
            return None, f"ERR ambiguous headset {selector}"
        return match[0], None

    def parse(self, parts):
        """Returns (key, value) for a valid setter or an ERR text."""
        verb = parts[0].lower()
        args = [p.lower() for p in parts[1:]]
        if verb == "ear-detection":
            verb = "ear-detect"
        if verb not in USAGE:
            return f"ERR unknown command '{verb}'"
        usage = "ERR usage: " + USAGE[verb]

        def one_of(options):
            return (verb, args[0]) if len(args) == 1 and args[0] in options else usage

        if verb == "noise":
            return one_of(["anc", "ambient", "off"])
        if verb in ("focus-voice", "dsee", "speak-to-chat", "voice-guidance", "ear-detect", "multipoint",
                    "touch-panel", "sidetone"):
            return one_of(ON_OFF)
        if verb == "ambient-level":
            v = parse_int(args[0], 1, 20) if len(args) == 1 else None
            return (verb, v) if v is not None else usage
        if verb == "voice-guidance-volume":
            v = parse_int(args[0], -2, 2) if len(args) == 1 else None
            return (verb, v) if v is not None else usage
        if verb == "volume":
            v = parse_int(args[0], 0, 30) if len(args) == 1 else None
            return (verb, v) if v is not None else usage
        if verb == "eq":
            if len(args) >= 2 and args[0] == "custom":
                values = [parse_int(a, -10, 10) for a in args[1:]]
                if len(values) != 6 or None in values:
                    return "ERR usage: " + USAGE["eq custom"]
                return ("eq custom", values)
            return one_of(EQ_ALL)
        if verb == "speak-to-chat-config":
            if len(args) == 2 and args[0] in ["auto", "high", "low"] and args[1] in ["short", "standard", "long", "off"]:
                return (verb, args)
            return usage
        if verb == "auto-power-off":
            return one_of(APO)
        if verb == "nc-amb-button":
            return one_of(NC_AMB)
        if verb == "button-function":
            return one_of(BUTTON_ALL)
        if verb == "touch-function":
            if len(args) == 2 and args[0] in ["left", "right"] and args[1] in TOUCH_ALL:
                return (verb, args)
            return usage
        if verb == "quick-access":
            if len(args) == 2 and args[0] in ["double", "triple"] and args[1] in ["off", "spotify"]:
                return (verb, args)
            return usage
        if verb == "connection-quality":
            return one_of(["sound", "connection"])
        if verb == "source":
            return (verb, parts[1].upper()) if len(parts) == 2 and ADDRESS.match(parts[1]) else usage
        return usage

    def apply(self, h, verb, value):
        feature = FEATURE_OF[verb]
        if not h["supported"][feature]:
            return f"ERR unsupported {feature}"
        if verb == "noise":
            h["noise_mode"] = value
        elif verb == "ambient-level":
            h["noise_mode"] = "ambient"
            h["ambient_sound_level"] = value
        elif verb == "focus-voice":
            h["voice_passthrough"] = value == "on"
        elif verb == "eq":
            if value not in h["eq_presets"]:
                return f"ERR unsupported eq_preset {value}"
            h["eq_preset"] = value
        elif verb == "eq custom":
            h["eq_preset"] = "custom"
            h["eq_custom_bands"] = value[:5]
            h["clear_bass"] = value[5]
        elif verb == "dsee":
            h["dsee_extreme"] = value == "on"
        elif verb == "speak-to-chat":
            h["speak_to_chat"] = value == "on"
        elif verb == "speak-to-chat-config":
            h["speak_to_chat_sensitivity"], h["speak_to_chat_timeout"] = value
        elif verb == "voice-guidance":
            h["voice_guidance"] = value == "on"
        elif verb == "voice-guidance-volume":
            h["voice_guidance_volume"] = value
        elif verb == "auto-power-off":
            h["auto_power_off"] = value
        elif verb == "nc-amb-button":
            h["nc_amb_button"] = value
        elif verb == "button-function":
            if value not in h["button_functions"]:
                return f"ERR unsupported button_function {value}"
            h["button_function"] = value
        elif verb == "touch-function":
            side, preset = value
            if preset not in h[f"touch_function_{side}_options"]:
                return f"ERR unsupported touch_function {side} {preset}"
            h[f"touch_function_{side}"] = preset
        elif verb == "ear-detect":
            h["ear_detection"] = value == "on"
        elif verb == "volume":
            h["volume"] = value
        elif verb == "connection-quality":
            h["connection_quality"] = value
        elif verb == "multipoint":
            h["multipoint"] = value == "on"
        elif verb == "touch-panel":
            h["touch_panel"] = value == "on"
        elif verb == "sidetone":
            h["sidetone"] = value == "on"
        elif verb == "quick-access":
            press, function = value
            h[f"quick_access_{press}_tap"] = function
        elif verb == "source":
            match = [d for d in h["devices"] if d["address"] == value and d["connected"]]
            if not match:
                return f"ERR unsupported source_switch {value}"
            for d in h["devices"]:
                d["active"] = d["address"] == value
        self.write_status()
        return "OK"

    def handle_command(self, cmd_line):
        parts = cmd_line.split()
        if not parts:
            return "ERR empty command\n"
        selector = None
        if parts[0].startswith("@"):
            if len(parts[0]) < 2 or len(parts) < 2:
                return "ERR usage: @<address|model> <verb> [args]\n"
            selector = parts[0][1:]
            parts = parts[1:]
        verb = parts[0].lower()

        if verb == "status":
            if selector is not None or len(parts) != 1:
                return "ERR usage: status\n"
            return json.dumps(self.document(), separators=(",", ":")) + "\n"
        if verb in ("probe", "_set_battery", "_disconnect", "_reconnect"):
            h, err = self.resolve(selector, listed_only=verb != "_reconnect")
            if err:
                return err + "\n"
            if verb == "probe":
                return json.dumps({"probe_version": 1, "id": h["id"], "model": h["model"], "kind": h["kind"],
                                   "address": h["id"], "recent_frames": []}) + "\n"
            if verb == "_set_battery":
                level_key = "battery_left" if h["kind"] == "earbuds" else "battery_level"
                if len(parts) >= 2 and re.fullmatch(r"-?[0-9]+", parts[1]):
                    h[level_key] = int(parts[1])
                if len(parts) >= 3 and h["kind"] == "headphones":
                    h["battery_charging"] = parts[2].lower() in ["true", "1", "yes"]
            elif verb == "_disconnect":
                self.attached.discard(h["id"])
            else:
                self.attached.add(h["id"])
            self.write_status()
            return "OK\n"

        parsed = self.parse(parts)
        if isinstance(parsed, str):
            return parsed + "\n"
        h, err = self.resolve(selector)
        if err:
            return err + "\n"
        return self.apply(h, *parsed) + "\n"

    def run(self):
        self.setup_directories()
        if os.path.exists(self.socket_path):
            try:
                os.unlink(self.socket_path)
            except OSError:
                pass

        self.server_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server_sock.bind(self.socket_path)
        os.chmod(self.socket_path, 0o700)
        self.server_sock.listen(10)
        self.server_sock.setblocking(False)
        self.write_status()

        def sig_term_handler(_signum, _frame):
            self.running = False

        def sig_usr1_handler(_signum, _frame):
            self.attached.clear()
            self.write_status()

        def sig_usr2_handler(_signum, _frame):
            self.attached = set(self.known)
            self.write_status()

        signal.signal(signal.SIGTERM, sig_term_handler)
        signal.signal(signal.SIGINT, sig_term_handler)
        signal.signal(signal.SIGUSR1, sig_usr1_handler)
        signal.signal(signal.SIGUSR2, sig_usr2_handler)

        sys.stdout.write(f"[MOCK_DAEMON] Ready PID={os.getpid()}\n")
        sys.stdout.flush()

        inputs = [self.server_sock]
        clients = {}
        while self.running:
            try:
                readable, _, _ = select.select(inputs, [], [], 0.5)
            except (select.error, InterruptedError):
                continue
            for s in readable:
                if s is self.server_sock:
                    try:
                        conn, _ = self.server_sock.accept()
                        conn.setblocking(False)
                        inputs.append(conn)
                        clients[conn] = b""
                    except OSError:
                        pass
                    continue
                try:
                    data = s.recv(4096)
                    if not data:
                        raise OSError("closed")
                    clients[s] += data
                    if len(clients[s]) > 4096 and b"\n" not in clients[s][:4097]:
                        s.sendall(b"ERR line too long\n")
                        raise OSError("line too long")
                    while b"\n" in clients[s]:
                        line, _, rest = clients[s].partition(b"\n")
                        clients[s] = rest
                        response = self.handle_command(line.decode("utf-8", errors="replace"))
                        s.sendall(response.encode("utf-8"))
                except OSError:
                    inputs.remove(s)
                    clients.pop(s, None)
                    s.close()

        try:
            if self.server_sock:
                self.server_sock.close()
            if os.path.exists(self.socket_path):
                os.unlink(self.socket_path)
            if os.path.exists(self.status_file):
                os.unlink(self.status_file)
        except OSError:
            pass
        sys.stdout.write("[MOCK_DAEMON] Shutdown cleanly\n")
        sys.stdout.flush()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-dir", default=None)
    parser.add_argument("--runtime-dir", default=None)
    parser.add_argument("--mock-profile", default="xm5")
    args = parser.parse_args()
    MockDaemon(state_dir=args.state_dir, runtime_dir=args.runtime_dir, profile=args.mock_profile).run()
