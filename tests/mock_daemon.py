#!/usr/bin/env python3
"""
tests/mock_daemon.py — Python stand-in for sony-headphones-daemon (status schema 2).

Used by tests/integration.sh when the daemon binary is not built. It follows
docs/backend-contract.md: status.json schema 2, the sony-ctl verbs and their
ERR texts. Every feature is reported as supported.

Signals:
    SIGTERM / SIGINT: clean shutdown (unlinks socket and status.json)
    SIGUSR1: simulate disconnect (connected: false)
    SIGUSR2: simulate reconnect (connected: true)
"""

import argparse
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
ADDRESS = re.compile(r"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")

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
    "ear-detect": "ear-detect on|off",
    "volume": "volume <n>",
    "connection-quality": "connection-quality sound|connection",
    "multipoint": "multipoint on|off",
    "source": "source <address>",
}


def parse_int(text, lo, hi):
    if not re.fullmatch(r"-?[0-9]+", text):
        return None
    value = int(text)
    return value if lo <= value <= hi else None


class MockDaemon:
    def __init__(self, state_dir=None, runtime_dir=None):
        self.state_dir = state_dir or os.environ.get("XDG_STATE_HOME", os.path.expanduser("~/.local/state"))
        self.runtime_dir = runtime_dir or os.environ.get("XDG_RUNTIME_DIR", f"/tmp/run-{os.getuid()}")
        self.sony_state_dir = os.path.join(self.state_dir, "sony-headphones")
        self.status_file = os.path.join(self.sony_state_dir, "status.json")
        self.socket_path = os.path.join(self.runtime_dir, "sony-headphones.sock")
        self.running = True
        self.server_sock = None
        self.connected = True
        supported = {k: True for k in [
            "battery", "noise_control", "eq_preset", "eq_custom", "dsee", "speak_to_chat", "voice_guidance",
            "voice_guidance_volume", "auto_power_off", "nc_amb_button", "button_function", "ear_detection", "worn",
            "volume", "connection_quality", "codec", "multipoint", "devices", "source_switch"]}
        self.state = {
            "device_name": "WH-1000XM5",
            "address": "AC:80:0A:12:34:56",
            "model_name": "WH-1000XM5",
            "firmware_version": "2.3.1",
            "supported_source": "device",
            "supported": supported,
            "battery_level": 85,
            "battery_charging": False,
            "noise_mode": "anc",
            "ambient_sound_level": 10,
            "voice_passthrough": False,
            "eq_preset": "off",
            "eq_presets": EQ_PRESETS,
            "eq_custom_bands": [0, 0, 0, 0, 0],
            "clear_bass": 0,
            "dsee_extreme": True,
            "connection_quality": "sound",
            "codec": "LDAC",
            "volume": 12,
            "speak_to_chat": False,
            "speak_to_chat_sensitivity": "auto",
            "speak_to_chat_timeout": "standard",
            "voice_guidance": True,
            "voice_guidance_volume": 0,
            "auto_power_off": "when_taken_off",
            "auto_power_off_options": APO,
            "nc_amb_button": "nc_ambient_off",
            "button_function": "ambient_sound_control",
            "button_functions": BUTTON_DEVICE,
            "ear_detection": True,
            "worn": True,
            "multipoint": False,
            "devices": [{"address": "F0:1D:BC:AA:00:01", "name": "laptop", "connected": True, "active": True}],
        }

    def document(self):
        now = int(time.time())
        if not self.connected:
            return {"schema_version": 2, "connected": False, "last_updated": now}
        doc = {"schema_version": 2, "connected": True, "last_updated": now}
        doc.update(self.state)
        doc["charging"] = self.state["battery_charging"]
        doc["ambient_level"] = self.state["ambient_sound_level"]
        doc["eq_bands"] = self.state["eq_custom_bands"]
        doc["dsee"] = self.state["dsee_extreme"]
        return doc

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
        if verb in ("focus-voice", "dsee", "speak-to-chat", "voice-guidance", "ear-detect", "multipoint"):
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
        if verb == "connection-quality":
            return one_of(["sound", "connection"])
        if verb == "source":
            return (verb, parts[1].upper()) if len(parts) == 2 and ADDRESS.match(parts[1]) else usage
        return usage

    def apply(self, verb, value):
        s = self.state
        if verb == "noise":
            s["noise_mode"] = value
        elif verb == "ambient-level":
            s["noise_mode"] = "ambient"
            s["ambient_sound_level"] = value
        elif verb == "focus-voice":
            s["voice_passthrough"] = value == "on"
        elif verb == "eq":
            if value not in s["eq_presets"]:
                return f"ERR unsupported eq_preset {value}"
            s["eq_preset"] = value
        elif verb == "eq custom":
            s["eq_preset"] = "custom"
            s["eq_custom_bands"] = value[:5]
            s["clear_bass"] = value[5]
        elif verb == "dsee":
            s["dsee_extreme"] = value == "on"
        elif verb == "speak-to-chat":
            s["speak_to_chat"] = value == "on"
        elif verb == "speak-to-chat-config":
            s["speak_to_chat_sensitivity"], s["speak_to_chat_timeout"] = value
        elif verb == "voice-guidance":
            s["voice_guidance"] = value == "on"
        elif verb == "voice-guidance-volume":
            s["voice_guidance_volume"] = value
        elif verb == "auto-power-off":
            s["auto_power_off"] = value
        elif verb == "nc-amb-button":
            s["nc_amb_button"] = value
        elif verb == "button-function":
            if value not in s["button_functions"]:
                return f"ERR unsupported button_function {value}"
            s["button_function"] = value
        elif verb == "ear-detect":
            s["ear_detection"] = value == "on"
        elif verb == "volume":
            s["volume"] = value
        elif verb == "connection-quality":
            s["connection_quality"] = value
        elif verb == "multipoint":
            s["multipoint"] = value == "on"
        elif verb == "source":
            match = [d for d in s["devices"] if d["address"] == value and d["connected"]]
            if not match:
                return f"ERR unsupported source_switch {value}"
            for d in s["devices"]:
                d["active"] = d["address"] == value
        self.write_status()
        return "OK"

    def handle_command(self, cmd_line):
        parts = cmd_line.split()
        if not parts:
            return "ERR empty command\n"
        verb = parts[0].lower()

        if verb == "status":
            return json.dumps(self.document(), separators=(",", ":")) + "\n"
        if verb == "probe":
            if not self.connected:
                return "ERR not connected\n"
            return json.dumps({"probe_version": 1, "address": self.state["address"], "recent_frames": []}) + "\n"
        if verb == "_set_battery":
            if len(parts) >= 2 and re.fullmatch(r"-?[0-9]+", parts[1]):
                self.state["battery_level"] = int(parts[1])
            if len(parts) >= 3:
                self.state["battery_charging"] = parts[2].lower() in ["true", "1", "yes"]
            self.write_status()
            return "OK\n"
        if verb == "_disconnect":
            self.connected = False
            self.write_status()
            return "OK\n"
        if verb == "_reconnect":
            self.connected = True
            self.write_status()
            return "OK\n"

        parsed = self.parse(parts)
        if isinstance(parsed, str):
            return parsed + "\n"
        if not self.connected:
            return "ERR not connected\n"
        return self.apply(*parsed) + "\n"

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
            self.connected = False
            self.write_status()

        def sig_usr2_handler(_signum, _frame):
            self.connected = True
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
    args = parser.parse_args()
    MockDaemon(state_dir=args.state_dir, runtime_dir=args.runtime_dir).run()
