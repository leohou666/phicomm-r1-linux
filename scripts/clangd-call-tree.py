#!/usr/bin/env python3
"""Export clangd's static incoming or outgoing call hierarchy as Markdown.

Example:
  scripts/clangd-call-tree.py build/kernel-src/kernel/cpu.c \
    bringup_nonboot_cpus --direction incoming --depth 4

This queries clangd through LSP, so the result uses the same Kbuild-derived
compile_commands.json and .clangd configuration as the editor call hierarchy.
It is a static source-level relationship, not a runtime stack trace.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import quote


def file_uri(path: Path) -> str:
    return "file://" + quote(str(path.resolve()))


def path_from_uri(uri: str) -> str:
    if uri.startswith("file://"):
        return uri.removeprefix("file://")
    return uri


@dataclass(frozen=True)
class Item:
    name: str
    uri: str
    line: int
    raw: dict[str, Any]

    @classmethod
    def from_lsp(cls, item: dict[str, Any]) -> "Item":
        return cls(
            name=item["name"],
            uri=item["uri"],
            line=item["selectionRange"]["start"]["line"] + 1,
            raw=item,
        )

    @property
    def key(self) -> tuple[str, int, str]:
        return (self.uri, self.line, self.name)

    def label(self) -> str:
        return f"`{self.name}()` — `{Path(path_from_uri(self.uri)).name}:{self.line}`"


class Clangd:
    def __init__(self, executable: str, root: Path) -> None:
        self.process = subprocess.Popen(
            [executable, "--background-index=false", "--log=error"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        self.stdin = self.process.stdin
        self.stdout = self.process.stdout
        self.next_id = 1
        self.request(
            "initialize",
            {
                "processId": os.getpid(),
                "rootUri": file_uri(root),
                "capabilities": {
                    "textDocument": {"callHierarchy": {"dynamicRegistration": False}}
                },
            },
        )
        self.notify("initialized", {})

    def close(self) -> None:
        try:
            self.request("shutdown", None)
            self.notify("exit", None)
        finally:
            self.process.terminate()
            self.process.wait(timeout=2)

    def send(self, message: dict[str, Any]) -> None:
        raw = json.dumps(message, separators=(",", ":")).encode()
        self.stdin.write(f"Content-Length: {len(raw)}\r\n\r\n".encode() + raw)
        self.stdin.flush()

    def receive(self) -> dict[str, Any]:
        headers: dict[str, str] = {}
        while True:
            line = self.stdout.readline()
            if not line:
                raise RuntimeError("clangd exited before responding")
            if line in (b"\r\n", b"\n"):
                break
            key, value = line.decode().split(":", 1)
            headers[key.lower()] = value.strip()
        length = int(headers["content-length"])
        return json.loads(self.stdout.read(length))

    def request(self, method: str, params: Any) -> Any:
        request_id = self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        while True:
            response = self.receive()
            if response.get("id") != request_id:
                continue
            if "error" in response:
                raise RuntimeError(f"clangd {method}: {response['error']}")
            return response.get("result")

    def notify(self, method: str, params: Any) -> None:
        self.send({"jsonrpc": "2.0", "method": method, "params": params})


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="C source file containing the symbol")
    parser.add_argument("symbol", help="function symbol at whose definition to start")
    parser.add_argument(
        "--direction",
        choices=("incoming", "outgoing"),
        default="incoming",
        help="incoming = callers, outgoing = callees (default: incoming)",
    )
    parser.add_argument("--depth", type=int, default=4, help="maximum tree depth (default: 4)")
    parser.add_argument("--clangd", default="clangd", help="clangd executable (default: clangd)")
    return parser.parse_args()


def find_definition(source: Path, symbol: str) -> tuple[int, int]:
    pattern = re.compile(rf"^\s*[\w\s*]+\b{re.escape(symbol)}\s*\(")
    for number, line in enumerate(source.read_text(errors="replace").splitlines()):
        match = pattern.search(line)
        if match:
            return (number, line.index(symbol, match.start()))
    raise SystemExit(f"could not find a function definition for {symbol!r} in {source}")


def find_source_root(source: Path) -> Path:
    for candidate in source.parents:
        if (candidate / "Makefile").is_file() and (candidate / "scripts").is_dir():
            return candidate
    raise SystemExit(f"could not find Linux source root above {source}")


def caller_candidates(source_root: Path, symbol: str, limit: int = 64) -> list[Path]:
    expression = rf"\b{re.escape(symbol)}\s*\("
    result = subprocess.run(
        ["rg", "-l", "--glob=*.c", "--glob=*.h", expression, str(source_root)],
        check=False,
        stdout=subprocess.PIPE,
        text=True,
    )
    paths = [Path(line) for line in result.stdout.splitlines()]
    if len(paths) > limit:
        raise SystemExit(
            f"{symbol} occurs in {len(paths)} source files; narrow the symbol or raise the script limit"
        )
    return paths


def open_and_parse(client: Clangd, source: Path) -> None:
    uri = file_uri(source)
    client.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "c", "version": 1,
        "text": source.read_text(errors="replace"),
    }})
    # A request/response round trip makes clangd finish parsing this candidate
    # before incomingCalls asks its cross-file index for references.
    client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})


def walk(
    client: Clangd,
    item: Item,
    direction: str,
    depth: int,
    ancestors: set[tuple[str, int, str]],
    source_root: Path,
    parsed: set[Path],
) -> list[tuple[Item, list[Any]]]:
    if depth == 0:
        return []
    if direction == "incoming":
        for candidate in caller_candidates(source_root, item.name):
            if candidate not in parsed:
                open_and_parse(client, candidate)
                parsed.add(candidate)
    method = "callHierarchy/incomingCalls" if direction == "incoming" else "callHierarchy/outgoingCalls"
    raw_calls = client.request(method, {"item": item.raw}) or []
    field = "from" if direction == "incoming" else "to"
    children: list[tuple[Item, list[Any]]] = []
    for call in raw_calls:
        child = Item.from_lsp(call[field])
        if child.key in ancestors:
            children.append((child, []))
        else:
            children.append((child, walk(
                client, child, direction, depth - 1, ancestors | {child.key}, source_root, parsed
            )))
    return children


def render(item: Item, children: list[Any], prefix: str = "") -> list[str]:
    lines = [prefix + item.label()]
    for index, (child, descendants) in enumerate(children):
        last = index == len(children) - 1
        branch = "└─ " if last else "├─ "
        child_prefix = prefix + ("   " if last else "│  ")
        rendered = render(child, descendants, child_prefix)
        lines.append(prefix + branch + rendered[0].removeprefix(child_prefix))
        lines.extend(rendered[1:])
    return lines


def main() -> int:
    args = parse_args()
    source = args.source.resolve()
    if not source.is_file():
        raise SystemExit(f"source does not exist: {source}")
    if args.depth < 1:
        raise SystemExit("--depth must be at least 1")
    executable = shutil.which(args.clangd) or args.clangd
    line, column = find_definition(source, args.symbol)
    source_root = find_source_root(source)
    root = source_root.parents[1]
    client = Clangd(executable, root)
    try:
        parsed: set[Path] = set()
        for candidate in caller_candidates(source_root, args.symbol):
            open_and_parse(client, candidate)
            parsed.add(candidate)
        uri = file_uri(source)
        prepared = client.request("textDocument/prepareCallHierarchy", {
            "textDocument": {"uri": uri},
            "position": {"line": line, "character": column},
        }) or []
        if not prepared:
            raise SystemExit(f"clangd found no call-hierarchy item for {args.symbol}")
        root_item = Item.from_lsp(prepared[0])
        children = walk(
            client, root_item, args.direction, args.depth, {root_item.key}, source_root, parsed
        )
    finally:
        client.close()

    print(f"# clangd {args.direction} call tree: `{root_item.name}()`\n")
    print("\n".join(render(root_item, children)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
