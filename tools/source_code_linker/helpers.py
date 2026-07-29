from __future__ import annotations

import subprocess
from functools import lru_cache
from pathlib import Path
from urllib.parse import urlsplit

from sphinx_needs.logging import get_logger

LOGGER = get_logger(__name__)


def find_git_root(start: Path | None = None) -> Path | None:
    git_root = (start or Path.cwd()).resolve()
    while not (git_root / ".git").exists():
        if git_root == git_root.parent:  # reached filesystem root
            return None
        git_root = git_root.parent
    return git_root


def _git(args: list[str], cwd: Path) -> str:
    result = subprocess.run(
        ["git", *args], cwd=cwd, capture_output=True, text=True, check=True
    )
    return result.stdout.strip()


@lru_cache(maxsize=None)
def get_toplevel_for(path: Path) -> Path:
    directory = path if path.is_dir() else path.parent
    toplevel = _git(["rev-parse", "--show-toplevel"], directory)
    return Path(toplevel).resolve()


def parse_remote_url(url: str) -> tuple[str, str]:
    url = url.strip()

    # scp-like SSH syntax: git@host:group/sub/repo.git  (no scheme, has ':')
    if "://" not in url and url.count(":") == 1 and "@" in url:
        host_part, path = url.split(":", 1)
        host = host_part.split("@", 1)[-1]
    else:
        parts = urlsplit(url)
        host = parts.hostname or ""
        path = parts.path

    path = path.lstrip("/").removesuffix(".git")
    return host, path


@lru_cache(maxsize=None)
def get_gitlab_remote(git_root: Path) -> tuple[str, str]:
    url = ""
    for line in _git(["remote", "-v"], git_root).splitlines():
        if "origin" in line and "(fetch)" in line:
            parts = line.split(maxsplit=2)
            if len(parts) >= 2:
                url = parts[1]
            break
    if not url:
        # Fall back to whatever remote URL we can get.
        url = _git(["remote", "get-url", "origin"], git_root)

    host, project_path = parse_remote_url(url)
    assert host and project_path, (
        "Could not determine the GitLab remote. "
        "Make sure 'git remote -v' shows an origin."
    )
    return host, project_path


@lru_cache(maxsize=None)
def get_head_hash(git_root: Path) -> str:
    # Current commit hash of git root
    return _git(["rev-parse", "HEAD"], git_root)


def get_gitlab_base_url(git_root: Path, ref: str | None = None) -> str:
    host, project_path = get_gitlab_remote(git_root)
    if ref is None:
        ref = get_head_hash(git_root)
    return f"https://{host}/{project_path}/-/blob/{ref}"


def get_gitlab_link(git_root: Path, rel_path: str, line: int, ref: str | None = None) -> str:
    """Full per-line link: ``<base>/<rel_path>#L<line>``."""
    base = get_gitlab_base_url(git_root, ref)
    return f"{base}/{rel_path}#L{line}"

# Get link by absolute file path and submodule aware
def get_gitlab_link_for_file(file: Path, line: int, ref: str | None = None) -> str:
    file = file.resolve()
    toplevel = get_toplevel_for(file)
    rel_path = file.relative_to(toplevel).as_posix()

    superproject = find_git_root(file.parent)
    is_submodule = superproject is not None and toplevel != superproject.resolve()
    effective_ref = None if is_submodule else ref

    return get_gitlab_link(toplevel, rel_path, line, ref=effective_ref)