#!/usr/bin/env python3
"""Validate public-readiness mechanics for whitelisted wiki Markdown files."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from urllib.parse import unquote


MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
MARKDOWN_REFERENCE_LINK = re.compile(
    r"^\s{0,3}\[(?!\^)[^\]]+\]:\s*(?:<([^>]+)>|(\S+))", re.MULTILINE
)
HTML_LINK = re.compile(
    r"\b(?:href|src)\s*=\s*(?:['\"]([^'\"]+)['\"]|([^\s>]+))", re.IGNORECASE
)
LOCAL_PATH = re.compile(
    r"(?:file://|\\\\[^\\\s]+\\[^\\\s]+|"
    r"(?<![:\w])/(?:Users|home|root|workspace|tmp|mnt/[A-Za-z])(?:/|$)|"
    r"(?<![A-Za-z0-9])[A-Za-z]:[\\/])",
    re.IGNORECASE,
)
EXTERNAL_URL = re.compile(r"https?://[^\s)>\"']+", re.IGNORECASE)
PRIVATE_TRACKER = re.compile(r"https?://[^\s)>]*linear\.app", re.IGNORECASE)
PLACEHOLDER = re.compile(
    r"(?:\bTODO\b|\bTBD\b|<Subsystem>|<Stable Design Title>|<Runtime Scenario>|"
    r"<commit or tag>|<YYYY-MM-DD>|<claim>|<path>|<question>|<code path>|"
    r"<test path>|<contract or limitation>)"
)
PUBLIC_MEASUREMENT = re.compile(
    r"\b\d[\d,]*(?:\.\d+)?\s*/\s*\d[\d,]*(?:\.\d+)?\b|"
    r"\b\d[\d,]*(?:\.\d+)?\s*[x×]\s*\d[\d,]*(?:\.\d+)?\b|"
    r"\b\d[\d,]*(?:\.\d+)?\s*(?:%|MB/s|GB/s|KiB/s|MiB/s|GiB/s|"
    r"ops/s|packets/s|requests/s|req/s|bytes/s|TPS|ms|us|µs|ns|bytes?|"
    r"KB|MB|GB|KiB|MiB|GiB|tests?|test\s+cases?|checks?|assertions?|runs?|"
    r"requests?|packets?|events?|messages?|errors?|failures?|clients?|users?|"
    r"connections?|sessions?)"
    r"(?![A-Za-z/])|"
    r"\b\d[\d,]*(?:\.\d+)?[kKmMbB]\s*(?:ops/s|packets/s|requests/s|"
    r"req/s|bytes/s|tests?|checks?|assertions?|runs?|requests?|packets?|events?|"
    r"messages?|errors?|failures?|clients?|users?|connections?|sessions?)"
    r"(?![A-Za-z/])|"
    r"\d[\d,]*(?:\.\d+)?\s*(?:명|건|개|회)"
    r"(?=$|[\s.,;:)\]}]|(?:이다|였다|이며|이고|이면|면|은|는|이|가|을|를|"
    r"의|에|에서|으로|만|도|당|씩|까지|부터)(?=$|[\s.,;:)\]}]))|"
    r"\bp(?:50|90|95|99)\b|\brun[ -]?id\b|\bverdict\b|"
    r"\b(?:verdict\.valid|completeness\.complete)\b|\berror\s*[:=]?\s*0\b|"
    r"\b(?:queue\s+)?(?:threshold|watermark)\b[^\r\n\d]{0,32}\d[\d,]*|"
    r"(?:임계값|경계값)[^\r\n\d]{0,32}\d[\d,]*",
    re.IGNORECASE,
)
METRIC_FIELD = re.compile(
    r"\b(?:success|pass|failure|error)\s+rate\b[^\r\n\d]{0,24}\d[\d,]*(?:\.\d+)?|"
    r"\b(?:latency|throughput|memory\s+usage|rss|resident\s+set|working\s+set|"
    r"heap\s+size|queue\s+(?:depth|size)|backlog|pending\s+count|discarded\s+count)\b"
    r"[^\r\n\d]{0,24}\d[\d,]*(?:\.\d+)?|"
    r"\b(?:tests?|checks?|assertions?|runs?|clients?|users?|connections?|sessions?|"
    r"requests?|packets?|events?|messages?|errors?|failures?)\s*[:=|]\s*"
    r"\d[\d,]*(?:\.\d+)?|"
    r"(?:성공률|통과율|실패율|오류율|지연(?:\s*시간)?|처리량|메모리\s*사용량)"
    r"[^\r\n\d]{0,24}\d[\d,]*(?:\.\d+)?|"
    r"(?:테스트|클라이언트|사용자|연결|세션|요청|패킷|이벤트|메시지|오류|실패)\s*수"
    r"\s*[:=|]\s*\d[\d,]*(?:\.\d+)?",
    re.IGNORECASE,
)
EVALUATION_RUBRIC = re.compile(
    r"^#{1,6}\s+(?:평가 기준|검증 기준|근거 수준|합격 기준|통과 기준|"
    r"수용 기준|판정 기준|evidence matrix)\s*$|"
    r"\|\s*(?:기술 주장|주장)\s*\|\s*근거 수준\s*\||"
    r"\b(?:acceptance criteria|pass/fail|scorecard)\b|"
    r"(?:합격|통과|수용|판정)\s*기준",
    re.IGNORECASE,
)
RESULT_VERDICT = re.compile(
    r"(?:^|\|)\s*(?:test\s+result|result|검증\s*결과|테스트\s*결과|판정)"
    r"\s*[:=|]\s*(?:pass(?:ed)?|fail(?:ed)?|success|failure|통과|실패|합격|불합격)"
    r"(?=$|[\s|.,;:)])",
    re.IGNORECASE,
)
MACHINE_DETAIL = re.compile(
    r"^\s*(?:[-*]\s*)?(?:cpu(?:\s+model)?|gpu(?:\s+model)?|ram|os(?:\s+version)?|"
    r"machine|processor|hardware|benchmark\s+host|(?:benchmark|test)\s+environment|"
    r"운영체제|프로세서|메모리|CPU\s*모델|GPU\s*모델|벤치마크\s*호스트|"
    r"(?:벤치마크|테스트|실행|측정)\s*환경)\s*[:|]|"
    r"\|\s*(?:cpu(?:\s+model)?|gpu(?:\s+model)?|ram|os(?:\s+version)?|machine|"
    r"processor|hardware|benchmark\s+host|(?:benchmark|test)\s+environment|운영체제|"
    r"프로세서|메모리|CPU\s*모델|GPU\s*모델|벤치마크\s*호스트|"
    r"(?:벤치마크|테스트|실행|측정)\s*환경)\s*\|",
    re.IGNORECASE,
)
INTERNAL_NARRATION = re.compile(
    r"(?:^|\n)#{1,6}\s+포트폴리오 가치\s*$|"
    r"(?:^|\n)#{1,6}\s+현재\s+drift(?:와\s+한계)?\s*$|"
    r"\bKnowledge Gate\b|문서화 세션",
    re.IGNORECASE | re.MULTILINE,
)
MANIFEST_NAME = ".wiki-documents"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate whitelisted public wiki Markdown without modifying files."
    )
    parser.add_argument("--repo-root", default=".", help="Repository root")
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--target", help="Selected wiki subsystem directory")
    selection.add_argument(
        "--all-public",
        action="store_true",
        help="Validate the recursive union of every wiki publication manifest",
    )
    parser.add_argument(
        "--changed-path",
        action="append",
        default=[],
        help=(
            "Registered Markdown path to validate; repeat for every selected document. "
            f"When omitted, validate every path in {MANIFEST_NAME}."
        ),
    )
    return parser.parse_args()


def is_within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def link_destination(raw_link: str) -> str:
    link = raw_link.strip()
    if link.startswith("<") and ">" in link:
        return link[1 : link.index(">")]
    return link.split(maxsplit=1)[0] if link else ""


def iter_link_targets(text: str):
    for match in MARKDOWN_LINK.finditer(text):
        yield match.group(1)
    for match in MARKDOWN_REFERENCE_LINK.finditer(text):
        yield match.group(1) or match.group(2)
    for match in HTML_LINK.finditer(text):
        yield match.group(1) or match.group(2)


def resolve_link(
    source: Path, raw_link: str, repository_root: Path | None = None
) -> Path | None:
    link = link_destination(raw_link)
    if (
        not link
        or link.startswith(("#", "//"))
        or re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", link)
    ):
        return None
    path_text = unquote(link.split("#", 1)[0].split("?", 1)[0])
    if not path_text:
        return None
    if path_text.startswith("/"):
        if repository_root is None:
            return None
        return (repository_root / path_text.lstrip("/")).resolve()
    return (source.parent / path_text).resolve()


def is_private_source_link(raw_link: str) -> bool:
    destination = link_destination(raw_link)
    if destination.startswith("//") or re.match(
        r"^[A-Za-z][A-Za-z0-9+.-]*:", destination
    ):
        return False
    link = unquote(destination).replace("\\", "/").lower()
    return any(
        private_path in link
        for private_path in ("docs/design/linear/", "docs/memo/", ".agents/", "legacy/")
    )


def load_manifest(target: Path, errors: list[str]) -> list[Path]:
    initial_error_count = len(errors)
    manifest = target / MANIFEST_NAME
    if not manifest.is_file():
        errors.append(f"managed document manifest does not exist: {manifest}")
        return []

    try:
        lines = manifest.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError:
        errors.append(f"{manifest}: not valid UTF-8")
        return []

    documents: list[Path] = []
    seen: set[Path] = set()
    for line_number, raw_line in enumerate(lines, start=1):
        entry = raw_line.strip()
        if not entry or entry.startswith("#"):
            continue

        relative = Path(entry)
        if relative.is_absolute():
            errors.append(f"{manifest}:{line_number}: path must be relative: {entry}")
            continue

        document = (target / relative).resolve()
        if not is_within(document, target):
            errors.append(f"{manifest}:{line_number}: path escapes target: {entry}")
        elif document.suffix.lower() != ".md":
            errors.append(f"{manifest}:{line_number}: path is not Markdown: {entry}")
        elif document in seen:
            errors.append(f"{manifest}:{line_number}: duplicate path: {entry}")
        else:
            seen.add(document)
            documents.append(document)

    if not documents and len(errors) == initial_error_count:
        errors.append(f"managed document manifest is empty: {manifest}")
    return documents


def load_public_manifests(
    wiki_root: Path, errors: list[str]
) -> tuple[dict[Path, list[Path]], set[Path]]:
    manifests: dict[Path, list[Path]] = {}
    documents: set[Path] = set()
    document_owners: dict[Path, Path] = {}
    manifest_paths = sorted(wiki_root.rglob(MANIFEST_NAME))
    if not manifest_paths:
        errors.append(f"no publication manifests found under: {wiki_root}")
        return manifests, documents

    for manifest in manifest_paths:
        target = manifest.parent.resolve()
        managed_documents = load_manifest(target, errors)
        manifests[target] = managed_documents
        for document in managed_documents:
            previous_owner = document_owners.get(document)
            if previous_owner is not None:
                errors.append(
                    f"public document is registered by multiple manifests: "
                    f"{document} ({previous_owner}, {manifest})"
                )
            else:
                document_owners[document] = manifest
                documents.add(document)
    return manifests, documents


def validate_metadata(path: Path, text: str, errors: list[str]) -> None:
    head = "\n".join(text.splitlines()[:16])
    status_match = re.search(
        r"^> Document status: (Draft|Reviewed)$", head, re.MULTILINE
    )
    baseline_match = re.search(r"^> Baseline: (\S.+)$", head, re.MULTILINE)
    reviewed_match = re.search(
        r"^> Last reviewed: (\d{4}-\d{2}-\d{2}|Not reviewed)$",
        head,
        re.MULTILINE,
    )

    if status_match is None:
        errors.append(f"{path}: missing 'Document status: Draft | Reviewed'")
    if baseline_match is None:
        errors.append(f"{path}: missing non-empty Baseline metadata")
    if reviewed_match is None:
        errors.append(f"{path}: missing Last reviewed date or 'Not reviewed'")

    if status_match is not None and status_match.group(1) == "Reviewed":
        if baseline_match is not None and baseline_match.group(1).lower() in {
            "not established",
            "not reviewed",
            "unknown",
        }:
            errors.append(f"{path}: Reviewed document requires an established Baseline")
        if reviewed_match is not None and reviewed_match.group(1) == "Not reviewed":
            errors.append(f"{path}: Reviewed document requires a review date")


def validate_file(
    path: Path,
    wiki_root: Path,
    public_documents: set[Path],
    errors: list[str],
    warnings: list[str],
) -> None:
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        errors.append(f"{path}: not valid UTF-8")
        return

    validate_metadata(path, text, errors)

    for raw_link in iter_link_targets(text):
        target = resolve_link(path, raw_link, wiki_root.parent)
        if target is not None and not target.exists():
            errors.append(f"{path}: broken relative link: {raw_link}")
        elif (
            target is not None
            and is_within(target, wiki_root)
            and target.suffix.lower() == ".md"
            and target not in public_documents
        ):
            errors.append(
                f"{path}: link points to an unregistered wiki document: {raw_link}"
            )
        if is_private_source_link(raw_link):
            errors.append(f"{path}: link points to a private workspace source: {raw_link}")

    for line_number, line in enumerate(text.splitlines(), start=1):
        if LOCAL_PATH.search(EXTERNAL_URL.sub("", line)):
            errors.append(f"{path}:{line_number}: local absolute path is not public-ready")
        if PRIVATE_TRACKER.search(line):
            errors.append(f"{path}:{line_number}: private Linear URL is not public-ready")
        if PLACEHOLDER.search(line):
            errors.append(f"{path}:{line_number}: unresolved template placeholder")
        if PUBLIC_MEASUREMENT.search(line) or METRIC_FIELD.search(line):
            errors.append(
                f"{path}:{line_number}: internal measurement value is not allowed in a public document"
            )
        if EVALUATION_RUBRIC.search(line) or RESULT_VERDICT.search(line):
            errors.append(
                f"{path}:{line_number}: internal evaluation rubric is not allowed in a public document"
            )
        if MACHINE_DETAIL.search(line):
            errors.append(
                f"{path}:{line_number}: machine detail is not allowed in a public document"
            )

    if INTERNAL_NARRATION.search(text):
        errors.append(
            f"{path}: internal workflow or portfolio narration is not public wiki prose"
        )


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    wiki_root = (repo_root / "wiki").resolve()
    errors: list[str] = []
    warnings: list[str] = []

    if not repo_root.is_dir():
        errors.append(f"repository root does not exist: {repo_root}")
    if not wiki_root.is_dir():
        errors.append(f"wiki root does not exist: {wiki_root}")

    manifests: dict[Path, list[Path]] = {}
    public_documents: set[Path] = set()
    if not errors:
        manifests, public_documents = load_public_manifests(wiki_root, errors)

    target: Path | None = None
    managed_documents: list[Path] = []
    if args.target:
        target = (repo_root / args.target).resolve()
        if not target.is_dir():
            errors.append(f"target directory does not exist: {target}")
        elif not is_within(target, wiki_root):
            errors.append(f"target must be inside {wiki_root}: {target}")
        elif target not in manifests:
            errors.append(f"managed document manifest does not exist: {target / MANIFEST_NAME}")
        else:
            managed_documents = manifests[target]
    else:
        managed_documents = sorted(public_documents)

    changed_paths: list[Path] = []
    for raw_path in args.changed_path:
        changed = (repo_root / raw_path).resolve()
        changed_paths.append(changed)
        if target is not None and not is_within(changed, target):
            errors.append(f"changed path is outside selected target: {raw_path}")
        elif target is None and not is_within(changed, wiki_root):
            errors.append(f"changed path is outside wiki root: {raw_path}")

    markdown_files: list[Path] = []
    if not errors:
        managed_set = set(managed_documents)
        if changed_paths:
            for changed in changed_paths:
                if not changed.exists():
                    errors.append(f"changed path does not exist: {changed}")
                elif changed.suffix.lower() != ".md":
                    errors.append(f"changed path is not Markdown: {changed}")
                elif changed not in managed_set:
                    errors.append(f"changed path is not registered in {MANIFEST_NAME}: {changed}")
                else:
                    markdown_files.append(changed)
        else:
            markdown_files = managed_documents

        markdown_files = sorted(set(markdown_files))
        if not markdown_files and not errors:
            selection = target if target is not None else wiki_root
            errors.append(f"no managed Markdown files selected under: {selection}")

        for path in markdown_files:
            if not path.is_file():
                errors.append(f"managed Markdown file does not exist: {path}")
            else:
                validate_file(path, wiki_root, public_documents, errors, warnings)

    for warning in warnings:
        print(f"WARN: {warning}")
    for error in errors:
        print(f"ERROR: {error}")

    if errors:
        print(f"Result: FAIL ({len(errors)} error(s), {len(warnings)} warning(s))")
        return 1

    print(
        f"Result: PASS ({len(markdown_files)} file(s), "
        f"{len(warnings)} warning(s))"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
