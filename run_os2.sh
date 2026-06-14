#!/bin/bash
#
# run_os2 - Execute an OS/2 program using lx_loader
#
# Usage: run_os2 <program.exe> [arguments...]
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <program.exe> [arguments...]" >&2
    exit 1
fi

if [ ! -f "${BUILD_DIR}/lx_loader" ]; then
    echo "Error: lx_loader not found in ${BUILD_DIR}" >&2
    exit 1
fi

if [ ! -f "$1" ]; then
    echo "Error: Program '$1' not found" >&2
    exit 1
fi

export LD_LIBRARY_PATH="${BUILD_DIR}:${LD_LIBRARY_PATH}"

cd "${BUILD_DIR}" || exit 1
exec "./lx_loader" "$@"
