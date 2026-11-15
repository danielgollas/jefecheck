#!/usr/bin/env python3
"""Feature notes: release notes that double as a feature reference.

Turns a project's Markdown feature notes into a static site with code quotes
checked against git and committed test evidence. Standard library only, so a
project can vendor this file (with ``templates/`` beside it) and run it in CI.

Usage: feature_notes.py {build,check,serve,collect,version} [--root PATH] ...
"""

from __future__ import annotations

import argparse
import contextlib
import html
import http.server
import json
import os
import re
import shutil
import string
import subprocess
import sys
import urllib.parse
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Literal

VERSION = "0.1.0"


class NotesError(Exception):
    """A problem the user can fix: bad config, a broken note, an unresolvable quote."""


# --- parse ---------------------------------------------------------------------------------
#
# A small YAML subset: block maps and lists, flow lists and maps, quoted and plain scalars,
# comments, null/true/false and integers. Anything fancier is rejected with its line number.

_INT_RE = re.compile(r"-?\d+")


def _strip_comment(line: str) -> str:
    quote: str | None = None
    for i, ch in enumerate(line):
        if quote:
            if ch == quote:
                quote = None
        elif ch in "'\"":
            quote = ch
        elif ch == "#" and (i == 0 or line[i - 1] in " \t"):
            return line[:i].rstrip()
    return line.rstrip()


def _unsupported(lineno: int, what: str) -> NotesError:
    return NotesError(f"line {lineno}: unsupported YAML: {what}")


def _scalar(text: str, lineno: int) -> Any:
    text = text.strip()
    if not text:
        return None
    if text[0] in "[{":
        value, rest = _flow(text, 0, lineno)
        if text[rest:].strip():
            raise _unsupported(lineno, f"trailing text after {text[:rest]!r}")
        return value
    if text[0] in "'\"":
        value, end = _quoted(text, 0, lineno)
        if text[end:].strip():
            raise _unsupported(lineno, f"trailing text after {text[:end]!r}")
        return value
    if text[0] in "|>&*!%@`":
        raise _unsupported(lineno, f"{text[0]!r} at the start of a value")
    if text in ("null", "~"):
        return None
    if text == "true":
        return True
    if text == "false":
        return False
    if _INT_RE.fullmatch(text):
        return int(text)
    return text


def _quoted(text: str, i: int, lineno: int) -> tuple[str, int]:
    quote = text[i]
    out: list[str] = []
    j = i + 1
    while j < len(text):
        ch = text[j]
        if quote == "'" and ch == "'":
            if text[j + 1 : j + 2] == "'":
                out.append("'")
                j += 2
                continue
            return "".join(out), j + 1
        if quote == '"' and ch == "\\":
            nxt = text[j + 1 : j + 2]
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(nxt, "\\" + nxt))
            j += 2
            continue
        if quote == '"' and ch == '"':
            return "".join(out), j + 1
        out.append(ch)
        j += 1
    raise _unsupported(lineno, "unterminated quoted string")


def _flow(text: str, i: int, lineno: int) -> tuple[Any, int]:
    """Parse a flow list or map starting at text[i]; returns (value, index after it)."""
    closer = "]" if text[i] == "[" else "}"
    is_map = closer == "}"
    items: list[Any] = []
    mapping: dict[str, Any] = {}
    j = i + 1
    while True:
        while j < len(text) and text[j] == " ":
            j += 1
        if j >= len(text):
            raise _unsupported(lineno, f"unterminated flow collection, expected {closer!r}")
        if text[j] == closer:
            return (mapping if is_map else items), j + 1
        if is_map:
            key, j = _flow_atom(text, j, lineno, stop=":")
            if j >= len(text) or text[j] != ":":
                raise _unsupported(lineno, "flow map entry without ':'")
            j += 1
            while j < len(text) and text[j] == " ":
                j += 1
            value, j = _flow_atom(text, j, lineno, stop=",}")
            if not isinstance(key, str):
                key = str(key)
            mapping[key] = value
        else:
            value, j = _flow_atom(text, j, lineno, stop=",]")
            items.append(value)
        while j < len(text) and text[j] == " ":
            j += 1
        if j < len(text) and text[j] == ",":
            j += 1


def _flow_atom(text: str, j: int, lineno: int, stop: str) -> tuple[Any, int]:
    if j < len(text) and text[j] in "[{":
        return _flow(text, j, lineno)
    if j < len(text) and text[j] in "'\"":
        return _quoted(text, j, lineno)
    start = j
    while j < len(text) and text[j] not in stop:
        j += 1
    return _scalar(text[start:j], lineno), j


def _split_key(content: str, lineno: int) -> tuple[str, str] | None:
    """Split 'key: value' (or 'key:') outside quotes; None when the line has no key."""
    if content[0] in "'\"":
        key, end = _quoted(content, 0, lineno)
        rest = content[end:]
        if rest.startswith(":") and (len(rest) == 1 or rest[1] == " "):
            return key, rest[1:].strip()
        return None
    if content[0] in "[{":
        return None
    for i, ch in enumerate(content):
        if ch == ":" and (i + 1 == len(content) or content[i + 1] == " "):
            return content[:i].strip(), content[i + 1 :].strip()
    return None


class _Lines:
    def __init__(self, text: str) -> None:
        self.items: list[tuple[int, int, str]] = []  # (lineno, indent, content)
        for n, raw in enumerate(text.splitlines(), 1):
            stripped = _strip_comment(raw)
            if not stripped.strip():
                continue
            lead = stripped[: len(stripped) - len(stripped.lstrip())]
            if "\t" in lead:
                raise _unsupported(n, "tab indentation")
            content = stripped.strip()
            if content in ("---", "..."):
                raise _unsupported(n, "multiple documents")
            self.items.append((n, len(lead), content))


def _is_item(content: str) -> bool:
    return content == "-" or content.startswith("- ")


def _block(lines: _Lines, i: int, indent: int) -> tuple[Any, int]:
    _, _, content = lines.items[i]
    if _is_item(content):
        return _block_list(lines, i, indent)
    return _block_map(lines, i, indent)


def _child(lines: _Lines, i: int, indent: int, allow_same_indent_list: bool) -> tuple[Any, int]:
    """The value of a key or item whose inline value is empty: a nested block or null."""
    if i < len(lines.items):
        _, nxt_indent, nxt = lines.items[i]
        if nxt_indent > indent:
            return _block(lines, i, nxt_indent)
        if allow_same_indent_list and nxt_indent == indent and _is_item(nxt):
            return _block_list(lines, i, indent)
    return None, i


def _block_map(lines: _Lines, i: int, indent: int) -> tuple[dict[str, Any], int]:
    out: dict[str, Any] = {}
    while i < len(lines.items):
        lineno, ind, content = lines.items[i]
        if ind < indent:
            break
        if ind > indent:
            raise _unsupported(lineno, "unexpected indentation")
        if _is_item(content):
            break
        split = _split_key(content, lineno)
        if split is None:
            raise _unsupported(lineno, f"expected 'key: value', got {content!r}")
        key, rest = split
        if key in out:
            raise NotesError(f"line {lineno}: duplicate key {key!r}")
        i += 1
        if rest:
            out[key] = _scalar(rest, lineno)
        else:
            out[key], i = _child(lines, i, indent, allow_same_indent_list=True)
    return out, i


def _block_list(lines: _Lines, i: int, indent: int) -> tuple[list[Any], int]:
    out: list[Any] = []
    while i < len(lines.items):
        lineno, ind, content = lines.items[i]
        if ind < indent or not _is_item(content):
            if ind > indent:
                raise _unsupported(lineno, "unexpected indentation")
            break
        if ind > indent:
            raise _unsupported(lineno, "unexpected indentation")
        rest = content[1:].lstrip()
        if not rest:
            value, i = _child(lines, i + 1, indent, allow_same_indent_list=False)
            out.append(value)
            continue
        if _split_key(rest, lineno) is not None:
            # "- key: value": the item is a map whose keys sit where 'key' starts.
            inner = ind + (len(content) - len(rest))
            lines.items[i] = (lineno, inner, rest)
            value, i = _block_map(lines, i, inner)
            out.append(value)
            continue
        out.append(_scalar(rest, lineno))
        i += 1
    return out, i


