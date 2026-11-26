#!/bin/bash
# Applies MANDELBROT's changes to LMMS's own files (ci/patches), draws the icons and writes the
# project templates.
set -eo pipefail
for p in manil/ci/patches/*.patch; do
  echo "applying $p"
  git -C src apply --ignore-whitespace --whitespace=nowarn --verbose "$PWD/$p"
done
python3 manil/tools/make_icons.py src/data/themes/default
# project templates (every track has a fractal role) + the CI render tests
python3 manil/tools/make_templates.py src/data/projects/templates manil/ci/renders
# File > New stays LMMS's normal (blank) project; the start screen offers the MANDELBROT templates
git -C src checkout -- data/projects/templates/default.mpt
# MANDELBROT look: recoloured theme, splash, icons, installer banner (needs Pillow)
python3 -m pip install --quiet --user pillow 2>/dev/null || python3 -m pip install --quiet --user --break-system-packages pillow
python3 manil/tools/make_theme.py src
