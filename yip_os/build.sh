#!/bin/bash
set -e
cd "$(dirname "$0")"

case "${1:-build}" in
    clean)
        rm -rf build
        echo "Cleaned."
        ;;
    configure)
        distrobox enter my-distrobox -- cmake -B build -DCMAKE_BUILD_TYPE=Debug
        ;;
    build)
        distrobox enter my-distrobox -- cmake --build build -j"$(nproc)"
        ;;
    run)
        distrobox enter my-distrobox -- ./build/yip_os
        ;;
    rebuild)
        rm -rf build
        distrobox enter my-distrobox -- cmake -B build -DCMAKE_BUILD_TYPE=Debug
        distrobox enter my-distrobox -- cmake --build build -j"$(nproc)"
        ;;
    *)
        echo "Usage: $0 {clean|configure|build|run|rebuild}"
        exit 1
        ;;
esac
