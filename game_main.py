#!/usr/bin/env python3
"""
Humanoid -- CAM, an embodied LLM agent in a 3D C/raylib world.

The C binary at src/game/game_engine renders + simulates the world and
exposes a line-based stdin/stdout protocol. This Python process:
  * Launches the engine.
  * Drives the CAM agent loop (autonomous or instruction/describe modes).
  * Reads async events from the engine (user typed text, mode toggle,
    describe request, quit).
"""
from __future__ import annotations

import os
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

ROOT_DIR = Path(__file__).resolve().parent


def load_dotenv(path: Path) -> None:
    if not path.exists():
        return

    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]
        os.environ.setdefault(key, value)


load_dotenv(ROOT_DIR / ".env")

# ── API key check ──────────────────────────────────────────────────────────
if not os.environ.get("GOOGLE_API_KEY"):
    print("Error: GOOGLE_API_KEY environment variable is not set.")
    print("  Set it in .env or export GOOGLE_API_KEY=AIza...")
    sys.exit(1)

from src.agent.cam_agent import CAMAgent, SCREENSHOT_PATH, AGENT_NAME

GAME_DIR = ROOT_DIR / "src" / "game"
BINARY   = GAME_DIR / "game_engine"


# ── Proxies ──────────────────────────────────────────────────────────────

class HUDProxy:
    def __init__(self, game: "HumanoidGame"):
        self._g = game

    def set_goal(self, text: str) -> None:
        self._g._send(f"GOAL {text}")

    def set_status(self, text: str) -> None:
        self._g._send(f"STATUS {text}")

    def set_cv_active(self, active: bool) -> None:
        self._g._send(f"CV {1 if active else 0}")

    def bubble(self, text: str, _seconds: float = 8.0) -> None:
        # Strip newlines so a single IPC line still parses cleanly.
        flat = " ".join(text.split())
        self._g._send(f"BUBBLE {flat}")


class StateProxy:
    GOAL = (
        "Primary: find the brass key, unlock the gate, reach the treasure chest. "
        "Bonus: collect the 3 crystals and read the signpost."
    )

    def __init__(self, game: "HumanoidGame"):
        self._g = game

    def _fetch(self) -> Dict[str, Any]:
        import json as _json
        line = self._g._send("STATE")
        if not line.startswith("OK "):
            return {}
        try:
            return _json.loads(line[3:])
        except _json.JSONDecodeError:
            return {}

    @property
    def goal_complete(self) -> bool:
        return self._fetch().get("goal_complete", False)

    @property
    def inventory(self) -> List[str]:
        return list(self._fetch().get("inventory", []))

    @property
    def character_pos(self):
        p = self._fetch().get("position", {"x": 0.0, "z": 0.0})
        return (p.get("x", 0.0), 0.0, p.get("z", 0.0))

    def to_dict(self) -> Dict[str, Any]:
        return self._fetch()


# ── Game host ────────────────────────────────────────────────────────────

