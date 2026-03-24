#!/bin/bash
set -e
cd "$(dirname "$0")"

# CTranslate2 prefix (for NLLB translation support)
# Override with: CT2_PREFIX=/path/to/ct2 ./build.sh configure
CT2_PREFIX="${CT2_PREFIX:-/tmp/ct2_install}"

CMAKE_EXTRA=""
if [ -d "$CT2_PREFIX/lib64/cmake/ctranslate2" ]; then
    CMAKE_EXTRA="-DCMAKE_PREFIX_PATH=$CT2_PREFIX"
    echo "CTranslate2 found at $CT2_PREFIX"
else
    echo "CTranslate2 not found — translation will be disabled"
fi

case "${1:-build}" in
    clean)
        rm -rf build
        echo "Cleaned."
        ;;
    configure)
        distrobox enter my-distrobox -- cmake -B build -DCMAKE_BUILD_TYPE=Debug $CMAKE_EXTRA
        ;;
    build)
        distrobox enter my-distrobox -- cmake --build build -j"$(nproc)"
        ;;
    run)
        LD_LIBRARY_PATH="$CT2_PREFIX/lib64:${LD_LIBRARY_PATH:-}" \
            ./build/yip_os
        ;;
    rebuild)
        rm -rf build
        distrobox enter my-distrobox -- cmake -B build -DCMAKE_BUILD_TYPE=Debug $CMAKE_EXTRA
        distrobox enter my-distrobox -- cmake --build build -j"$(nproc)"
        ;;
    *)
        echo "Usage: $0 {clean|configure|build|run|rebuild}"
        exit 1
        ;;
esac
