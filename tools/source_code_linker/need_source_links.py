from collections import defaultdict

from sphinx.application import Sphinx
from sphinx.environment import BuildEnvironment
from sphinx_needs.data import SphinxNeedsData

from .config import GITLAB_REF, SOURCE_SCAN_ROOT
from .generate_source_code_links import find_all_need_references
from .helpers import get_gitlab_link_for_file
from .needlinks import NeedLink


def inject_links_into_needs(app: Sphinx, env: BuildEnvironment) -> None:
    """Attach a ``implements`` string to every need that has references."""
    needs_data = SphinxNeedsData(env)
    needs = needs_data.get_needs_mutable()

    # Group every reference
    links_by_need: dict[str, list[NeedLink]] = defaultdict(list)
    for link in find_all_need_references(SOURCE_SCAN_ROOT):
        if link.need in needs:
            links_by_need[link.need].append(link)

    for need_id, links in links_by_need.items():
        need = needs[need_id]
        need["implements"] = ";".join(
            _render_code_link(link) for link in links
        )

        # Remove & re-add so sphinx-needs re-evaluates the modified need.
        needs_data.remove_need(need_id)
        needs_data.add_need(need)


def _render_code_link(link: NeedLink) -> str:
    rel_path = link.file.relative_to(SOURCE_SCAN_ROOT).as_posix()
    url = get_gitlab_link_for_file(link.file, link.line, ref=GITLAB_REF)
    label = f"{rel_path}:{link.line}"

    return f"{url}<>{label}"