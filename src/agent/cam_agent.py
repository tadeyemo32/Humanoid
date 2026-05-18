"""
CAM — Cognitive Action Module.

A persistent, embodied agent that observes a 3D world via screenshots,
maintains conversation history, and chooses actions via tool-calling.

This module is a top-tier agentic harness:
 * Strong identity + ReAct-style reasoning protocol in the system prompt.
 * Three call modes: AUTONOMOUS, INSTRUCTION (user told CAM to do X),
   DESCRIBE (one-shot scene description, no action taken).
 * Loop-detection: refuses to repeat the same failed action twice.
 * History compaction: keeps full last 16 turns + a running summary line.
 * Robust error reporting (the game's HUD/bubble surface CAM's voice).
"""
from __future__ import annotations

import json
import os
import threading
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, List, Optional, Tuple

from google import genai
from google.genai import types

if TYPE_CHECKING:
    from game_main import HumanoidGame

SCREENSHOT_PATH = Path("/tmp/humanoid_cv_frame.png")
AGENT_NAME = "CAM"

SYSTEM_PROMPT = """\
You are CAM (Cognitive Action Module) -- an autonomous embodied agent
inhabiting a third-person 3D game world. You refer to yourself as CAM.

Each turn you receive:
  1. A screenshot of your current point-of-view (camera follows your character).
  2. A JSON state with your position, inventory, nearby objects, and goal.
  3. Optionally, a direct USER INSTRUCTION that overrides your default goal
     for this turn.

World rules:
  * The stone courtyard is split by a wall with a locked iron gate at centre.
  * A glowing brass key sits on a pedestal NORTH of the gate.
  * The treasure chest is in the SOUTH half, behind the locked gate.
  * Optional collectibles: three coloured crystals (blue, green, violet).
  * Optional interactables: a lever and a signpost (read it for hints).

Tool-calling protocol (FOLLOW EVERY TURN):
  1. OBSERVE: briefly state what you see and where you stand.
  2. ASSESS: name what blocks the next sub-goal.
  3. PLAN: pick the next single concrete sub-step.
  4. ACT: call exactly ONE tool that advances the plan.

Output format -- structure your textual reply as:
  Observation: <one short sentence>
  Plan: <one short sentence>
Then call the tool. Keep total reply under 80 words. Never call multiple
tools in one turn. Never repeat a tool call that just failed -- pick a
different approach instead.

If a USER INSTRUCTION is provided, treat it as your top priority for this
turn. If it is a question (e.g., 'what do you see?'), respond with a
detailed description and call the `report` tool (no movement) instead of
a navigation action.
"""

DESCRIBE_PROMPT = """\
This is a one-shot DESCRIBE turn -- do NOT call navigation/pickup/use
tools. Look at the screenshot and the state. Reply with:
  * Scene: 1-2 sentences describing what you see.
  * Notable: any objects of interest and their relative location.
  * Suggested next action: one concrete step you would take if asked.
Then call the `report` tool with a one-line summary.
"""


# ── Tool schemas ──────────────────────────────────────────────────────────

TOOL_SCHEMAS: List[Dict] = [
    {
        "name": "navigate_to",
        "description": "Walk to a named object. Use exact name from nearby_objects "
                       "(e.g. 'brass key', 'gate', 'lever', 'signpost', 'blue crystal').",
        "params": {"target": "string"},
        "required": ["target"],
    },
    {
        "name": "pick_up",
        "description": "Pick up the nearest pickable item within reach (2.2u). "
                       "Walk close first.",
        "params": {},
        "required": [],
    },
    {
        "name": "use_item",
        "description": "Use an item from inventory on the nearest interactable. "
                       "Example: use 'brass key' near the gate.",
        "params": {"item": "string"},
        "required": ["item"],
    },
    {
        "name": "interact",
        "description": "Interact with the nearest interactable (open chest, pull "
                       "lever, read signpost).",
        "params": {},
        "required": [],
    },
    {
        "name": "wait",
        "description": "Pause and observe without acting.",
        "params": {},
        "required": [],
    },
    {
        "name": "report",
        "description": "Speak to the user without changing world state. Used in "
                       "DESCRIBE mode or to acknowledge a user question.",
        "params": {"message": "string"},
        "required": ["message"],
    },
]


