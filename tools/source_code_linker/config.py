from pathlib import Path

SOURCE_SCAN_ROOT: Path = Path(__file__).resolve().parents[2]

# Base URL of the GitLab tree used to build per-line source links.
GITLAB_REF: str | None = None

SOURCE_TAGS: tuple[str, ...] = (
    "// req-Id:",
    "// test-Id:",
    " * req-Id:",
    "// req-traceability:",
)

SCANNED_EXTENSIONS: frozenset[str] = frozenset(
    {".cpp", ".hpp", ".c", ".h"}
)
