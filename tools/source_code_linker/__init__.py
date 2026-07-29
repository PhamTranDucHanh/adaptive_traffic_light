"""Sphinx extension that links sphinx-needs items to their source code"""

import os
from pathlib import Path

from sphinx.application import Sphinx

from .need_source_links import inject_links_into_needs

__version__ = "0.1.0"

_STRING_LINK_CONFIG = {
    "regex": r"(?P<url>[^<>;]+)<>(?P<name>[^<>;]+)",
    "link_url": "{{url}}",
    "link_name": "{{name}}",
    "options": ["implements"],
}

def _register_string_link(app: Sphinx) -> None:
    """Register the ``url<>label`` string-link pattern with sphinx-needs."""
    app.config.needs_string_links.setdefault(
        "source_code_linker", _STRING_LINK_CONFIG
    )

def setup_source_code_linker(app: Sphinx, ws_root: Path | None) -> None:
    """Set up the source-code linker with all needed options."""
    _register_string_link(app)

def setup_once(app: Sphinx) -> None:
    ws_root = Path(os.path.dirname(__file__)).resolve()
    setup_source_code_linker(app, ws_root)

def setup(app: Sphinx) -> dict:
    setup_once(app)

    app.connect(
        "env-updated",
        inject_links_into_needs,
        priority=999,
    )

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": False,
    }