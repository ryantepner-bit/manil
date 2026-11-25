#!/bin/bash
# vcpkg ports that need a newer version than the runner's preinstalled vcpkg:
#   pcre2 10.49#2 reads api.github.com (rate limited on shared runners, HTTP 403); #3 uses the
#   release tarball instead. Pinned to a vcpkg commit so the build is reproducible.
set -e
VCPKG_PORTS_COMMIT=9290f1a2049af5fdd6d580efbb765baaa85a3623
rm -rf vcpkg-ports-src vcpkg-overlays
mkdir -p vcpkg-overlays
git init -q vcpkg-ports-src
cd vcpkg-ports-src
git remote add origin https://github.com/microsoft/vcpkg
git sparse-checkout set --no-cone /ports/pcre2/
git fetch -q --depth 1 --filter=blob:none origin "$VCPKG_PORTS_COMMIT"
git checkout -q FETCH_HEAD
cd ..
cp -r vcpkg-ports-src/ports/pcre2 vcpkg-overlays/
ls vcpkg-overlays/*
