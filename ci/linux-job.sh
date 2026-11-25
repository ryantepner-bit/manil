#!/bin/bash
set -o pipefail
mkdir -p logs
export CCACHE_MAXSIZE=0 CCACHE_NOCOMPRESS=1
bash manil/ci/fetch.sh 2>&1 | tee logs/fetch.log || exit 1
sudo apt-get update -y > /dev/null
sudo apt-get install -y --no-install-recommends $(xargs < src/.github/workflows/deps-ubuntu-24.04-gcc.txt) xvfb sox libsox-fmt-mp3 > logs/apt.log 2>&1 || { tail -n 50 logs/apt.log; exit 1; }
cmake -S src -B build -DCMAKE_INSTALL_PREFIX="$PWD/inst" -DPLUGIN_LIST="$(xargs < manil/ci/linux-plugins.txt)" \
  $(xargs < manil/ci/linux-cmake-args.txt) 2>&1 | tee logs/configure.log || exit 1
cmake --build build --parallel $(nproc) 2>&1 | tee logs/build.log || exit 1
cmake --install build > logs/install.log 2>&1 || { tail -n 50 logs/install.log; exit 1; }
bash manil/ci/linux-tests.sh 2>&1 | tee logs/tests.log
