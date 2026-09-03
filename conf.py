# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

import time
import os
import sys

project = 'QAC 0.1'
author = 'Ban Vien Corp'
copyright = '{}, {}'.format(time.strftime('%Y'), author)

sys.path.insert(0, os.path.abspath("."))

_PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__),))
_PLANTUML_JAR = os.path.join(_PROJECT_ROOT, '_external_tools', 'plantuml', 'plantuml-1.2024.7.jar')

plantuml = f'java -jar {_PLANTUML_JAR}'

extensions = [
    'sphinx_multiversion',
    'sphinx_copybutton',
    'sphinxcontrib.mermaid',
    'sphinx_needs',
    "sphinxcontrib.plantuml",
    "tools.source_code_linker",
]

# --- Requirements model ---
# sphinx-needs IDs default to UPPERCASE-only; allow our lowercase double-underscore IDs:
needs_id_regex = r"^[A-Za-z][A-Za-z0-9_]*$"

needs_types = [
    # Stakeholder Requirements
    dict(directive="stkh_req", title="Stakeholder Requirements",    prefix="stkh_req__",   color="#DF744A", style="node"),
    
    # System Requirements
    dict(directive="sys_req", title="System Requirements",    prefix="sys_req__",   color="#DF744A", style="node"),
    dict(directive="sys_des", title="System Architecture Design",    prefix="sys_des__",   color="#DF744A", style="node"),
    
    # Software Requirement
    dict(directive="sw_req", title="Software Feature Requirements",    prefix="sw_req__",   color="#DF744A", style="node"),
    dict(directive="sw_des", title="Software Architecture Design",    prefix="sw_des__",   color="#DF744A", style="node"),
    dict(directive="sw_dd", title="Software Detailed Design",    prefix="sw_dd__",   color="#DF744A", style="node"),
    dict(directive="sw_comp_req", title="Software Component Requirement",    prefix="sw_comp_req__",   color="#DF744A", style="node"),
    
    # Software Testing
    dict(directive="sw_ut_test_spec",     title="Sofware Unit Test Specification",prefix="sw_ut_test_spec__", color="#FEDCD2", style="node"),
    dict(directive="sw_ut_test_case",     title="Sofware Unit Test Case",prefix="sw_ut_test_case__", color="#FEDCD2", style="node"),
    dict(directive="sw_it_test_spec",     title="Sofware Integration Specification",prefix="sw_it_test_spec__", color="#FEDCD2", style="node"),
    dict(directive="sw_it_test_case",     title="Sofware Integration Test Case",prefix="sw_it_test_case__", color="#FEDCD2", style="node"),
    dict(directive="sw_qt_test_spec",     title="Sofware Qualification Test Specification",prefix="sw_qt_test_spec__", color="#FEDCD2", style="node"),
    dict(directive="sw_qt_test_case",     title="Sofware Qualification Test Case",prefix="sw_qt_test_case__", color="#FEDCD2", style="node"),
    
    # System Testing
    dict(directive="sys_it_test_case",     title="System Integration Test Case",prefix="sys_it_test_case__", color="#FEDCD2", style="node"),
    dict(directive="sys_qt_test_case",     title="System Qualification Test Case",prefix="sys_qt_test_case__", color="#FEDCD2", style="node"),
]

needs_links = {
    "derives": {
        "incoming": "derived from",
        "outgoing": "derived to",
    },
    "verifies": {
        "incoming": "is verified by",
        "outgoing": "verifies",
    },
    "satisfies": {
        "incoming": "is satisfied by",
        "outgoing": "satisfies",
    },
    "fulfils": {
        "incoming": "is fulfilled by",
        "outgoing": "fulfils",
    }
}

needs_flow_engine = "graphviz"
plantuml_output_format = "svg"

templates_path = ['docs/_templates']
exclude_patterns = ['build', 'Thumbs.db', '.DS_Store']

# Need Layout Option
_HEAD_RIGHT_COMMON = [
    '<<collapse_button("meta", collapsed="icon:arrow-down-circle", visible="icon:arrow-right-circle", initial=False)>>',
]
_FOOTER_COMMON = {
    "footer_left": ['<<meta_id()>>'],
    "footer": ['<<meta("type_name")>>'],
    "footer_right": [],
}

def _layout(meta_left):
    return {
        "grid": "complex",
        "layout": {
            "head_left": ['<<meta("title")>>'],
            "head": ['<<meta("status", prefix="status: ", show_empty=True)>>'],
            "head_right": _HEAD_RIGHT_COMMON,
            "meta_left": meta_left,
            "meta_right": [],
            **_FOOTER_COMMON,
        },
    }

