#!/usr/bin/env python3
"""Check that every local Markdown link in the repo's docs actually resolves
to a file that exists.

This exists because the README referenced KNOWN_ISSUES.md and PLAN.md while
neither file existed in the repository -- two silently broken links that a
reviewer (or a teammate cloning the repo) would only discover by clicking
them. Run it locally or wire it into CI to catch this class of bug before
it reaches a reviewer.

Usage:
    python3 scripts/check_docs.py [FILES...]

With no arguments it checks every *.md file at the repository root and one
level down. Exits 0 if every local link resolves, exits 1 and prints every
broken link otherwise.
"""
from __future__ import annotations

import pathlib
import re
import sys

LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parent.parent


def default_targets() -> list[pathlib.Path]:
    root = repo_root()
    targets = list(root.glob("*.md"))
    targets += list(root.glob("*/*.md"))
    return sorted(set(targets))


def is_external_or_anchor(link: str) -> bool:
    link = link.strip()
    if not link:
        return True
    if link.startswith("#"):
        return True
    if "://" in link:
        return True
    if link.startswith("mailto:"):
        return True
    return False


def find_broken_links(md_file: pathlib.Path) -> list[str]:
    text = md_file.read_text(encoding="utf-8")
    broken = []
    for link in LINK_RE.findall(text):
        if is_external_or_anchor(link):
            continue
        target = link.split("#", 1)[0].strip()
        if not target:
            continue
        root = repo_root().resolve()
        resolved = (md_file.parent / target).resolve()
        if resolved != root and root not in resolved.parents:
            broken.append(link)
        elif not resolved.exists():
            broken.append(link)
    return broken


def main(argv: list[str]) -> int:
    targets = [pathlib.Path(a) for a in argv] if argv else default_targets()
    if not targets:
        print("check_docs: no Markdown files found to check")
        return 0

    had_failure = False
    for md_file in targets:
        if not md_file.is_file():
            continue
        broken = find_broken_links(md_file)
        if broken:
            had_failure = True
            print(f"{md_file}: broken local link(s):")
            for link in broken:
                print(f"  - {link}")

    if had_failure:
        print("check_docs: FAILED - fix the links above")
        return 1

    print("check_docs: OK - every local Markdown link resolves")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