def parse_yaml(text: str) -> Any:
    """Parse the supported YAML subset. Empty documents parse to None."""
    lines = _Lines(text)
    if not lines.items:
        return None
    indent = lines.items[0][1]
    value, i = _block(lines, 0, indent)
    if i < len(lines.items):
        raise _unsupported(lines.items[i][0], "unexpected indentation")
    return value


def split_front_matter(text: str) -> tuple[dict[str, Any], str]:
    """Split '---' delimited YAML front matter from a Markdown body."""
    lines = text.splitlines(keepends=True)
    if not lines or lines[0].strip() != "---":
        raise NotesError("missing front matter: the file must start with a '---' line")
    for n in range(1, len(lines)):
        if lines[n].strip() == "---":
            meta = parse_yaml("".join(lines[1:n])) or {}
            if not isinstance(meta, dict):
                raise NotesError("front matter must be a map of 'key: value' lines")
            return meta, "".join(lines[n + 1 :])
    raise NotesError("unclosed front matter: no closing '---' line")


# --- model ---------------------------------------------------------------------------------

CONFIG_FILE = "feature-notes.yml"
_REPO_RE = re.compile(r"[\w.-]+/[\w.-]+")
_SHA_RE = re.compile(r"[0-9a-f]{7,40}")
_DATE_RE = re.compile(r"\d{4}-\d{2}-\d{2}")
_SLUG_RE = re.compile(r"[a-z0-9][a-z0-9._-]*")
RESULTS = ("pass", "fail", "skip")
_AREA_RE = re.compile(r"[a-z0-9][a-z0-9-]*(/[a-z0-9][a-z0-9-]*)*")


@dataclass
class TestDef:
    cmd: str
    env: dict[str, str] = field(default_factory=dict)


@dataclass
class Config:
    root: Path
    repo: str
    notes_dir: Path
    out_dir: Path
    base_url: str
    title: str
    theme_css: str | None
    tokens: dict[str, str]
    quote_refs: list[str]
    preview_refs: list[str]
    releases_order: Literal["semver", "date"]
    tests: dict[str, TestDef]
    deny: list[str]


@dataclass
class Area:
    id: str
    title: str
    blurb: str
    parent: str | None


@dataclass
class TestRun:
    id: str | None
    cmd: str
    platform: str
    result: Literal["pass", "fail", "skip"]
    ran_at: str
    log: Path | None
    shots: list[Path]
    ci: str | None


@dataclass
class Note:
    path: Path
    slug: str
    release: str
    title: str
    summary: str
    areas: list[str]
    pr: int | None
    commit: str
    date: str
    hero: Path | None
    supersedes: list[str]
    tests: list[TestRun]
    body: str

    @property
    def key(self) -> str:
        return f"{self.release}/{self.slug}"


