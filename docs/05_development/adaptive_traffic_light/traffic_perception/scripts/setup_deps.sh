#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB_DIR="${ROOT_DIR}/lib"
ONNX_DIR="${LIB_DIR}/onnxruntime"

ONNX_VERSION="1.27.1"

ARCH="$(uname -m)"
case "${ARCH}" in
    x86_64)
        ONNX_ARCH="x64"
        ;;
    aarch64)
        ONNX_ARCH="aarch64"
        ;;
    *)
        echo "Unsupported architecture: ${ARCH}"
        exit 1
        ;;
esac

ARCHIVE="onnxruntime-linux-${ONNX_ARCH}-${ONNX_VERSION}.tgz"
URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/${ARCHIVE}"

mkdir -p "${LIB_DIR}"

if [[ -f "${ONNX_DIR}/lib/libonnxruntime.so" ]]; then
    echo "ONNX Runtime already installed."
    exit 0
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

echo "Downloading ONNX Runtime ${ONNX_VERSION} (${ONNX_ARCH})..."

if command -v curl >/dev/null 2>&1; then
    curl -L "${URL}" -o "${TMP_DIR}/${ARCHIVE}"
elif command -v wget >/dev/null 2>&1; then
    wget -O "${TMP_DIR}/${ARCHIVE}" "${URL}"
else
    echo "Neither curl nor wget is installed."
    exit 1
fi

echo "Extracting..."

tar -xzf "${TMP_DIR}/${ARCHIVE}" -C "${TMP_DIR}"

EXTRACT_DIR="$(find "${TMP_DIR}" -maxdepth 1 -type d -name "onnxruntime-linux-*")"

rm -rf "${ONNX_DIR}"
mv "${EXTRACT_DIR}" "${ONNX_DIR}"

cp ${ROOT_DIR}/scripts/onnx_build.bazel ${ONNX_DIR}/BUILD.bazel

echo
echo "========================================"
echo "ONNX Runtime installed successfully."
echo
echo "Location:"
echo "  ${ONNX_DIR}"
echo
echo "Libraries:"
echo "  ${ONNX_DIR}/lib"
echo
echo "Headers:"
echo "  ${ONNX_DIR}/include"
echo "========================================"
