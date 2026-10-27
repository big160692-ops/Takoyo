"""Prompt assembly: Takoyo's personality plus project-edit and image tools."""

from __future__ import annotations

import datetime

BASE = """You are Takoyo, a warm, clever octopus AI who lives inside the Takoyo desktop app.
You help people build software, make images, explore ideas and simply chat.
Be concise, direct and human. Use Markdown when it helps (short paragraphs, bullets, fenced code).
Never claim to have run something you did not run. If you are unsure, say so and suggest a check.

You can create images. To do that, emit exactly:
<takoyo-image>a detailed image prompt</takoyo-image>
Takoyo renders it in the art panel on the right.
"""

PROJECT_TOOLS = """
A project is open, so you can read and edit real files in it.
When you need to replace or create a file, emit exactly this block (valid JSON, full file content):
<takoyo-tool>
{"tool": "write_file", "path": "relative/path.ext", "content": "the complete new file content"}
</takoyo-tool>
Rules for edits:
- Use paths relative to the project root. Never use absolute paths or `..`.
- Always send the complete file content, not a patch.
- Keep prose around edits short. Explain what changed after the block.
- One write_file block per file. Do not invent files that the user did not ask for.
- If you put code in a normal fenced block, start the fence with the language and file, like:
  ```python file=src/main.py
  ...code...
  ```
  Takoyo will offer that block as an editable file too.
"""

IMAGE_TOOLS = """
If the user asks you to visualise something, prefer the <takoyo-image> tool above over describing it.
"""


def build_system_prompt(project_name: str = "", project_context: str = "", project_id: str = "") -> str:
    parts = [BASE.strip()]
    today = datetime.date.today().strftime("%B %d, %Y")
    parts.append(f"Today is {today}.")
    if project_id:
        parts.append(PROJECT_TOOLS.strip())
        parts.append(
            f"Current project: {project_name} (id {project_id}).\n"
            f"Project snapshot:\n{project_context or '(empty project)'}"
        )
    else:
        parts.append(
            "No project is open. If the user wants to build or edit a project, suggest starting one "
            "from the sidebar so you can touch the files directly."
        )
    parts.append(IMAGE_TOOLS.strip())
    return "\n\n".join(parts)


def build_messages(system: str, history: list[dict], max_history_chars: int = 14_000) -> list[dict]:
    """Trim history to a character budget while keeping complete tool-visible turns."""
    cleaned: list[dict] = []
    for message in history:
        role = message.get("role")
        content = message.get("content")
        if role not in ("user", "assistant") or not isinstance(content, str):
            continue
        if not content.strip():
            continue
        cleaned.append({"role": role, "content": content})

    total = 0
    kept: list[dict] = []
    for message in reversed(cleaned):
        cost = len(message["content"]) + 24
        if kept and total + cost > max_history_chars:
            break
        kept.append(message)
        total += cost
    kept.reverse()
    while kept and kept[0]["role"] != "user":
        kept.pop(0)
    return [{"role": "system", "content": system}] + kept


def attachment_note(name: str, content: str) -> str:
    trimmed = content[:60_000]
    suffix = "\n… (truncated)" if len(content) > 60_000 else ""
    return f"\n\nThe user attached a file named {name}:\n```\n{trimmed}\n```{suffix}"
