#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRAFFIC_PERCEPTION_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT_DIR="$(cd "${TRAFFIC_PERCEPTION_DIR}/.." && pwd)"
LIB_DIR="${TRAFFIC_PERCEPTION_DIR}/lib"
ONNX_DIR="${LIB_DIR}/onnxruntime"
DATA_DIR="${PROJECT_DIR}/data"

ONNX_VERSION="1.27.1"
GDRIVE_FOLDER_URL="https://drive.google.com/drive/folders/1nXzpFTzbHLYzKeyYwoBCRkDmDqH5TnIM"

REQUIRED_ASSETS=(
    "traffic.mp4"
    "traffic2.mp4"
    "traffic3.mp4"
    "traffic4.mp4"
    "yolov8m-oiv7.onnx"
    "yolov8n-oiv7.onnx"
    "yolov8m.onnx"
    "yolov8n.onnx"
    "rt-detrv2-s.onnx"
)

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

TMP_DIR="$(mktemp -d)"
trap 'rm -rf -- "${TMP_DIR}"' EXIT

mkdir -p "${LIB_DIR}" "${DATA_DIR}"

download_file() {
    local source_url="$1"
    local output_file="$2"

    if command -v curl >/dev/null 2>&1; then
        curl --fail --location --retry 3 "${source_url}" --output "${output_file}"
    elif command -v wget >/dev/null 2>&1; then
        wget --tries=3 --output-document="${output_file}" "${source_url}"
    else
        echo "ERROR: curl hoặc wget chưa được cài đặt." >&2
        exit 1
    fi
}

install_onnx_runtime() {
    if [[ -e "${ONNX_DIR}/lib/libonnxruntime.so.1" && \
          -f "${ONNX_DIR}/BUILD.bazel" ]]; then
        echo "[SETUP][ONNX] ONNX Runtime ${ONNX_VERSION} đã tồn tại."
        return
    fi

    echo "[SETUP][ONNX] Đang tải ONNX Runtime ${ONNX_VERSION} (${ONNX_ARCH})..."
    download_file "${URL}" "${TMP_DIR}/${ARCHIVE}"
    tar -xzf "${TMP_DIR}/${ARCHIVE}" -C "${TMP_DIR}"

    local extract_dir
    extract_dir="$(find "${TMP_DIR}" -maxdepth 1 -type d \
        -name 'onnxruntime-linux-*' -print -quit)"
    if [[ -z "${extract_dir}" ]]; then
        echo "ERROR: Không tìm thấy thư mục ONNX Runtime sau khi giải nén." >&2
        exit 1
    fi

    rm -rf -- "${ONNX_DIR}"
    mv "${extract_dir}" "${ONNX_DIR}"
    install -m 0644 "${SCRIPT_DIR}/onnx_build.bazel" \
        "${ONNX_DIR}/BUILD.bazel"

    if [[ ! -e "${ONNX_DIR}/lib/libonnxruntime.so.1" ]]; then
        echo "ERROR: Thiếu libonnxruntime.so.1 sau khi cài đặt." >&2
        exit 1
    fi
    echo "[SETUP][ONNX] Đã cài tại ${ONNX_DIR}."
}

run_gdown() {
    if command -v gdown >/dev/null 2>&1; then
        gdown "$@"
        return
    fi

    if python3 -c 'import gdown' >/dev/null 2>&1; then
        python3 -m gdown "$@"
        return
    fi

    if ! python3 -m pip --version >/dev/null 2>&1; then
        echo "ERROR: Cần gdown hoặc python3-pip để tải Google Drive." >&2
        exit 1
    fi

    local gdown_python_dir="${TMP_DIR}/gdown-python"
    echo "[SETUP][DATA] Đang cài tạm gdown 5.2.0..."
    python3 -m pip install --disable-pip-version-check --quiet \
        --target "${gdown_python_dir}" "gdown==5.2.0"
    PYTHONPATH="${gdown_python_dir}${PYTHONPATH:+:${PYTHONPATH}}" \
        python3 -m gdown "$@"
}

install_runtime_assets() {
    local missing_assets=()
    local asset
    for asset in "${REQUIRED_ASSETS[@]}"; do
        if [[ ! -s "${DATA_DIR}/${asset}" ]]; then
            missing_assets+=("${asset}")
        fi
    done

    if (( ${#missing_assets[@]} == 0 )); then
        echo "[SETUP][DATA] Đủ 4 video và 3 model trong ${DATA_DIR}."
        return
    fi

    echo "[SETUP][DATA] Thiếu: ${missing_assets[*]}"
    echo "[SETUP][DATA] Đang tải thư mục Google Drive..."
    local download_dir="${TMP_DIR}/google-drive-assets"
    mkdir -p "${download_dir}"
    run_gdown --folder --remaining-ok --output "${download_dir}" \
        "${GDRIVE_FOLDER_URL}"

    local source_file
    for asset in "${missing_assets[@]}"; do
        source_file="$(find "${download_dir}" -type f -name "${asset}" \
            -print -quit)"
        if [[ -z "${source_file}" || ! -s "${source_file}" ]]; then
            echo "ERROR: Google Drive không cung cấp file ${asset}." >&2
            echo "Kiểm tra quyền chia sẻ và tên file trong: ${GDRIVE_FOLDER_URL}" >&2
            exit 1
        fi
        install -m 0644 "${source_file}" "${DATA_DIR}/${asset}"
        echo "[SETUP][DATA] Đã cài ${asset}."
    done
}

install_onnx_runtime
install_runtime_assets

echo
echo "========================================"
echo "Setup dependencies thành công."
echo "ONNX Runtime: ${ONNX_DIR}"
echo "Runtime assets: ${DATA_DIR}"
echo "========================================"
