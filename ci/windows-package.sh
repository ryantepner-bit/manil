#!/bin/bash
# Builds the NSIS installer with LMMS's own packaging and puts it in dist/.
set -e
cmake --build build --target package
mkdir -p dist
for f in build/lmms-*.exe; do cp -v "$f" "dist/MANDELBROT-setup-$(basename "$f" .exe | sed 's/^lmms-//').exe"; done
ls -la dist