def _str_list(value: Any, what: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
        raise NotesError(f"{what} must be a list of strings")
    return list(value)


def _inside(root: Path, rel: Any, what: str) -> Path:
    if not isinstance(rel, str) or not rel or Path(rel).is_absolute():
        raise NotesError(f"{what} must be a relative path inside the project")
    path = (root / rel).resolve()
    if not path.is_relative_to(root.resolve()):
        raise NotesError(f"{what} must stay inside the project, got {rel!r}")
    return root / rel


def load_config(root: Path) -> Config:
    path = root / CONFIG_FILE
    if not path.is_file():
        raise NotesError(f"no {CONFIG_FILE} in {root}; run the skill's set-up first")
    try:
        data = parse_yaml(path.read_text()) or {}
    except NotesError as exc:
        raise NotesError(f"{CONFIG_FILE}: {exc}") from None
    if not isinstance(data, dict):
        raise NotesError(f"{CONFIG_FILE} must be a map of settings")

    def bad(msg: str) -> NotesError:
        return NotesError(f"{CONFIG_FILE}: {msg}")

    repo = data.get("repo")
    if not isinstance(repo, str) or not _REPO_RE.fullmatch(repo):
        raise bad("repo must be 'owner/name' on GitHub")
    try:
        notes_dir = _inside(root, data.get("notes_dir", "notes"), "notes_dir")
        out_dir = _inside(root, data.get("out_dir", "site/features"), "out_dir")
    except NotesError as exc:
        raise bad(str(exc)) from None
    if out_dir.resolve().is_relative_to(notes_dir.resolve()):
        raise bad("out_dir must not be inside notes_dir")
    base_url = data.get("base_url", "/")
    if not isinstance(base_url, str) or not base_url.startswith("/") or "//" in base_url:
        raise bad(
            "base_url must be the URL path the site is served at, starting with '/', "
            "like /features/"
        )
    if not base_url.endswith("/"):
        base_url += "/"
    theme = data.get("theme") or {}
    if not isinstance(theme, dict):
        raise bad("theme must be a map")
    if theme.get("css") is not None:
        try:
            _inside(root, theme["css"], "theme.css")
        except NotesError as exc:
            raise bad(str(exc)) from None
    tokens = theme.get("tokens") or {}
    if not isinstance(tokens, dict):
        raise bad("theme.tokens must be a map")
    order = data.get("releases_order", "semver")
    if order not in ("semver", "date"):
        raise bad("releases_order must be 'semver' or 'date'")
    tests: dict[str, TestDef] = {}
    for name, spec in (data.get("tests") or {}).items():
        if not isinstance(spec, dict) or not isinstance(spec.get("cmd"), str):
            raise bad(f"tests.{name} needs a 'cmd' string")
        env = spec.get("env") or {}
        tests[name] = TestDef(cmd=spec["cmd"], env={str(k): str(v) for k, v in env.items()})
    try:
        quote_refs = _str_list(data.get("quote_refs", ["main"]), "quote_refs")
        preview_refs = _str_list(data.get("preview_refs"), "preview_refs")
        deny = _str_list(data.get("deny"), "deny")
    except NotesError as exc:
        raise bad(str(exc)) from None
    for pattern in deny:
        try:
            re.compile(pattern)
        except re.error as exc:
            raise bad(f"deny pattern {pattern!r} is not a valid regex: {exc}") from None
    return Config(
        root=root,
        repo=repo,
        notes_dir=notes_dir,
        out_dir=out_dir,
        base_url=base_url,
        title=str(data.get("title", "What's new")),
        theme_css=theme.get("css"),
        tokens={str(k): str(v) for k, v in tokens.items()},
        quote_refs=quote_refs,
        preview_refs=preview_refs,
        releases_order=order,
        tests=tests,
        deny=deny,
    )


def load_areas(cfg: Config) -> dict[str, Area]:
    path = cfg.notes_dir / "areas.yml"
    rel = path.relative_to(cfg.root)
    if not path.is_file():
        raise NotesError(f"{rel}: missing; list the project's feature areas there")
    try:
        data = parse_yaml(path.read_text()) or []
    except NotesError as exc:
        raise NotesError(f"{rel}: {exc}") from None
    if not isinstance(data, list):
        raise NotesError(f"{rel}: must be a list of areas")
    areas: dict[str, Area] = {}
    for entry in data:
        if not isinstance(entry, dict) or not isinstance(entry.get("id"), str):
            raise NotesError(f"{rel}: every area needs an 'id'")
        aid = entry["id"]
        if not _AREA_RE.fullmatch(aid) or aid == "index":
            raise NotesError(
                f"{rel}: area id {aid!r} must be lower-case words joined by '-' and nested "
                "with '/', like fx/panel (and not 'index')"
            )
        if aid in areas:
            raise NotesError(f"{rel}: duplicate area '{aid}'")
        parent = entry.get("parent")
        areas[aid] = Area(
            aid,
            str(entry.get("title") or aid),
            str(entry.get("blurb") or ""),
            None if parent is None else str(parent),
        )
    for area in areas.values():
        if area.parent is not None and area.parent not in areas:
            raise NotesError(f"{rel}: area '{area.id}' has unknown parent '{area.parent}'")
        seen = {area.id}
        node = area
        while node.parent is not None:
            if node.parent in seen:
                raise NotesError(f"{rel}: area '{area.id}' is in a parent cycle")
            seen.add(node.parent)
            node = areas[node.parent]
    return areas


def area_path(areas: dict[str, Area], aid: str) -> list[Area]:
    """The area and its ancestors, root first."""
    chain = [areas[aid]]
    while chain[0].parent is not None:
        chain.insert(0, areas[chain[0].parent])
    return chain


def _has_symlink(folder: Path, path: Path) -> bool:
    """True when path, or any folder between the note folder and it, is a symlink."""
    node = path
    while node != folder and node != node.parent:
        if node.is_symlink():
            return True
        node = node.parent
    return False


def _evidence(folder: Path, rel: Any, what: str) -> Path:
    if not isinstance(rel, str) or not rel or Path(rel).is_absolute():
        raise NotesError(f"{what} must be a path inside the note folder")
    path = folder / rel
    if _has_symlink(folder, path):
        raise NotesError(f"{what} {rel!r} is a symlink; copy the file into the note folder")
    if not path.resolve().is_relative_to(folder.resolve()):
        raise NotesError(f"{what} must be inside the note folder, got {rel!r}")
    if not path.is_file():
        raise NotesError(f"{what} {rel!r} does not exist")
    return path


def _load_note(cfg: Config, areas: dict[str, Area], path: Path) -> Note:
    folder = path.parent
    meta, body = split_front_matter(path.read_text())
    missing = [
        k
        for k in ("title", "summary", "release", "areas", "commit", "date")
        if meta.get(k) in (None, "", [])
    ]
    if missing:
        raise NotesError(f"missing {', '.join(missing)}")
    release = str(meta["release"])
    if release != folder.parent.name:
        raise NotesError(f"release '{release}' must match its folder '{folder.parent.name}'")
    if not _SLUG_RE.fullmatch(folder.name):
        raise NotesError(f"folder name '{folder.name}' must be lower-case letters, digits, -._")
    note_areas = _str_list(meta["areas"], "areas")
    for aid in note_areas:
        if aid not in areas:
            raise NotesError(f"unknown area '{aid}' (add it to areas.yml)")
    if isinstance(meta["commit"], int):
        raise NotesError(
            "commit looks like a number; write the sha in quotes, like commit: '0451234'"
        )
    commit = str(meta["commit"]).lower()
    if not _SHA_RE.fullmatch(commit):
        raise NotesError(f"commit '{meta['commit']}' must be a git sha")
    date = str(meta["date"])
    if not _DATE_RE.fullmatch(date):
        raise NotesError(f"date '{date}' must be YYYY-MM-DD")
    pr = meta.get("pr")
    if pr is not None and not isinstance(pr, int):
        raise NotesError("pr must be a number")
    runs: list[TestRun] = []
    raw_tests = meta.get("tests") or []
    if not isinstance(raw_tests, list):
        raise NotesError("tests must be a list")
    for n, raw in enumerate(raw_tests, 1):
        where = f"tests[{n}]"
        if not isinstance(raw, dict):
            raise NotesError(f"{where} must be a map")
        tid = raw.get("id")
        cmd = raw.get("cmd")
        if cmd is None:
            if tid not in cfg.tests:
                raise NotesError(f"{where}: unknown test '{tid}' and no cmd given")
            cmd = cfg.tests[tid].cmd
        if raw.get("result") not in RESULTS:
            raise NotesError(f"{where}: result must be one of {', '.join(RESULTS)}")
        ran_at = raw.get("ran_at")
        if isinstance(ran_at, int):
            raise NotesError(f"{where}: ran_at looks like a number; write the sha in quotes")
        if not isinstance(ran_at, str) or not _SHA_RE.fullmatch(ran_at.lower()):
            raise NotesError(f"{where}: ran_at (the commit the test ran at) is required")
        runs.append(
            TestRun(
                id=None if tid is None else str(tid),
                cmd=str(cmd),
                platform=str(raw.get("platform") or ""),
                result=raw["result"],
                ran_at=ran_at.lower(),
                log=None
                if raw.get("log") is None
                else _evidence(folder, raw["log"], f"{where}.log"),
                shots=[
                    _evidence(folder, s, f"{where}.shots")
                    for s in _str_list(raw.get("shots"), f"{where}.shots")
                ],
                ci=None if raw.get("ci") is None else str(raw["ci"]),
            )
        )
    hero = meta.get("hero")
    return Note(
        path=path,
        slug=folder.name,
        release=release,
        title=str(meta["title"]),
        summary=str(meta["summary"]),
        areas=note_areas,
        pr=pr,
        commit=commit,
        date=date,
        hero=None if hero is None else _evidence(folder, hero, "hero"),
        supersedes=_str_list(meta.get("supersedes"), "supersedes"),
        tests=runs,
        body=body,
    )


def load_notes(
    cfg: Config, areas: dict[str, Area], problems: list[str] | None = None
) -> list[Note]:
    """Load every `<notes_dir>/<release>/<slug>/note.md`.

    With `problems`, broken notes are reported there and skipped; otherwise the first
    problem raises. Every message starts with the note's path relative to the project.
    """
    notes: list[Note] = []

    def report(msg: str) -> None:
        if problems is None:
            raise NotesError(msg)
        problems.append(msg)

    for path in sorted(cfg.notes_dir.glob("*/*/note.md")):
        rel = path.relative_to(cfg.root).as_posix()
        try:
            notes.append(_load_note(cfg, areas, path))
        except NotesError as exc:
            report(f"{rel}: {exc}")
    keys = {n.key for n in notes}
    for note in notes:
        for target in note.supersedes:
            if target not in keys:
                rel = note.path.relative_to(cfg.root).as_posix()
                report(f"{rel}: supersedes '{target}', which is not a note")
    return notes


def release_key(cfg: Config, release: str, notes: list[Note]) -> tuple:
    """Sort key for a release; larger is newer."""
    if cfg.releases_order == "date":
        dates = [n.date for n in notes if n.release == release]
        return (max(dates) if dates else "", release)
    main_part, _, pre = release.partition("-")
    nums = tuple(int(p) if p.isdigit() else -1 for p in main_part.split("."))
    return (nums, 0 if pre else 1, pre)


# --- markdown ------------------------------------------------------------------------------
#
# A small, escaping Markdown subset: headings, paragraphs, lists (nested), blockquotes,
# fenced code, tables, rules, and inline code, bold, emphasis, links and images. Quote
# markers ({{quote path#La-Lb "caption"}}) on a line of their own call a resolver.


@dataclass(frozen=True)
class QuoteRef:
    path: str
    start: int
    end: int
    caption: str


QUOTE_RE = re.compile(r'\{\{quote\s+(.+?)#L(\d+)(?:-L(\d+))?(?:\s+"([^"]*)")?\s*\}\}')
_FENCE_RE = re.compile(r"(`{3,})\s*([\w+-]*)[^`]*")
_HEADING_RE = re.compile(r"(#{1,4})\s+(.*?)\s*#*")
_RULE_RE = re.compile(r"(-{3,}|\*{3,}|_{3,})")
_ITEM_RE = re.compile(r"(\s*)([-*+]|\d+[.)])\s+(.*)")
_TABLE_SEP_RE = re.compile(r"\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?")
_SCHEME_RE = re.compile(r"[a-zA-Z][a-zA-Z0-9+.-]*:")
_INLINE_RE = re.compile(
    r"`([^`]+)`"
    r"|!\[([^\]]*)\]\(([^)\s]*)\)"
    r"|\[([^\]]+)\]\(([^)\s]*)\)"
    r"|\*\*(.+?)\*\*"
    r"|\*([^*\s][^*]*?)\*"
)
SAFE_SCHEMES = ("http", "https", "mailto")


def _esc(text: str) -> str:
    return html.escape(text, quote=True)


def _safe_url(url: str, link_base: str) -> str | None:
    if not url:
        return None
    if _SCHEME_RE.match(url):
        return url if url.split(":", 1)[0].lower() in SAFE_SCHEMES else None
    if url.startswith(("/", "#")):
        return url
    return link_base + url


def _inline(text: str, link_base: str) -> str:
    out: list[str] = []
    last = 0
    for m in _INLINE_RE.finditer(text):
        out.append(_esc(text[last : m.start()]))
        last = m.end()
        code, img_alt, img_url, link_text, link_url, bold, em = m.groups()
        if code is not None:
            out.append(f"<code>{_esc(code)}</code>")
        elif img_url is not None:
            url = _safe_url(img_url, link_base)
            alt = _esc(img_alt or "")
            out.append(f'<img src="{_esc(url)}" alt="{alt}">' if url else alt)
        elif link_text is not None:
            url = _safe_url(link_url or "", link_base)
            inner = _inline(link_text, link_base)
            out.append(f'<a href="{_esc(url)}">{inner}</a>' if url else inner)
        elif bold is not None:
            out.append(f"<strong>{_inline(bold, link_base)}</strong>")
        else:
            out.append(f"<em>{_inline(em, link_base)}</em>")
    out.append(_esc(text[last:]))
    return "".join(out)


def _cells(line: str) -> list[str]:
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in line.split("|")]


