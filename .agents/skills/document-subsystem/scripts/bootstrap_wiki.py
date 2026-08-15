#!/usr/bin/env python3
"""Create the minimum public-ready wiki skeleton without overwriting files."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SUBSYSTEM_SLUG = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create missing wiki entrypoint, workflow, templates, and subsystem README."
    )
    parser.add_argument("--repo-root", default=".", help="Repository root")
    parser.add_argument("--project-name", required=True, help="Human-facing project name")
    parser.add_argument("--subsystem", required=True, help="Lowercase hyphenated subsystem slug")
    parser.add_argument("--title", required=True, help="Human-facing subsystem title")
    parser.add_argument("--dry-run", action="store_true", help="Report without writing")
    return parser.parse_args()


def render(text: str, project_name: str, subsystem: str, title: str) -> str:
    return (
        text.replace("{{PROJECT_NAME}}", project_name)
        .replace("{{SUBSYSTEM_SLUG}}", subsystem)
        .replace("{{SUBSYSTEM_TITLE}}", title)
    )


def main() -> int:
    args = parse_args()
    if not SUBSYSTEM_SLUG.fullmatch(args.subsystem):
        print("ERROR: --subsystem must use lowercase hyphen-case", file=sys.stderr)
        return 2
    if not args.project_name.strip() or not args.title.strip():
        print("ERROR: --project-name and --title must be non-empty", file=sys.stderr)
        return 2

    repo_root = Path(args.repo_root).resolve()
    if not repo_root.is_dir():
        print(f"ERROR: repository root does not exist: {repo_root}", file=sys.stderr)
        return 2

    asset_root = Path(__file__).resolve().parent.parent / "assets" / "wiki"
    if not asset_root.is_dir():
        print(f"ERROR: bundled wiki assets are missing: {asset_root}", file=sys.stderr)
        return 2

    pairs = [
        (asset_root / "README.md", repo_root / "wiki" / "README.md"),
        (asset_root / "WORKFLOW.md", repo_root / "wiki" / "WORKFLOW.md"),
        (
            asset_root / "subsystem-document-manifest.txt",
            repo_root / "wiki" / ".wiki-documents",
        ),
        *[
            (source, repo_root / "wiki" / "templates" / source.name)
            for source in sorted((asset_root / "templates").glob("*.md"))
        ],
        (
            asset_root / "templates" / "subsystem-readme-template.md",
            repo_root / "wiki" / args.subsystem / "README.md",
        ),
        (
            asset_root / "subsystem-document-manifest.txt",
            repo_root / "wiki" / args.subsystem / ".wiki-documents",
        ),
    ]

    created = 0
    skipped = 0
    for source, destination in pairs:
        if not source.is_file():
            print(f"ERROR: required asset is missing: {source}", file=sys.stderr)
            return 2
        relative = destination.relative_to(repo_root)
        if destination.exists():
            print(f"SKIP: {relative}")
            skipped += 1
            continue
        print(f"CREATE: {relative}")
        created += 1
        if args.dry_run:
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        rendered = render(
            source.read_text(encoding="utf-8"),
            args.project_name.strip(),
            args.subsystem,
            args.title.strip(),
        )
        destination.write_text(rendered, encoding="utf-8")

    print(f"Result: PASS ({created} created, {skipped} existing)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
