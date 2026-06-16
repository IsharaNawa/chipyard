#!/usr/bin/env python3
"""Convert a VS Code Copilot chat-session JSON export into a readable Markdown file.

The exported chat.json has the shape:

    {
      "responderUsername": "GitHub Copilot",
      "requests": [
        {
          "message": {"text": "<the user's question>", ...},
          "response": [ <list of response parts>, ... ],
          "modelId": "...",
          "timestamp": <ms>,
          ...
        },
        ...
      ]
    }

Each item in `requests` is one user turn (`message.text`) followed by the
assistant's `response`, which is a list of heterogeneous "parts". The parts we
care about for reconstructing the answer:

  * markdown text  -> a part WITHOUT a "kind" key but WITH a string "value"
                      (these hold the assistant's actual prose answer)
  * "thinking"     -> the model's reasoning (optional, off by default)
  * "toolInvocationSerialized" -> a tool/command the agent ran (optional)

Usage:
    python3 chat_to_md.py chat.json -o chat.md
    python3 chat_to_md.py chat.json --thinking --tools
"""

import argparse
import datetime as _dt
import json
import sys


def _fmt_timestamp(ms):
    """Render a millisecond epoch timestamp as a readable local datetime."""
    if not ms:
        return None
    try:
        return _dt.datetime.fromtimestamp(ms / 1000).strftime("%Y-%m-%d %H:%M:%S")
    except (ValueError, OSError, TypeError):
        return None


def _text_of(value):
    """Coerce a part's `value` into a plain string.

    The value is usually a markdown string, but VS Code sometimes nests it as
    {"value": "..."} (an IMarkdownString), so handle both.
    """
    if isinstance(value, str):
        return value
    if isinstance(value, dict) and isinstance(value.get("value"), str):
        return value["value"]
    return ""


def extract_answer(response, include_thinking=False, include_tools=False):
    """Walk a request's `response` list and build the assistant's answer markdown."""
    chunks = []
    for part in response:
        if not isinstance(part, dict):
            continue
        kind = part.get("kind")

        if kind is None:
            # Plain markdown prose -- the core of the answer.
            text = _text_of(part.get("value"))
            if text.strip():
                chunks.append(text)

        elif kind == "thinking" and include_thinking:
            text = _text_of(part.get("value"))
            if text.strip():
                title = part.get("generatedTitle") or "Thinking"
                body = "\n".join("> " + line for line in text.strip().splitlines())
                chunks.append(f"**🧠 {title}**\n\n{body}")

        elif kind == "toolInvocationSerialized" and include_tools:
            label = (
                part.get("pastTenseMessage")
                or part.get("invocationMessage")
                or part.get("toolId")
                or "tool call"
            )
            label = _text_of(label)
            if label.strip():
                chunks.append(f"_🔧 {label.strip()}_")

    return "\n\n".join(chunks).strip()


def convert(data, include_thinking=False, include_tools=False):
    responder = data.get("responderUsername", "Assistant")
    requests = data.get("requests", [])

    out = ["# Chat Session", ""]
    out.append(f"- **Assistant:** {responder}")
    out.append(f"- **Turns:** {len(requests)}")
    out.append("")
    out.append("---")
    out.append("")

    for i, req in enumerate(requests, start=1):
        message = req.get("message") or {}
        question = _text_of(message.get("text")) if isinstance(message, dict) else ""
        answer = extract_answer(
            req.get("response", []),
            include_thinking=include_thinking,
            include_tools=include_tools,
        )

        ts = _fmt_timestamp(req.get("timestamp"))
        model = req.get("modelId")
        meta = " · ".join(x for x in (ts, model) if x)

        out.append(f"## Turn {i}")
        if meta:
            out.append(f"*{meta}*")
        out.append("")
        out.append("### 🧑 User")
        out.append("")
        out.append(question.strip() if question.strip() else "_(no message text)_")
        out.append("")
        out.append(f"### 🤖 {responder}")
        out.append("")
        out.append(answer if answer else "_(no textual answer captured)_")
        out.append("")
        out.append("---")
        out.append("")

    return "\n".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Convert a VS Code Copilot chat.json export to Markdown."
    )
    parser.add_argument("input", help="Path to the chat JSON file (e.g. chat.json)")
    parser.add_argument(
        "-o", "--output", help="Output .md path (default: <input>.md)"
    )
    parser.add_argument(
        "--thinking",
        action="store_true",
        help="Include the model's 'thinking' reasoning blocks.",
    )
    parser.add_argument(
        "--tools",
        action="store_true",
        help="Include a one-line note for each tool/command the agent ran.",
    )
    args = parser.parse_args(argv)

    try:
        with open(args.input, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"error: could not read {args.input}: {e}", file=sys.stderr)
        return 1

    md = convert(data, include_thinking=args.thinking, include_tools=args.tools)

    out_path = args.output or (args.input.rsplit(".", 1)[0] + ".md")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(md)

    print(f"Wrote {out_path} ({len(data.get('requests', []))} turns).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