def _starts_block(lines: list[str], i: int) -> bool:
    line = lines[i]
    stripped = line.strip()
    return bool(
        not stripped
        or _FENCE_RE.fullmatch(stripped)
        or _HEADING_RE.fullmatch(stripped)
        or _RULE_RE.fullmatch(stripped)
        or QUOTE_RE.fullmatch(stripped)
        or stripped.startswith(">")
        or _ITEM_RE.fullmatch(line)
        or (
            stripped.startswith("|")
            and i + 1 < len(lines)
            and _TABLE_SEP_RE.fullmatch(lines[i + 1].strip())
        )
    )


def _list(
    lines: list[str], i: int, link_base: str, quote: Callable[[QuoteRef], str]
) -> tuple[str, int]:
    first = _ITEM_RE.fullmatch(lines[i])
    assert first is not None
    indent = len(first.group(1))
    ordered = first.group(2)[0].isdigit()
    items: list[str] = []
    while i < len(lines):
        m = _ITEM_RE.fullmatch(lines[i])
        if not m or len(m.group(1)) != indent or m.group(2)[0].isdigit() != ordered:
            break
        text = [m.group(3)]
        i += 1
        children: list[str] = []
        while i < len(lines) and lines[i].strip():
            sub = _ITEM_RE.fullmatch(lines[i])
            if sub and len(sub.group(1)) > indent:
                html_child, i = _list(lines, i, link_base, quote)
                children.append(html_child)
                continue
            if sub or len(lines[i]) - len(lines[i].lstrip()) <= indent:
                break
            if fence := _FENCE_RE.fullmatch(lines[i].strip()):
                # A fenced block indented under the item belongs to the item.
                block = [lines[i]]
                i += 1
                while i < len(lines) and not lines[i].strip().startswith(fence.group(1)):
                    block.append(lines[i])
                    i += 1
                if i < len(lines):
                    block.append(lines[i])
                    i += 1
                cut = min(len(b) - len(b.lstrip()) for b in block if b.strip())
                children.append("".join(_blocks([b[cut:] for b in block], quote, link_base)))
                continue
            text.append(lines[i].strip())
            i += 1
        items.append(f"<li>{_inline(' '.join(text), link_base)}{''.join(children)}</li>")
        while i < len(lines) and not lines[i].strip():
            nxt = i + 1
            m2 = _ITEM_RE.fullmatch(lines[nxt]) if nxt < len(lines) else None
            if m2 and len(m2.group(1)) == indent and m2.group(2)[0].isdigit() == ordered:
                i = nxt
            else:
                break
    tag = "ol" if ordered else "ul"
    return f"<{tag}>{''.join(items)}</{tag}>", i


def _blocks(lines: list[str], quote: Callable[[QuoteRef], str], link_base: str) -> list[str]:
    out: list[str] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped:
            i += 1
            continue
        if fence := _FENCE_RE.fullmatch(stripped):
            ticks, lang = fence.group(1), fence.group(2)
            body: list[str] = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith(ticks):
                body.append(lines[i])
                i += 1
            i += 1
            cls = f' class="language-{_esc(lang)}"' if lang else ""
            out.append(f"<pre><code{cls}>{_esc(chr(10).join(body))}</code></pre>")
            continue
        if m := QUOTE_RE.fullmatch(stripped):
            start = int(m.group(2))
            end = int(m.group(3)) if m.group(3) else start
            out.append(quote(QuoteRef(m.group(1).strip(), start, end, m.group(4) or "")))
            i += 1
            continue
        if m := _HEADING_RE.fullmatch(stripped):
            level = len(m.group(1))
            out.append(f"<h{level}>{_inline(m.group(2), link_base)}</h{level}>")
            i += 1
            continue
        if _RULE_RE.fullmatch(stripped):
            out.append("<hr>")
            i += 1
            continue
        if stripped.startswith(">"):
            inner: list[str] = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                inner.append(re.sub(r"^\s*>\s?", "", lines[i]))
                i += 1
            out.append(f"<blockquote>{''.join(_blocks(inner, quote, link_base))}</blockquote>")
            continue
        if _ITEM_RE.fullmatch(line):
            block, i = _list(lines, i, link_base, quote)
            out.append(block)
            continue
        if (
            stripped.startswith("|")
            and i + 1 < len(lines)
            and _TABLE_SEP_RE.fullmatch(lines[i + 1].strip())
        ):
            head = "".join(f"<th>{_inline(c, link_base)}</th>" for c in _cells(line))
            rows: list[str] = []
            i += 2
            while i < len(lines) and lines[i].strip().startswith("|"):
                cells = "".join(f"<td>{_inline(c, link_base)}</td>" for c in _cells(lines[i]))
                rows.append(f"<tr>{cells}</tr>")
                i += 1
            out.append(
                f"<table><thead><tr>{head}</tr></thead><tbody>{''.join(rows)}</tbody></table>"
            )
            continue
        para = [stripped]
        i += 1
        while i < len(lines) and not _starts_block(lines, i):
            para.append(lines[i].strip())
            i += 1
        out.append(f"<p>{_inline(' '.join(para), link_base)}</p>")
    return out


def render_markdown(text: str, *, quote: Callable[[QuoteRef], str], link_base: str) -> str:
    """Render the Markdown subset to HTML. All text is escaped; only http(s)/mailto,
    absolute-path and fragment URLs are kept as is, and relative ones get `link_base`."""
    return "\n".join(_blocks(text.splitlines(), quote, link_base))


# --- git -----------------------------------------------------------------------------------

QUOTE_MAX_LINES = 40
Visibility = Literal["public", "preview", "draft"]
_PRIVATE_ALLOW = ("/home/runner/", "/Users/runner/")  # GitHub-hosted runner homes
_PRIVATE_RES = (
    re.compile(r"/Users/[^/\s]+/"),
    re.compile(r"/home/[^/\s]+/"),
    re.compile(r"[A-Za-z]:\\Users\\"),
)