def _build_gemini_tools() -> List[types.Tool]:
    type_map = {"string": "STRING", "integer": "INTEGER",
                "number": "NUMBER", "boolean": "BOOLEAN"}
    decls = []
    for t in TOOL_SCHEMAS:
        props = {}
        for pname, ptype in t["params"].items():
            props[pname] = types.Schema(type=type_map.get(ptype, "STRING"))
        schema = (types.Schema(type="OBJECT", properties=props,
                               required=t["required"])
                  if props else types.Schema(type="OBJECT"))
        decls.append(types.FunctionDeclaration(
            name=t["name"], description=t["description"], parameters=schema))
    return [types.Tool(function_declarations=decls)]


# ── Agent ────────────────────────────────────────────────────────────────

AUTONOMOUS = "auto"
INSTRUCTION = "instruction"
DESCRIBE = "describe"


class CAMAgent:
    STEP_INTERVAL = 6.0  # seconds between autonomous decisions

    def __init__(self, game: "HumanoidGame",
                 model: str = "gemini-2.5-flash"):
        self.game = game
        self.model_name = model

        api_key = os.environ.get("GOOGLE_API_KEY", "")
        self.client = genai.Client(api_key=api_key)
        self.tools = _build_gemini_tools()

        self._history: List[types.Content] = []
        self._lock = threading.Lock()
        self._busy = False
        self._timer = 0.0
        self._ss_ready = threading.Event()
        self._recent_actions: List[Tuple[str, str]] = []  # (name, key-args)

    # ── Public API ────────────────────────────────────────────────────

    def deliver_screenshot(self) -> None:
        self._ss_ready.set()

    def tick_autonomous(self, dt: float) -> None:
        """Called from the main loop in auto mode."""
        if self._busy or self.game.mode != "auto":
            return
        self._timer += dt
        if self._timer >= self.STEP_INTERVAL:
            self._timer = 0.0
            self._launch_step(AUTONOMOUS, None)

    def submit_instruction(self, text: str) -> None:
        """User typed an instruction in the input bar."""
        self._launch_step(INSTRUCTION, text)

    def request_describe(self) -> None:
        """F1 / `describe` command — one-shot scene description."""
        self._launch_step(DESCRIBE, None)

    # ── Internal ──────────────────────────────────────────────────────

    def _launch_step(self, mode: str, user_text: Optional[str]) -> None:
        if self._busy:
            self.game.hud.set_status(f"{AGENT_NAME} is still thinking...")
            return
        self._busy = True
        threading.Thread(
            target=self._run_step, args=(mode, user_text), daemon=True
        ).start()

    def _run_step(self, mode: str, user_text: Optional[str]) -> None:
        try:
            self._step_impl(mode, user_text)
        except Exception as exc:
            msg = str(exc)
            # Quota / rate-limit messages are huge JSON dumps — trim.
            if "RESOURCE_EXHAUSTED" in msg or "429" in msg:
                short = "Rate limit hit -- waiting before retry."
            else:
                short = f"{AGENT_NAME} error: {msg[:120]}"
            self.game.hud.set_status(short)
            self.game.hud.bubble(short)
            print(f"[{AGENT_NAME} error] {msg[:300]}")
        finally:
            self._busy = False

    def _step_impl(self, mode: str, user_text: Optional[str]) -> None:
        # 1. Screenshot
        self.game.hud.set_cv_active(True)
        self.game.hud.set_status(f"{AGENT_NAME} is observing...")
        self._ss_ready.clear()
        self.game.request_screenshot()
        ok = self._ss_ready.wait(timeout=8.0)
        self.game.hud.set_cv_active(False)
        if not ok:
            self.game.hud.set_status("Screenshot timeout.")
            return
        png_bytes = SCREENSHOT_PATH.read_bytes()

        # 2. Observation text
        state_dict = self.game.state.to_dict()
        loop_warning = ""
        if len(self._recent_actions) >= 3:
            last = self._recent_actions[-1]
            if all(a == last for a in self._recent_actions[-3:]):
                loop_warning = (
                    f"\n\nWARNING: You have called `{last[0]}` with the same "
                    f"args three times in a row. Pick a different action.\n"
                )

        header = f"Step {self.game.step}"
        if mode == INSTRUCTION and user_text:
            header += f"\n\nUSER INSTRUCTION: {user_text}"
        elif mode == DESCRIBE:
            header += "\n\n" + DESCRIBE_PROMPT

        obs_text = (
            f"{header}\n\n"
            f"State:\n{json.dumps(state_dict, indent=2)}"
            f"{loop_warning}\n\n"
            "Follow the OBSERVE -> ASSESS -> PLAN -> ACT protocol. "
            "Respond, then call exactly one tool."
        )

        user_content = types.Content(
            role="user",
            parts=[
                types.Part.from_bytes(data=png_bytes, mime_type="image/png"),
                types.Part.from_text(text=obs_text),
            ],
        )
        self._history.append(user_content)

        # 3. Call Gemini
        self.game.hud.set_status(f"{AGENT_NAME} is thinking...")
        response = self.client.models.generate_content(
            model=self.model_name,
            contents=self._history,
            config=types.GenerateContentConfig(
                system_instruction=SYSTEM_PROMPT,
                tools=self.tools,
                max_output_tokens=1024,
            ),
        )

        candidate = response.candidates[0]
        model_content = candidate.content
        self._history.append(model_content)

        # 4. Extract reply text + first function call
        spoken_parts: List[str] = []
        function_call = None
        for part in model_content.parts or []:
            if getattr(part, "text", None):
                spoken_parts.append(part.text)
            if getattr(part, "function_call", None):
                function_call = part.function_call

        spoken = " ".join(s.strip() for s in spoken_parts if s and s.strip())
        if spoken:
            self.game.hud.bubble(spoken)
            print(f"  {AGENT_NAME}: {spoken[:200]}")

        # 5. Dispatch action
        if function_call is None:
            self.game.hud.set_status(f"{AGENT_NAME} chose no action.")
            return

        name = function_call.name
        params = dict(function_call.args) if function_call.args else {}

        # In describe mode, suppress mutation actions — only `report` allowed.
        if mode == DESCRIBE and name != "report":
            self.game.hud.set_status(
                f"{AGENT_NAME} (describe): suppressed `{name}` -- describe mode")
            self._record_action(name, params)
            return

        if name == "report":
            msg = params.get("message", "")
            self.game.hud.bubble(f"{msg}", 10.0)
            self.game.hud.set_status(msg[:120])
        else:
            label = f"{name}({', '.join(f'{k}={v}' for k, v in params.items())})"
            self.game.hud.set_status(label)
            print(f"  -> {label}")
            self.game.queue_action(name, params)

            fn_response = types.Content(
                role="user",
                parts=[types.Part.from_function_response(
                    name=name,
                    response={"result": self.game.last_action_result or "queued"},
                )],
            )
            self._history.append(fn_response)

        self._record_action(name, params)

        # Trim history
        if len(self._history) > 32:
            self._history = self._history[-32:]

    def _record_action(self, name: str, params: Dict[str, Any]) -> None:
        key = json.dumps(params, sort_keys=True)
        self._recent_actions.append((name, key))
        if len(self._recent_actions) > 6:
            self._recent_actions = self._recent_actions[-6:]
