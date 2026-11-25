#!/bin/bash
# Fetch pinned LMMS (with submodules) + the CC0 drum library, then apply the MANDELBROT overlay.
set -eo pipefail
source manil/ci/versions.env
fetch() { # repo sha dir
  mkdir -p "$3"; git -C "$3" init -q; git -C "$3" remote add origin "https://github.com/$1" 2>/dev/null || true
  git -C "$3" fetch -q --depth 1 origin "$2"; git -C "$3" checkout -q FETCH_HEAD
}
fetch "$LMMS_REPO" "$LMMS_REF" src
git -C src submodule update --init --recursive --depth 1 --jobs 8 2>&1 | tail -n 5 || \
  git -C src submodule update --init --recursive --jobs 8 2>&1 | tail -n 5
fetch miskibin/synkopa "$SYNKOPA_REF" synkopa
cp -rv manil/lmms/. src/ | tail -n 300
mkdir -p src/data/samples/drums-cc0
cp synkopa/crates/engine/library/* src/data/samples/drums-cc0/
echo "drums-cc0: $(ls src/data/samples/drums-cc0 | wc -l) files"
if [ -f manil/ci/patch.sh ]; then bash manil/ci/patch.sh; fi