class ToolError(Exception):
    """git or gh is missing or unusable: an environment problem (exit 2)."""


def _run(tool: str, args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run([tool, *args], cwd=cwd, capture_output=True, text=True)
    except FileNotFoundError:
        raise ToolError(f"{tool} is not installed or not on PATH") from None


def _git(args: list[str], cwd: Path) -> str:
    result = _run("git", args, cwd)
    if result.returncode != 0:
        raise NotesError(f"git {' '.join(args)}: {result.stderr.strip() or 'failed'}")
    return result.stdout.strip()


def _git_ok(args: list[str], cwd: Path) -> bool:
    return _run("git", args, cwd).returncode == 0


def _gh(args: list[str], cwd: Path) -> str:
    result = _run("gh", args, cwd)
    if result.returncode != 0:
        raise NotesError(f"gh {' '.join(args)}: {result.stderr.strip() or 'failed'}")
    return result.stdout.strip()


def _rel(cfg: Config, note: Note) -> str:
    return note.path.relative_to(cfg.root).as_posix()


def full_sha(cfg: Config, note: Note, sha: str) -> str:
    """Resolve a (short) sha, explaining what to fetch when it isn't in the clone."""
    if _git_ok(["rev-parse", "--quiet", "--verify", f"{sha}^{{commit}}"], cfg.root):
        return _git(["rev-parse", f"{sha}^{{commit}}"], cfg.root)
    if _git(["rev-parse", "--is-shallow-repository"], cfg.root) == "true":
        raise NotesError(
            f"{_rel(cfg, note)}: commit {sha} is not in this shallow clone; check out with "
            "full history (actions/checkout `fetch-depth: 0`) and fetch the PR heads "
            "(`git fetch origin '+refs/pull/*/head:refs/remotes/origin/pr/*'`)"
        )
    raise NotesError(
        f"{_rel(cfg, note)}: commit {sha} not found; fetch the branches and PR heads "
        "(`git fetch origin '+refs/pull/*/head:refs/remotes/origin/pr/*'`)"
    )


def _on(cfg: Config, sha: str, ref: str) -> bool:
    return _git_ok(["merge-base", "--is-ancestor", sha, ref], cfg.root)


def note_visibility(cfg: Config, note: Note, *, draft: bool) -> Visibility:
    """Public when the pinned commit is on a pushed quote ref or is a merged PR's head;
    preview when it is on a preview ref; draft (only with draft=True) otherwise."""
    sha = full_sha(cfg, note, note.commit)
    if any(_on(cfg, sha, f"refs/remotes/origin/{ref}") for ref in cfg.quote_refs):
        return "public"
    if any(_on(cfg, sha, f"refs/remotes/origin/{ref}") for ref in cfg.preview_refs):
        return "preview"
    if note.pr is not None and _on(cfg, sha, f"refs/remotes/origin/pr/{note.pr}"):
        # A squash merge leaves the PR head off every branch; where the merge commit landed
        # decides whether the note is public, a preview, or neither.
        out = _gh(
            [
                "pr",
                "view",
                str(note.pr),
                "--repo",
                cfg.repo,
                "--json",
                "state,mergeCommit",
                "-q",
                '.state + " " + (.mergeCommit.oid // "")',
            ],
            cfg.root,
        ).split()
        if len(out) == 2 and out[0].upper() == "MERGED":
            merge = out[1]
            if any(_on(cfg, merge, f"refs/remotes/origin/{ref}") for ref in cfg.quote_refs):
                return "public"
            if any(_on(cfg, merge, f"refs/remotes/origin/{ref}") for ref in cfg.preview_refs):
                return "preview"
    if draft:
        return "draft"
    refs = ", ".join(cfg.quote_refs + cfg.preview_refs) or "no quote_refs"
    where = f" or the head of PR #{note.pr} merged into one of them" if note.pr is not None else ""
    raise NotesError(
        f"{_rel(cfg, note)}: commit {note.commit[:7]} is not on a pushed branch ({refs}){where}"
        " (use --draft to preview locally)"
    )


def read_quote(cfg: Config, note: Note, ref: QuoteRef) -> list[str]:
    """The quoted lines, read from git at the note's commit."""
    sha = full_sha(cfg, note, note.commit)
    short = sha[:7]
    count = ref.end - ref.start + 1
    if ref.start < 1 or count < 1:
        raise NotesError(f"{_rel(cfg, note)}: quote range L{ref.start}-L{ref.end} is empty")
    if count > QUOTE_MAX_LINES:
        raise NotesError(
            f"{_rel(cfg, note)}: {ref.path}: quote is {count} lines; the limit is {QUOTE_MAX_LINES}"
        )
    result = _run("git", ["show", f"{sha}:{ref.path}"], cfg.root)
    if result.returncode != 0:
        raise NotesError(f"{_rel(cfg, note)}: {ref.path} does not exist at {short}")
    lines = result.stdout.splitlines()
    if ref.end > len(lines):
        raise NotesError(
            f"{_rel(cfg, note)}: L{ref.start}-L{ref.end} is outside {ref.path} "
            f"({len(lines)} lines) at {short}"
        )
    return lines[ref.start - 1 : ref.end]


def permalink(cfg: Config, commit: str, ref: QuoteRef) -> str:
    sha = _git(["rev-parse", f"{commit}^{{commit}}"], cfg.root)
    lines = f"L{ref.start}" if ref.start == ref.end else f"L{ref.start}-L{ref.end}"
    return f"https://github.com/{cfg.repo}/blob/{sha}/{quote_url(ref.path)}#{lines}"


def quote_url(path: str) -> str:
    return urllib.parse.quote(path, safe="/")


def scan_private(cfg: Config, text: str) -> list[str]:
    """Private material in text: home-folder paths, this machine's $HOME, and config `deny`."""
    found: list[str] = []
    patterns = [*_PRIVATE_RES, *(re.compile(p) for p in cfg.deny)]
    for pattern in patterns:
        for m in pattern.finditer(text):
            if m.group(0) in _PRIVATE_ALLOW:
                continue
            if m.group(0) not in found:
                found.append(m.group(0))
    home = os.environ.get("HOME", "")
    if len(home) > 1 and home in text and home not in found:
        found.append(home)
    return found


# --- pages ---------------------------------------------------------------------------------

_TOKEN_KEY_RE = re.compile(r"[a-z][a-z-]*")
_TOKEN_BAD = set(";{}<>\\")
CRUMB_SEP = " \u203a "  # single right-pointing angle quotation mark
DRAFT_BANNER = '<div class="draft-banner">Draft — not for publication</div>'


def templates_dir() -> Path:
    """`templates/` beside the script (vendored copy) or beside `scripts/` (the skill)."""
    here = Path(__file__).resolve().parent
    for candidate in (here / "templates", here.parent / "templates"):
        if candidate.is_dir():
            return candidate
    raise ToolError(f"templates/ not found next to {here}")


def _template(name: str) -> string.Template:
    return string.Template((templates_dir() / name).read_text())


def note_url(cfg: Config, note: Note) -> str:
    return f"{cfg.base_url}{quote_url(note.release)}/{quote_url(note.slug)}/"


def asset_url(cfg: Config, note: Note, path: Path) -> str:
    return note_url(cfg, note) + quote_url(path.relative_to(note.path.parent).as_posix())


def release_url(cfg: Config, release: str) -> str:
    return f"{cfg.base_url}releases/{quote_url(release)}.html"


def area_url(cfg: Config, aid: str) -> str:
    return f"{cfg.base_url}areas/{quote_url(aid)}.html"


def _token_css(cfg: Config) -> str:
    decls = []
    for key, value in cfg.tokens.items():
        if not _TOKEN_KEY_RE.fullmatch(key) or _TOKEN_BAD & set(value):
            raise NotesError(
                f"{CONFIG_FILE}: theme token {key!r}: {value!r} is not a plain CSS value"
            )
        decls.append(f"--{key}: {value};")
    return f"<style>:root {{ {' '.join(decls)} }}</style>" if decls else ""


def _page(cfg: Config, title: str, description: str, content: str, *, draft: bool) -> str:
    head = []
    if cfg.theme_css:
        head.append(f'<link rel="stylesheet" href="{_esc(cfg.base_url)}site.css">')
    head.append(_token_css(cfg))
    page_title = title if title == cfg.title else f"{title} · {cfg.title}"
    return _template("base.html").substitute(
        page_title=_esc(page_title),
        description=_esc(description),
        base=_esc(cfg.base_url),
        head_extra="\n".join(h for h in head if h),
        draft_banner=DRAFT_BANNER if draft else "",
        site_title=_esc(cfg.title),
        content=content,
        version=_esc(VERSION),
        repo=_esc(cfg.repo),
    )


def _badges(cfg: Config, note: Note, vis: Visibility) -> str:
    out = [
        f'<a class="badge release" href="{_esc(release_url(cfg, note.release))}">'
        f"{_esc(note.release)}</a>"
    ]
    if vis == "preview":
        out.append('<span class="badge preview">Preview: unreleased</span>')
    elif vis == "draft":
        out.append('<span class="badge draft">Draft</span>')
    return " ".join(out)


def _expanded_areas(areas: dict[str, Area], ids: list[str]) -> list[str]:
    out: list[str] = []
    for aid in ids:
        for area in area_path(areas, aid):
            if area.id not in out:
                out.append(area.id)
    return out


def render_quote_html(cfg: Config, note: Note, ref: QuoteRef, lines: list[str]) -> str:
    link = permalink(cfg, note.commit, ref)
    span = f"L{ref.start}" if ref.start == ref.end else f"L{ref.start}-L{ref.end}"
    label = f"{ref.path}#{span} @ {note.commit[:7]}"
    numbered = "\n".join(
        f'<span class="ln">{ref.start + n}</span>{_esc(text)}' for n, text in enumerate(lines)
    )
    caption = f"{_esc(ref.caption)} " if ref.caption else ""
    return (
        f'<figure class="quote"><figcaption>{caption}<a href="{_esc(link)}">{_esc(label)}</a>'
        f"</figcaption><pre><code>{numbered}</code></pre></figure>"
    )


def _run_html(cfg: Config, note: Note, run: TestRun) -> str:
    mismatch = run.ran_at[:7] != note.commit[:7]
    cls = "ran-at ran-at-mismatch" if mismatch else "ran-at"
    title = (
        f' title="The note is pinned to {_esc(note.commit[:7])}; this test ran at '
        f'{_esc(run.ran_at[:7])}."'
        if mismatch
        else ""
    )
    parts = [
        '<div class="run"><div class="run-head">',
        f'<span class="result {run.result}">{run.result}</span>',
        f"<code>{_esc(run.cmd)}</code>",
    ]
    if run.platform:
        parts.append(f'<span class="platform">{_esc(run.platform)}</span>')
    parts.append(f'<span class="{cls}"{title}>ran at {_esc(run.ran_at[:7])}</span>')
    if run.ci:
        url = _safe_url(run.ci, "")
        if url:
            parts.append(f'<a href="{_esc(url)}">CI run</a>')
    parts.append("</div>")
    if run.log is not None:
        parts.append(
            f"<details><summary>Log</summary><pre>{_esc(run.log.read_text())}</pre></details>"
        )
    if run.shots:
        shots = "".join(
            f'<a href="{_esc(asset_url(cfg, note, s))}"><img src="'
            f'{_esc(asset_url(cfg, note, s))}" alt="{_esc(s.name)}" loading="lazy">'
            "</a>"
            for s in run.shots
        )
        parts.append(f'<div class="shots">{shots}</div>')
    parts.append("</div>")
    return "".join(parts)


def render_note_page(
    cfg: Config,
    note: Note,
    areas: dict[str, Area],
    vis: Visibility,
    body_html: str,
    *,
    superseded_by: list[Note] | tuple[()] = (),
) -> str:
    crumbs = ""
    if note.areas:
        crumbs = CRUMB_SEP.join(
            f'<a href="{_esc(area_url(cfg, a.id))}">{_esc(a.title)}</a>'
            for a in area_path(areas, note.areas[0])
        )
    pr_link = ""
    if note.pr is not None:
        pr_link = f'<a href="https://github.com/{_esc(cfg.repo)}/pull/{note.pr}">PR #{note.pr}</a>'
    superseded = ""
    if superseded_by:
        links = ", ".join(
            f'<a href="{_esc(note_url(cfg, n))}">{_esc(n.title)}</a>' for n in superseded_by
        )
        superseded = f'<p class="notice">Superseded by {links}.</p>'
    hero = ""
    if note.hero is not None:
        hero = f'<img class="hero" src="{_esc(asset_url(cfg, note, note.hero))}" alt="">'
    tests = ""
    if note.tests:
        runs = "".join(_run_html(cfg, note, r) for r in note.tests)
        tests = f'<section class="tests"><h2>Tested</h2>{runs}</section>'
    content = _template("note.html").substitute(
        crumbs=crumbs,
        title=_esc(note.title),
        summary=_esc(note.summary),
        badges=_badges(cfg, note, vis),
        date=_esc(note.date),
        pr_link=pr_link,
        superseded=superseded,
        hero=hero,
        body=body_html,
        tests=tests,
    )
    return _page(cfg, note.title, note.summary, content, draft=vis == "draft")


def _card(
    cfg: Config,
    note: Note,
    vis: Visibility,
    areas: dict[str, Area],
    superseded: bool,
    older: bool = False,
) -> str:
    expanded = _expanded_areas(areas, note.areas)
    search = f"{note.title} {note.summary} {' '.join(areas[a].title for a in expanded)}".lower()
    thumb = ""
    if note.hero is not None:
        thumb = (
            f'<img class="thumb" src="{_esc(asset_url(cfg, note, note.hero))}" alt="" '
            'loading="lazy">'
        )
    cls = "card" + (" superseded" if superseded else "") + (" older" if older else "")
    return (
        f'<article class="{cls}" data-areas="{_esc(" ".join(expanded))}" '
        f'data-search="{_esc(search)}">{thumb}'
        f'<h3><a href="{_esc(note_url(cfg, note))}">{_esc(note.title)}</a></h3>'
        f"<p>{_esc(note.summary)}</p>"
        f'<p class="meta">{_badges(cfg, note, vis)} <span class="date">{_esc(note.date)}</span>'
        "</p></article>"
    )


def render_list_page(
    cfg: Config,
    title: str,
    intro_html: str,
    notes: list[tuple[Note, Visibility]],
    areas: dict[str, Area],
    *,
    draft_any: bool,
    superseded: set[str] | None = None,
    older: set[str] | None = None,
) -> str:
    used: list[str] = []
    for note, _ in notes:
        for aid in _expanded_areas(areas, note.areas):
            if aid not in used:
                used.append(aid)
    chips = "".join(
        f'<button class="chip" type="button" data-area="{_esc(aid)}">'
        f"{_esc(CRUMB_SEP.join(a.title for a in area_path(areas, aid)))}</button>"
        for aid in sorted(used)
    )
    cards = "\n".join(
        _card(cfg, n, v, areas, n.key in (superseded or set()), n.key in (older or set()))
        for n, v in notes
    )
    content = _template("list.html").substitute(
        heading=_esc(title), intro=intro_html, chips=chips, cards=cards
    )
    return _page(cfg, title, title, content, draft=draft_any)


# --- build ---------------------------------------------------------------------------------

_LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)\)")
_CODE_RE = re.compile(r"<pre>.*?</pre>|<code>.*?</code>", re.S)


