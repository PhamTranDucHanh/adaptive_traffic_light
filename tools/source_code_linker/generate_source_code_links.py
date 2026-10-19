"""Scan source files for ``req-Id`` tags and collect their references.
"""

from pathlib import Path
from typing import Iterator

from .config import SCANNED_EXTENSIONS, SOURCE_TAGS
from .needlinks import NeedLink


def _extract_references_from_line(line: str) -> Iterator[tuple[str, str]]:
    """Yield ``(tag, need_id)`` pairs for every reference found in ``line``.

    A single line may reference several needs, separated by spaces or commas:
        // req-Id: REQ_A, REQ_B REQ_C
    """
    for tag in SOURCE_TAGS:
        index = line.find(tag)
        if index < 0:
            continue
        after_tag = line[index + len(tag):].strip()
        for need in after_tag.replace(",", " ").split():
            if need:
                yield tag, need


def _extract_references_from_file(file: Path) -> Iterator[NeedLink]:
    """Yield every :class:`NeedLink` found in a single file."""
    text = file.read_text(encoding="utf-8", errors="ignore")
    for line_no, line in enumerate(text.splitlines(), start=1):
        for tag, need in _extract_references_from_line(line):
            yield NeedLink(file=file, line=line_no, tag=tag, need=need)


def find_all_need_references(root: Path) -> Iterator[NeedLink]:
    for file in root.rglob("*"):
        if not file.is_file():
            continue
        if file.suffix not in SCANNED_EXTENSIONS:
            continue
        yield from _extract_references_from_file(file)