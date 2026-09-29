#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "=== 1. Building libfaac on current branch with Meson ==="
BUILD_DIR="${REPO_ROOT}/build_ladder"
if [ ! -d "${BUILD_DIR}" ]; then
    meson setup "${BUILD_DIR}" "${REPO_ROOT}"
fi
ninja -C "${BUILD_DIR}"

echo "=== 2. Building probe/ladder/reemit_tool ==="
gcc -O2 -I"${REPO_ROOT}/libfaac" -I"${REPO_ROOT}/include" -I"${BUILD_DIR}" \
    "${SCRIPT_DIR}/reemit_main.c" -o "${SCRIPT_DIR}/reemit_tool" \
    "${BUILD_DIR}/libfaac/libfaac.a" -lm

echo "=== 3. Building FAAD3 decoder from faad-ladder-dump branch ==="
FAAD_WORKTREE="/tmp/faad-ladder-dump"
if [ ! -d "${FAAD_WORKTREE}" ]; then
    git worktree add "${FAAD_WORKTREE}" origin/faad-ladder-dump || {
        echo "Worktree failed, attempting clone..."
        git clone "${REPO_ROOT}" "${FAAD_WORKTREE}"
        git -C "${FAAD_WORKTREE}" checkout origin/faad-ladder-dump
    }
fi
FAAD_BUILD_DIR="${FAAD_WORKTREE}/build_faad"
if [ ! -d "${FAAD_BUILD_DIR}" ]; then
    meson setup -Dstats=true "${FAAD_BUILD_DIR}" "${FAAD_WORKTREE}"
fi
ninja -C "${FAAD_BUILD_DIR}"

echo "=== 4. Setting up faac-benchmark and datasets ==="
BENCHMARK_DIR="/opt/faac-benchmark"
if [ ! -d "${BENCHMARK_DIR}" ]; then
    if [ -d "${REPO_ROOT}/../faac-benchmark" ]; then
        BENCHMARK_DIR="${REPO_ROOT}/../faac-benchmark"
    elif [ -d "/tmp/faac-benchmark" ]; then
        BENCHMARK_DIR="/tmp/faac-benchmark"
    else
        BENCHMARK_DIR="/tmp/faac-benchmark"
        git clone https://github.com/nschimme/faac-benchmark "${BENCHMARK_DIR}"
    fi
fi

if [ -f "${BENCHMARK_DIR}/requirements.txt" ]; then
    pip install -q -r "${BENCHMARK_DIR}/requirements.txt" || true
fi

python3 "${BENCHMARK_DIR}/setup_datasets.py"

echo "=== 5. Locating matching source WAVs for probe/ladder/ref/apple/*.m4a ==="
REF_APPLE_DIR="${SCRIPT_DIR}/ref/apple"
mkdir -p "${REF_APPLE_DIR}"

found_count=0
if ls "${REF_APPLE_DIR}"/*.m4a >/dev/null 2>&1; then
    for m4a in "${REF_APPLE_DIR}"/*.m4a; do
        stem="$(basename "${m4a}" .m4a)"
        wav="${BENCHMARK_DIR}/data/external/audio/${stem}.wav"
        if [ -f "${wav}" ]; then
            found_count=$((found_count + 1))
        else
            echo "  Warning: missing corpus WAV for Apple ref '${stem}' at ${wav}"
        fi
    done
    echo "Found ${found_count} matching source WAVs in ${BENCHMARK_DIR}/data/external/audio"
else
    echo "No Apple reference .m4a files present in ${REF_APPLE_DIR} (directory created)."
fi

echo "=== 6. Verifying MOS scorer and ffmpeg dependencies ==="
pip install -q zimtohrli || true
which ffmpeg >/dev/null || { echo "Error: ffmpeg is required but not installed."; exit 1; }

echo "=== Setup complete! ==="