def _body_files(cfg: Config, note: Note, problems: list[str]) -> list[Path]:
    """Files inside the note folder that the body links to (outside fenced code)."""
    folder = note.path.parent
    text = re.sub(r"(?ms)^\s*(`{3,}).*?^\s*\1", "", note.body)
    files: list[Path] = []
    for url in _LINK_RE.findall(text):
        if _SCHEME_RE.match(url) or url.startswith(("/", "#")):
            continue
        rel_url = urllib.parse.unquote(url.split("#")[0].split("?")[0])
        if not rel_url or rel_url.endswith("/"):
            continue
        path = folder / rel_url
        if not path.resolve().is_relative_to(folder.resolve()) and not _has_symlink(folder, path):
            continue  # a link to another page of the site
        if _has_symlink(folder, path):
            problems.append(f"{_rel(cfg, note)}: {rel_url} is a symlink; copy the file in")
        elif not path.is_file():
            problems.append(
                f"{_rel(cfg, note)}: links to {rel_url}, which is not in the note folder"
            )
        else:
            files.append(path)
    return files


def _published_files(note: Note, body_files: list[Path]) -> list[Path]:
    files = [*([note.hero] if note.hero else []), *body_files]
    for run in note.tests:
        files += [*([run.log] if run.log else []), *run.shots]
    return list(dict.fromkeys(files))


