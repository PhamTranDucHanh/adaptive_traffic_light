"""The :class:`NeedLink` data type: a single requirement reference found in source code."""

from pathlib import Path
from typing import NamedTuple


class NeedLink(NamedTuple):
    """A single requirement reference (``req-Id`` tag) found in a source file.

    Attributes:
        file: Absolute path to the file containing the reference.
        line: 1-based line number of the reference.
        tag:  The tag that introduced the reference (e.g. ``// req-Id:``).
        need: The referenced need ID (e.g. ``TOOL_REQ__...``).
    """

    file: Path
    line: int
    tag: str
    need: str