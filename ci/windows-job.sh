#!/bin/bash
set -o pipefail
mkdir -p logs dist
export CCACHE_MAXSIZE=0 CCACHE_NOCOMPRESS=1
bash manil/ci/fetch.sh 2>&1 | tee logs/fetch.log || exit 1
( cd src && sudo .github/workflows/setup-mingw-packages.sh && \
  sudo apt-get install -y --no-install-recommends $(xargs < .github/workflows/deps-ubuntu-24.04-mingw.txt) ) > logs/apt.log 2>&1 || { tail -n 50 logs/apt.log; exit 1; }
export VCPKG_ROOT=${VCPKG_INSTALLATION_ROOT}
cmake -S src -B build -DCMAKE_TOOLCHAIN_FILE="$PWD/src/cmake/toolchains/x64-mingw-vcpkg.cmake" -DVCPKG_MANIFEST_INSTALL=YES \
  $(xargs < manil/ci/windows-cmake-args.txt) 2>&1 | tee logs/configure.log || exit 1
cmake --build build --parallel $(nproc) 2>&1 | tee logs/build.log || exit 1
bash manil/ci/windows-package.sh 2>&1 | tee logs/package.log || exit 1