OUTPUT_MARKER = ".feature-notes-output"
LOG_MAX_LINES = 200


@dataclass
class BuildReport:
    notes: int = 0
    public: int = 0
    preview: int = 0
    draft: int = 0
    problems: list[str] = field(default_factory=list)


def _newest_first(notes: list[tuple[Note, Visibility]]) -> list[tuple[Note, Visibility]]:
    return sorted(notes, key=lambda nv: (nv[0].date, nv[0].title), reverse=True)


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def _clear_out_dir(cfg: Config) -> None:
    out = cfg.out_dir
    if out.exists():
        if not out.is_dir() or (any(out.iterdir()) and not (out / OUTPUT_MARKER).exists()):
            rel = out.relative_to(cfg.root).as_posix()
            raise NotesError(
                f"refusing to clear {rel}: it is not empty and was not created by feature-notes "
                f"(no {OUTPUT_MARKER}); point out_dir somewhere else or empty it"
            )
        shutil.rmtree(out)
    out.mkdir(parents=True)


def _area_tree_html(cfg: Config, areas: dict[str, Area], counts: dict[str, int]) -> str:
    def branch(parent: str | None) -> str:
        kids = sorted((a for a in areas.values() if a.parent == parent), key=lambda a: a.title)
        if not kids:
            return ""
        items = []
        for a in kids:
            n = counts.get(a.id, 0)
            items.append(
                f'<li><a href="{_esc(area_url(cfg, a.id))}">{_esc(a.title)}</a> '
                f'<span class="count">({n})</span>'
                + (f'<div class="blurb">{_esc(a.blurb)}</div>' if a.blurb else "")
                + branch(a.id)
                + "</li>"
            )
        return f"<ul>{''.join(items)}</ul>"

    return f'<div class="tree">{branch(None)}</div>'


def build(cfg: Config, *, draft: bool, write: bool = True) -> BuildReport:
    """Validate every note, then (when write and there are no problems) write the site."""
    report = BuildReport()
    areas = load_areas(cfg)
    notes = load_notes(cfg, areas, problems=report.problems)
    if cfg.theme_css and not (cfg.root / cfg.theme_css).is_file():
        report.problems.append(f"{CONFIG_FILE}: theme.css {cfg.theme_css} does not exist")
    _token_css(cfg)

    rendered: list[tuple[Note, Visibility, str, list[Path]]] = []
    for note in notes:
        rel = _rel(cfg, note)
        try:
            vis = note_visibility(cfg, note, draft=draft)
        except NotesError as exc:
            report.problems.append(str(exc))
            continue
        quote_problems: list[str] = []

        def quote(ref: QuoteRef, note: Note = note, sink: list[str] = quote_problems) -> str:
            try:
                return render_quote_html(cfg, note, ref, read_quote(cfg, note, ref))
            except NotesError as exc:
                sink.append(str(exc))
                return ""

        body = render_markdown(note.body, quote=quote, link_base=note_url(cfg, note))
        report.problems.extend(quote_problems)
        if "{{quote" in _CODE_RE.sub("", body):
            report.problems.append(
                f"{rel}: a {{{{quote ...}}}} marker must be on its own line, outside lists"
            )
        files = _published_files(note, _body_files(cfg, note, report.problems))
        for found in scan_private(cfg, note.path.read_text()):
            report.problems.append(f"{rel}: private material in a public note: {found}")
        for path in files:
            try:
                text = path.read_text()
            except (UnicodeDecodeError, OSError):
                continue  # binary: images and the like
            name = path.relative_to(note.path.parent).as_posix()
            for found in scan_private(cfg, text):
                report.problems.append(f"{rel}: private material in {name}: {found}")
        rendered.append((note, vis, body, files))

    report.notes = len(notes)
    for _, vis, _, _ in rendered:
        setattr(report, vis, getattr(report, vis) + 1)
    if report.problems or not write:
        return report

    _clear_out_dir(cfg)
    out = cfg.out_dir
    by_key = {n.key: n for n, _, _, _ in rendered}
    superseded_by: dict[str, list[Note]] = {}
    for n, _, _, _ in rendered:
        for target in n.supersedes:
            superseded_by.setdefault(target, []).append(n)
    superseded = set(superseded_by)
    all_nv = [(n, v) for n, v, _, _ in rendered]

    for note, vis, body, files in rendered:
        folder = out / note.release / note.slug
        for path in files:
            target = folder / path.relative_to(note.path.parent)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
        page = render_note_page(
            cfg, note, areas, vis, body, superseded_by=superseded_by.get(note.key, [])
        )
        _write(folder / "index.html", page)

    releases = sorted(
        {n.release for n in by_key.values()},
        key=lambda r: release_key(cfg, r, list(by_key.values())),
        reverse=True,
    )

    def page(
        path: Path,
        title: str,
        intro: str,
        nv: list[tuple[Note, Visibility]],
        older: set[str] | None = None,
        ordered: bool = False,
    ) -> None:
        nv = nv if ordered else _newest_first(nv)
        _write(
            path,
            render_list_page(
                cfg,
                title,
                intro,
                nv,
                areas,
                draft_any=any(v == "draft" for _, v in nv),
                superseded=superseded,
                older=older,
            ),
        )

    if releases:
        latest = releases[0]
        intro = (
            f'<p class="meta">Release <a href="{_esc(release_url(cfg, latest))}">'
            f'{_esc(latest)}</a> · <a href="{_esc(cfg.base_url)}releases/">All releases'
            f'</a> · <a href="{_esc(cfg.base_url)}areas/">Browse by area</a></p>'
        )
        # Every note is on the landing page so search finds them all; only the latest
        # release shows until the reader searches or picks an area.
        rank = {r: i for i, r in enumerate(releases)}
        everything = sorted(_newest_first(all_nv), key=lambda nv: rank[nv[0].release])
        older = {n.key for n, _ in all_nv if n.release != latest}
        page(out / "index.html", cfg.title, intro, everything, older=older, ordered=True)
    else:
        page(out / "index.html", cfg.title, "<p>No notes yet.</p>", [])
    rows = "".join(
        f'<li><a href="{_esc(release_url(cfg, r))}">{_esc(r)}</a> '
        f'<span class="count">({sum(1 for n, _ in all_nv if n.release == r)})</span></li>'
        for r in releases
    )
    _write(
        out / "releases" / "index.html",
        _page(
            cfg,
            "Releases",
            "All releases",
            f'<h1>Releases</h1><ul class="releases">{rows}</ul>',
            draft=any(v == "draft" for _, v in all_nv),
        ),
    )
    for r in releases:
        page(
            out / "releases" / f"{r}.html",
            f"Release {r}",
            "",
            [nv for nv in all_nv if nv[0].release == r],
        )

    def in_area(note: Note, aid: str) -> bool:
        return any(aid in (a.id for a in area_path(areas, x)) for x in note.areas)

    counts = {aid: sum(1 for n, _ in all_nv if in_area(n, aid)) for aid in areas}
    _write(
        out / "areas" / "index.html",
        _page(
            cfg,
            "Areas",
            "Browse by area",
            f"<h1>Areas</h1>{_area_tree_html(cfg, areas, counts)}",
            draft=any(v == "draft" for _, v in all_nv),
        ),
    )
    for aid, area in areas.items():
        kids = [a for a in areas.values() if a.parent == aid]
        intro = f"<p>{_esc(area.blurb)}</p>" if area.blurb else ""
        if area.parent:
            parent = areas[area.parent]
            intro = (
                f'<p class="crumbs">In <a href="{_esc(area_url(cfg, parent.id))}">'
                f"{_esc(parent.title)}</a></p>"
            ) + intro
        if kids:
            links = ", ".join(
                f'<a href="{_esc(area_url(cfg, k.id))}">{_esc(k.title)}</a>'
                for k in sorted(kids, key=lambda a: a.title)
            )
            intro += f"<p>Sub-areas: {links}</p>"
        page(
            out / "areas" / f"{aid}.html",
            area.title,
            intro,
            [nv for nv in all_nv if in_area(nv[0], aid)],
        )

    tdir = templates_dir()
    shutil.copy(tdir / "theme.css", out / "theme.css")
    shutil.copy(tdir / "search.js", out / "search.js")
    if cfg.theme_css:
        shutil.copy(cfg.root / cfg.theme_css, out / "site.css")
    index = [
        {
            "title": n.title,
            "summary": n.summary,
            "release": n.release,
            "areas": n.areas,
            "url": note_url(cfg, n),
            "visibility": v,
            "date": n.date,
        }
        for n, v in _newest_first(all_nv)
    ]
    _write(out / "index.json", json.dumps(index, indent=1, ensure_ascii=False) + "\n")
    _write(out / OUTPUT_MARKER, f"feature-notes {VERSION}\n")
    return report