class HumanoidGame:
    def __init__(self):
        if not BINARY.exists():
            print(f"Game binary not found: {BINARY}")
            print("Run ./run.sh from the repo root, or build it first:")
            print(f"    cd {GAME_DIR} && make")
            sys.exit(1)

        self.proc = subprocess.Popen(
            [str(BINARY)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=None,
            bufsize=1,
            text=True,
            cwd=str(GAME_DIR),
        )

        # ── IPC plumbing ────────────────────────────────────────────────
        self._send_lock = threading.Lock()
        self._response_q: "queue.Queue[str]" = queue.Queue()
        self._events:     "queue.Queue[tuple]" = queue.Queue()
        self._reader = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader.start()

        # ── Proxies + agent ─────────────────────────────────────────────
        self.state = StateProxy(self)
        self.hud   = HUDProxy(self)
        self.last_action_result: Optional[str] = None
        self.step: int = 0
        self.mode: str = "auto"  # 'auto' | 'manual'
        self._shutdown = False

        # Initial HUD state
        self._send(f"AGENT_NAME {AGENT_NAME}")
        self.hud.set_goal(StateProxy.GOAL)
        self.hud.set_status(f"{AGENT_NAME} online. Auto mode.")

        self.agent = CAMAgent(self)

    # ── Reader thread ────────────────────────────────────────────────────

    def _reader_loop(self) -> None:
        assert self.proc.stdout is not None
        for line in self.proc.stdout:
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("EVT "):
                self._dispatch_event(line[4:])
            else:
                self._response_q.put(line)
        # EOF — engine died
        self._response_q.put("ERR engine_eof")
        self._events.put(("QUIT", ""))

    def _dispatch_event(self, payload: str) -> None:
        # Forms: "INPUT <text>", "MODE <auto|manual>", "DESCRIBE", "QUIT"
        if " " in payload:
            verb, rest = payload.split(" ", 1)
        else:
            verb, rest = payload, ""
        self._events.put((verb, rest))

    # ── IPC ──────────────────────────────────────────────────────────────

    def _send(self, command: str) -> str:
        with self._send_lock:
            if self.proc.poll() is not None or self.proc.stdin is None:
                return "ERR engine_dead"
            try:
                self.proc.stdin.write(command + "\n")
                self.proc.stdin.flush()
            except (BrokenPipeError, OSError):
                return "ERR engine_dead"
            try:
                return self._response_q.get(timeout=5.0)
            except queue.Empty:
                return "ERR timeout"

    # ── Interface for CAMAgent ───────────────────────────────────────────

    def request_screenshot(self) -> None:
        self._send(f"SCREENSHOT {SCREENSHOT_PATH}")
        self.agent.deliver_screenshot()

    def queue_action(self, action: str, params: Dict[str, Any]) -> None:
        if action == "navigate_to":
            cmd = f"NAV {params.get('target', '')}"
        elif action == "pick_up":
            cmd = "PICKUP"
        elif action == "use_item":
            cmd = f"USE {params.get('item', '')}"
        elif action == "interact":
            cmd = "INTERACT"
        else:
            cmd = "WAIT"
        response = self._send(cmd)
        if response.startswith("OK "):
            self.last_action_result = response[3:]
        elif response.startswith("ERR "):
            self.last_action_result = response[4:]
        else:
            self.last_action_result = response
        self.step += 1

    # ── Event handlers ───────────────────────────────────────────────────

    def _handle_event(self, verb: str, rest: str) -> None:
        if verb == "QUIT":
            self._shutdown = True
            return
        if verb == "MODE":
            self.mode = rest if rest in ("auto", "manual") else "auto"
            self.hud.set_status(f"Mode: {self.mode.upper()}")
            self.hud.bubble(f"Switched to {self.mode.upper()} mode.")
            return
        if verb == "DESCRIBE":
            self.hud.set_status(f"{AGENT_NAME} describing scene...")
            self.agent.request_describe()
            return
        if verb == "INPUT":
            text = rest.strip()
            if not text:
                return
            # Built-in command shortcuts
            low = text.lower()
            if low in ("auto", "/auto"):
                self.mode = "auto"
                self._send("MODE auto")
                self.hud.bubble("Auto mode engaged.")
                return
            if low in ("manual", "/manual"):
                self.mode = "manual"
                self._send("MODE manual")
                self.hud.bubble("Manual mode engaged.")
                return
            if low in ("describe", "/describe"):
                self.agent.request_describe()
                return
            # Otherwise treat as a free-form instruction for CAM
            self.hud.bubble(f'You: "{text}"')
            self.agent.submit_instruction(text)

    # ── Run loop ─────────────────────────────────────────────────────────

    def run(self) -> None:
        print("\n" + "=" * 60)
        print(f"  Humanoid -- {AGENT_NAME} (C/raylib)")
        print("  TAB: auto/manual    F1: describe    ENTER: submit input")
        print("=" * 60 + "\n")

        last = time.time()
        try:
            while self.proc.poll() is None and not self._shutdown:
                # Drain events
                try:
                    while True:
                        verb, rest = self._events.get_nowait()
                        self._handle_event(verb, rest)
                        if self._shutdown:
                            break
                except queue.Empty:
                    pass

                now = time.time()
                dt = now - last
                last = now

                if self.mode == "auto" and not self.state.goal_complete:
                    self.agent.tick_autonomous(dt)

                time.sleep(0.05)
        except KeyboardInterrupt:
            pass
        finally:
            try:
                self._send("QUIT")
            except Exception:
                pass
            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()


if __name__ == "__main__":
    HumanoidGame().run()
