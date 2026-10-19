#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"   
TOOLS_DIR="${PROJECT_ROOT}/_external_tools"
PLANTUML_VERSION="1.2024.7"
PLANTUML_JAR="${TOOLS_DIR}/plantuml/plantuml-${PLANTUML_VERSION}.jar"
PLANTUML_URL="https://github.com/plantuml/plantuml/releases/download/v${PLANTUML_VERSION}/plantuml-${PLANTUML_VERSION}.jar"

echo "[INFO] Project root: ${PROJECT_ROOT}"

# Check Java
if ! command -v java &> /dev/null; then
    echo "[FAIL] Java not found"
    exit 1
fi

# Setup PlantUML
mkdir -p "${TOOLS_DIR}/plantuml"

if [ -s "${PLANTUML_JAR}" ] && java -jar "${PLANTUML_JAR}" -version &> /dev/null; then
    echo "[ OK ] PlantUML already installed at ${PLANTUML_JAR}"
else
    echo "[INFO] Downloading PlantUML ${PLANTUML_VERSION}..."
    rm -f "${PLANTUML_JAR}"
    curl -L --progress-bar -o "${PLANTUML_JAR}" "${PLANTUML_URL}"

    if ! java -jar "${PLANTUML_JAR}" -version &> /dev/null; then
        echo "[FAIL] PlantUML jar is corrupt or download failed"
        rm -f "${PLANTUML_JAR}"
        exit 1
    fi
    echo "[ OK ] PlantUML installed: ${PLANTUML_JAR}"
fi

echo
echo "[DONE] Setup complete."
echo "       PlantUML jar: ${PLANTUML_JAR}"

# Git Submodule updated
git submodule update --init
