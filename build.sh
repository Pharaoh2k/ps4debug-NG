#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

require_build_host() {
    . /etc/os-release
    if [ "${ID:-}" != "ubuntu" ] || [ "${VERSION_ID:-}" != "24.04" ]; then
        echo "ERROR: ps4debug-NG must be built in WSL Ubuntu-24.04 (found ${ID:-unknown} ${VERSION_ID:-unknown})." >&2
        exit 1
    fi
    local gcc_version
    gcc_version="$(gcc -dumpfullversion -dumpversion)"
    if [ "$gcc_version" != "13.3.0" ]; then
        echo "ERROR: ps4debug-NG release builds require GCC 13.3.0 (found $gcc_version)." >&2
        exit 1
    fi
}

clean_build() {
    cd ps4-ksdk
    make clean
    cd ..

    cd ps4-payload-sdk/libPS4/
    make clean
    cd ../../

    cd debugger
    make clean
    cd ..

    cd kdebugger
    make clean
    cd ..

    cd installer
    make clean
    cd ..
}

build_submodules() {
    cd ps4-ksdk
    make
    cd ..

    cd ps4-payload-sdk/libPS4/
    make
    cd ../../
}

build_debugger() {
    cd debugger
    make
    cd ..
}

build_kdebugger() {
    cd kdebugger
    make
    cd ..
}

build_installer() {
    cd installer
    make
    cd ..
}

if [ "${1:-}" = "clean" ]; then
    echo "ERROR: './build.sh clean' is intentionally disabled for release builds." >&2
    echo "       Clean debugger/, kdebugger/, installer/, and SDK components independently when required." >&2
    exit 1
fi

require_build_host

echo "ps4debug-ng building..."

echo "=> submodules..."
build_submodules
echo "=> debugger..."
build_debugger
echo "=> kdebugger..."
build_kdebugger
echo "=> installer..."
build_installer

cp ./installer/installer.bin ./ps4debug-ng.bin

echo ""
echo "enjoy ps4debug-NG! OpenSourcereR :P"