def serve_path(cfg: Config, url_path: str) -> Path | None:
    """The file under out_dir that a request for url_path serves, or None."""
    path = urllib.parse.unquote(urllib.parse.urlsplit(url_path).path)
    if not path.startswith(cfg.base_url):
        return None
    rel = path[len(cfg.base_url) :]
    target = (cfg.out_dir / rel).resolve()
    if not target.is_relative_to(cfg.out_dir.resolve()):
        return None
    if rel == "" or rel.endswith("/"):
        target = target / "index.html"
    return cfg.out_dir / target.relative_to(cfg.out_dir.resolve())


# --- cli -----------------------------------------------------------------------------------


def _root(arg: str | None) -> Path:
    if arg:
        return Path(arg).resolve()
    result = _run("git", ["rev-parse", "--show-toplevel"], Path.cwd())
    return Path(result.stdout.strip()) if result.returncode == 0 else Path.cwd()


def _report(cfg: Config, report: BuildReport, verb: str) -> int:
    for problem in report.problems:
        print(f"error: {problem}", file=sys.stderr)
    if report.problems:
        print(f"{len(report.problems)} problem(s); nothing {verb}.", file=sys.stderr)
        return 1
    where = cfg.out_dir.relative_to(cfg.root).as_posix()
    print(
        f"{verb} {report.notes} notes ({report.public} public, {report.preview} preview, "
        f"{report.draft} draft) in {where}"
    )
    return 0


def _serve(cfg: Config, port: int) -> None:
    class Handler(http.server.SimpleHTTPRequestHandler):
        def translate_path(self, path: str) -> str:
            target = serve_path(cfg, path)
            return str(target) if target else str(cfg.out_dir / "__missing__")

        def log_message(self, format: str, *args: Any) -> None:
            pass

    with http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler) as server:
        print(f"serving http://127.0.0.1:{port}{cfg.base_url} (Ctrl-C to stop)")
        with contextlib.suppress(KeyboardInterrupt):
            server.serve_forever()


def _collect(cfg: Config, run: str, note_dir: Path, test_id: str | None) -> int:
    match = re.search(r"(\d+)/?$", run)
    if not match:
        raise NotesError(f"--run must be a run id or an actions/runs/<id> URL, got {run!r}")
    run_id = match.group(1)
    note_dir = note_dir.resolve()
    if not (note_dir / "note.md").is_file():
        raise NotesError(f"{note_dir} has no note.md")
    dest = note_dir / "tests" / run_id
    _gh(["run", "download", run_id, "--repo", cfg.repo, "-D", str(dest)], cfg.root)
    sha = _gh(
        ["run", "view", run_id, "--repo", cfg.repo, "--json", "headSha", "-q", ".headSha"], cfg.root
    ).strip()
    logs: list[str] = []
    shots: list[str] = []
    for path in sorted(dest.rglob("*")) if dest.exists() else []:
        if not path.is_file():
            continue
        rel = path.relative_to(note_dir).as_posix()
        if path.suffix in (".log", ".txt"):
            lines = path.read_text(errors="replace").splitlines()
            path.write_text("\n".join(lines[-LOG_MAX_LINES:]) + "\n")
            logs.append(rel)
        elif path.suffix.lower() in (".png", ".jpg", ".jpeg", ".gif", ".webp"):
            shots.append(rel)
    entry = [
        "tests:",
        f"  - id: {test_id}" if test_id else "  - cmd: <the command>",
        "    platform: <platform>",
        "    result: <pass|fail|skip>",
        f"    ran_at: {sha}",
    ]
    if logs:
        entry.append(f"    log: {logs[0]}")
    if shots:
        entry.append(f"    shots: [{', '.join(shots)}]")
    entry.append(f"    ci: https://github.com/{cfg.repo}/actions/runs/{run_id}")
    print("\n".join(entry))
    if len(logs) > 1:
        print(f"# other logs: {', '.join(logs[1:])}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="feature_notes.py", description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name, text in (
        ("build", "write the site"),
        ("check", "validate without writing"),
        ("serve", "build, then serve the site locally"),
    ):
        p = sub.add_parser(name, help=text)
        p.add_argument("--root", help="project root (default: the git top level)")
        p.add_argument(
            "--draft",
            action="store_true",
            help="allow unmerged PR heads and local commits; pages get a Draft banner",
        )
        if name == "serve":
            p.add_argument("--port", type=int, default=4000)
    p = sub.add_parser("collect", help="pull a GitHub Actions run's logs into a note")
    p.add_argument("--root")
    p.add_argument("--run", required=True, help="run id or https://github.com/.../runs/<id>")
    p.add_argument("--note", required=True, help="the note's folder")
    p.add_argument("--test", help="the config test id this run is evidence for")
    sub.add_parser("version", help="print the generator version")
    args = parser.parse_args(argv)

    if args.command == "version":
        print(VERSION)
        return 0
    try:
        cfg = load_config(_root(args.root))
        if args.command == "collect":
            return _collect(cfg, args.run, Path(args.note), args.test)
        if args.command == "check":
            return _report(cfg, build(cfg, draft=args.draft, write=False), "checked")
        code = _report(cfg, build(cfg, draft=args.draft), "built")
        if code == 0 and args.command == "serve":
            _serve(cfg, args.port)
        return code
    except NotesError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except ToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