needs_layouts = {
    "banvien": _layout([
        '<<meta("implements", prefix="implements: ")>>',
        'derives to: <<meta_links("derives", incoming=False)>>',
        'derives from: <<meta_links("derives", incoming=True)>>',
        'satisfies: <<meta_links("satisfies", incoming=False)>>',
        'is satisfied by: <<meta_links("satisfies", incoming=True)>>',
        'verifies: <<meta_links("verifies", incoming=False)>>',
        'is verified by: <<meta_links("verifies", incoming=True)>>',
        'fulfils: <<meta_links("fulfils", incoming=False)>>',
        'is fulfilled by: <<meta_links("fulfils", incoming=True)>>',
    ]),
    "layout_sw_req": _layout([
        'derives to: <<meta_links("derives", incoming=False)>>',
        'derives from: <<meta_links("derives", incoming=True)>>',
        'is satisfied by: <<meta_links("satisfies", incoming=True)>>',
        'is verified by: <<meta_links("verifies", incoming=True)>>',
        '<<meta("req_covered", prefix="req_covered: ", show_empty=True)>>',
    ]),
    "layout_sys_req": _layout([
        'derives to: <<meta_links("derives", incoming=False)>>',
        'derives from: <<meta_links("derives", incoming=True)>>',
        'is satisfied by: <<meta_links("satisfies", incoming=True)>>',
        'is verified by: <<meta_links("verifies", incoming=True)>>',
        '<<meta("req_covered", prefix="req_covered: ", show_empty=True)>>',
    ]),
    # stkh_req
    "layout_stkh_req": _layout([
        'derives to: <<meta_links("derives", incoming=False)>>',
        '<<meta("req_covered", prefix="req_covered: ", show_empty=True)>>',
    ]),
    "layout_comp_req": _layout([
        'derives from: <<meta_links("derives", incoming=True)>>',
        'is fulfilled by: <<meta_links("fulfils", incoming=True)>>',
        '<<meta("req_covered", prefix="req_covered: ", show_empty=True)>>',
    ]),
    
    "layout_sw_des": _layout([
        'satisfies: <<meta_links("satisfies", incoming=False)>>',
        'is satisfied by: <<meta_links("satisfies", incoming=True)>>',
        'is verified by: <<meta_links("verifies", incoming=True)>>',
        'fulfils: <<meta_links("fulfils", incoming=False)>>',
        '<<meta("req_covered", prefix="req_covered: ", show_empty=True)>>',
    ]),
    "layout_sys_des": _layout([
        'derives to: <<meta_links("derives", incoming=False)>>',
        'derives from: <<meta_links("derives", incoming=True)>>',
        'satisfies: <<meta_links("satisfies", incoming=False)>>',
        'is verified by: <<meta_links("verifies", incoming=True)>>',
        '<<meta("req_covered", prefix="req_covered: ", show_empty=True)>>',
    ]),
    "layout_dd": _layout([
        '<<meta("implements", prefix="implements: ")>>',
        'satisfies: <<meta_links("satisfies", incoming=False)>>',
        'is verified by: <<meta_links("verifies", incoming=True)>>',
    ]),
    "layout_sw_ut_test_case": _layout([
        '<<meta("implements", prefix="implements: ")>>',
        'derives from: <<meta_links("derives", incoming=True)>>',
        'verifies: <<meta_links("verifies", incoming=False)>>',
    ]),
    "layout_sw_ut_test_spec": _layout([
        'derives to: <<meta_links("derives", incoming=False)>>',
    ]),
    "layout_sw_it_test_case": _layout([
        '<<meta("implements", prefix="implements: ")>>',
        'derives from: <<meta_links("derives", incoming=True)>>',
        'verifies: <<meta_links("verifies", incoming=False)>>',
    ]),
    "layout_sw_it_test_spec": _layout([
        'derives to: <<meta_links("derives", incoming=False)>>',
    ]),
    "layout_qt_test_case": _layout([
        'derives from: <<meta_links("derives", incoming=True)>>',
        'verifies: <<meta_links("verifies", incoming=False)>>',
    ]),
    "layout_qt_test_spec": _layout([
        'derives to: <<meta_links("derives", incoming=False)>>',
    ]),
    "layout_sys_it_test_case": _layout([
        'derives from: <<meta_links("derives", incoming=True)>>',
        'verifies: <<meta_links("verifies", incoming=False)>>',
    ]),
}

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'shibuya'
html_theme_options = {
    "color_mode": "dark",
    "light_logo": "_static/logo_bv.png",
    "dark_logo": "_static/logo_bv.png",
    "gitlab_url": "https://gitlab-scm.banvien.com.vn/playground/internship/2026/adaptive_traffic_light",
}

html_favicon = 'docs/_static/logo_bv.png'

# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['docs/_static']
html_css_files = ['custom.css']

smv_branch_whitelist = r'^$'
# Reg following SemVer standard
smv_tag_whitelist = r'^v\d+\.\d+\.\d+(?:-(?:alpha|beta|rc)(?:\.?\d+)?)?$'
smv_remote_whitelist = None
smv_latest_version = "main"

# Debug
needs_build_json = True

needs_fields = {
    "status": {
        "description": "Review status following the S-CORE workflow",
        "schema": {
            "type": "string",
            "enum": ["valid", "invalid"],
        },
    },
    "implements": {
        "description": "Link to source code on GitLab",
        "schema": {
            "type": "string",
        },
    },
    "req_covered": {
        "description": "Requirement is covered by child requirements (S-CORE coverage attribute)",
        "schema": {
            "type": "string",
            "enum": ["yes", "no"],
        },
    },
    "layout": {
        "predicates": [
            ('type in ["stkh_req"]', "layout_stkh_req"),
            ('type in ["sys_req"]', "layout_sys_req"),
            ('type in ["sw_req"]', "layout_sw_req"),
            ('type in ["sw_comp_req",]', "layout_comp_req"),
            ('type in ["sw_des",]', "layout_sw_des"),
            ('type in ["sys_des",]', "layout_sys_des"),
            ('type in ["sw_dd",]', "layout_dd"),
            ('type in ["sw_ut_test_case",]', "layout_sw_ut_test_case"),
            ('type in ["sw_ut_test_spec",]', "layout_sw_ut_test_spec"),
            ('type in ["sw_it_test_case",]', "layout_sw_it_test_case"),
            ('type in ["sw_it_test_spec",]', "layout_sw_it_test_spec"),
            ('type in ["sw_qt_test_case","sys_qt_test_case",]', "layout_qt_test_case"),
            ('type in ["sw_qt_test_spec","sys_qt_test_spec",]', "layout_qt_test_spec"),
            ('type in ["sys_it_test_case",]', "layout_sys_it_test_case"),
        ],
        "default": "banvien",
    },
}
