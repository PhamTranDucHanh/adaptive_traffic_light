# Introduction

This project follows the **Docs as Code** approach — treating documentation the same way we treat source code.
RST is a lightweight, plain-text format that is both human-readable and machine-friendly, 
making it well-suited for integration with AI Agents — enabling automated 
parsing, semantic search, and context retrieval in modern AI-powered workflows.

Documentation is written in **reStructuredText (.rst)**, stored alongside 
the codebase in Git, and built automatically via a build script. This ensures:

- **Single source of truth**: Documentation lives with the code in version control
- **Consistency**: All contributors follow the same workflow (edit → review → merge)
- **Automation**: Documentation is built and published through a repeatable script
- **Collaboration**: Changes go through merge requests, just like code changes

By adopting Docs as Code, we keep our documentation accurate, up-to-date, 
and easy to maintain as the project evolves.

## Table of Contents
 
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [QuickStart](#quickstart)
- [Development Guide](#development-guide)
  - [Linux & WSL Preview](#linux--wsl-preview)
  - [Creating Tables](#creating-tables)

## Prerequisites

**VS Code Extensions (for live preview):**
 
- `restructuredtext`(LeXtudio Inc) — RST syntax support
- `esbonio`(Swyddfa) — RST language server
- `Live Server`(Ritwick Dey) — live HTML preview in browser

# Installation

> Run these steps once before anything else.

**Linux / WSL**
 
```shell
# 1. Create virtual env
python3 -m venv .venv
source .venv/bin/activate
 
# 2. Install dependencies
pip install -r requirement.txt
 
# 3. Run setup
./script/setup.sh
```

# QuickStart

> Make sure [Installation](#installation) is completed first.

**Linux / WSL**
 
```shell
source .venv/bin/activate
./script/make_docs.sh --build
```

**View the output:**
 
**Option 1 — Open file directly in browser**
 
```shell
cd build/latest/html
```
 
Open `index.html` in Chrome / Firefox / Edge. Simple and works on all platforms — no extra tools needed.
 
> Reload the page manually after each rebuild.
 
**Option 2 — Live Server**
 
See [Live Server](#option-2--live-server-browser-preview) for setup steps.

## Development Guide

### Linux & WSL Preview

#### Option 1 — Preview in VS Code (RST Preview)
 
**Required extensions:** `restructuredtext`, `esbonio`
 
1. Open any `.rst` file in VS Code
2. Press `Ctrl+K V` to open **Preview to the Side**
3. The preview panel appears on the right and updates as you edit

#### Option 2 — Live Server (Browser Preview)
 
**Required extension:** `Live Server`
 
1. Activate the virtual environment and start the build watcher:
```shell
source .venv/bin/activate
./script/make_docs.sh --build --viewhtml
```
 
2. In the terminal, hold `Ctrl` and click the URL (e.g. `http://0.0.0.0:8000`)
3. Select **Open in Browser** — the page auto-reloads on changes
4. Press `Ctrl+C` in the terminal to stop

## Creating Tables

RST tables can be created using the VS Code RST extension.
 
Reference: https://tatsuyanakamori.github.io/vscode-reStructuredText/en/sec02_functions/table.html
 
> **Warning:** Tables with merged cells are not currently supported.
> See the [warning note](https://tatsuyanakamori.github.io/vscode-reStructuredText/en/sec02_functions/table.html#:~:text=up%20three%20lines.-,Warning,-Currently%2C%20tables%20with) for details.
