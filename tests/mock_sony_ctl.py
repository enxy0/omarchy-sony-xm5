#!/usr/bin/env python3
"""
tests/mock_sony_ctl.py: reference implementation of the sony-ctl CLI.

Follows docs/backend-contract.md (schema version 2), section 1 (exit codes)
and section 3 (verbs). The CLI checks argument syntax before it connects,
with the same rules as the daemon, then sends one line and prints one reply.

Usage: sony-ctl [-s <socket>] <verb> [args...]

Exit codes:
    0: the daemon replied OK or a JSON object (printed to stdout)
    1: bad arguments, the daemon replied ERR, or socket I/O failed
    2: cannot connect to the daemon socket
"""

import os
import re
import socket
import sys

SOCKET_TIMEOUT_S = 2.0

ADDRESS_RE = re.compile(r"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")

EQ_PRESETS = [
    "off", "bright", "excited", "mellow", "relaxed", "vocal", "treble",
    "bass", "speech", "custom", "user1", "user2", "user3", "user4", "user5",
]
AUTO_POWER_OFF = ["off", "5min", "15min", "30min", "60min", "180min", "when_taken_off"]
NC_AMB_BUTTON = ["nc_ambient_off", "nc_ambient", "nc_off", "ambient_off"]
BUTTON_FUNCTION = [
    "ambient_sound_control", "voice_recognition", "google_assistant",
    "amazon_alexa", "quick_access", "none",
]
ON_OFF = ["on", "off"]

# verb -> usage text (contract 3.3 "Verb and syntax" cell).
USAGE = {
    "status": "status",
    "probe": "probe",
    "noise": "noise anc|ambient|off",
    "ambient-level": "ambient-level <n>",
    "focus-voice": "focus-voice on|off",
    "eq": "eq <preset> | eq custom [<b1> <b2> <b3> <b4> <b5> <clear_bass>]",
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
ALIASES = {"ear-detection": "ear-detect"}
READ_VERBS = ("status", "probe")


class UsageError(Exception):
    pass


def default_socket_path():
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR") or f"/tmp/run-{os.getuid()}"
    return os.path.join(runtime_dir, "sony-headphones.sock")


def parse_int(text, low, high, what):
    try:
        value = int(text)
    except (TypeError, ValueError):
        raise UsageError(f"'{what}' requires an integer between {low} and {high}")
    if not low <= value <= high:
        raise UsageError(f"'{what}' requires an integer between {low} and {high}")
    return value


def need_args(verb, args, count):
    if len(args) != count:
        raise UsageError(f"usage: {USAGE[verb]}")


def choice(verb, value, options):
    value = value.lower()
    if value not in options:
        raise UsageError(f"usage: {USAGE[verb]}")
    return value


def build_request(verb, args):
    """Checks the arguments of one verb and returns the request line."""
    if verb in READ_VERBS:
        need_args(verb, args, 0)
        return verb

    if verb == "eq":
        if args and args[0].lower() == "custom":
            # Bare `eq custom` is the preset form (contract 3.3).
            if len(args) == 1:
                return "eq custom"
            if len(args) != 7:
                raise UsageError(f"usage: {USAGE[verb]}")
            values = [parse_int(a, -10, 10, "eq custom") for a in args[1:]]
            return "eq custom " + " ".join(str(v) for v in values)
        need_args(verb, args, 1)
        return f"eq {choice(verb, args[0], EQ_PRESETS)}"

    if verb == "speak-to-chat-config":
        need_args(verb, args, 2)
        sensitivity = choice(verb, args[0], ["auto", "high", "low"])
        timeout = choice(verb, args[1], ["short", "standard", "long", "off"])
        return f"{verb} {sensitivity} {timeout}"

    need_args(verb, args, 1)
    arg = args[0]
    if verb == "noise":
        return f"noise {choice(verb, arg, ['anc', 'ambient', 'off'])}"
    if verb == "ambient-level":
        return f"ambient-level {parse_int(arg, 1, 20, verb)}"
    if verb == "voice-guidance-volume":
        return f"{verb} {parse_int(arg, -2, 2, verb)}"
    if verb == "volume":
        return f"volume {parse_int(arg, 0, 30, verb)}"
    if verb in ("focus-voice", "dsee", "speak-to-chat", "voice-guidance", "ear-detect", "multipoint"):
        return f"{verb} {choice(verb, arg, ON_OFF)}"
    if verb == "auto-power-off":
        return f"{verb} {choice(verb, arg, AUTO_POWER_OFF)}"
    if verb == "nc-amb-button":
        return f"{verb} {choice(verb, arg, NC_AMB_BUTTON)}"
    if verb == "button-function":
        return f"{verb} {choice(verb, arg, BUTTON_FUNCTION)}"
    if verb == "connection-quality":
        return f"{verb} {choice(verb, arg, ['sound', 'connection'])}"
    if verb == "source":
        if not ADDRESS_RE.match(arg):
            raise UsageError(f"usage: {USAGE[verb]}")
        return f"source {arg.upper()}"
    raise UsageError(f"Unknown subcommand '{verb}'")


def send_request(socket_path, line):
    """Returns (exit_code, reply)."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(SOCKET_TIMEOUT_S)
    try:
        sock.connect(socket_path)
    except OSError as e:
        sock.close()
        sys.stderr.write(f"Error: Cannot connect to daemon socket ({e})\n")
        return 2, ""

    try:
        sock.sendall((line + "\n").encode("utf-8"))
        reply = b""
        while b"\n" not in reply:
            chunk = sock.recv(65536)
            if not chunk:
                break
            reply += chunk
    except OSError as e:
        sys.stderr.write(f"Error: Socket communication failed ({e})\n")
        return 1, ""
    finally:
        sock.close()
    return 0, reply.decode("utf-8", errors="replace").strip()


def print_usage(stream):
    stream.write("Usage: sony-ctl [-s <socket>] <verb> [args...]\n")
    stream.write("Verbs:\n")
    for usage in USAGE.values():
        stream.write(f"  {usage}\n")


def main(argv):
    socket_path = None
    args = list(argv)
    while args and args[0].startswith("-"):
        flag = args.pop(0)
        if flag in ("-h", "--help"):
            print_usage(sys.stdout)
            return 0
        if flag in ("-s", "--socket") and args:
            socket_path = args.pop(0)
            continue
        sys.stderr.write(f"Error: Unknown option '{flag}'\n")
        return 1

    if not args:
        print_usage(sys.stderr)
        return 1

    verb = args[0].lower()
    verb = ALIASES.get(verb, verb)
    if verb not in USAGE:
        sys.stderr.write(f"Error: Unknown subcommand '{args[0]}'\n")
        return 1

    try:
        line = build_request(verb, args[1:])
    except UsageError as e:
        sys.stderr.write(f"Error: {e}\n")
        return 1

    code, reply = send_request(socket_path or default_socket_path(), line)
    if code != 0:
        return code
    if reply == "OK" or reply.startswith("{"):
        print(reply)
        return 0
    sys.stderr.write(f"Error from daemon: {reply or 'empty reply'}\n")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
