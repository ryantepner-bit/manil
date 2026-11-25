#!/bin/bash
# Renders every test project and MANDELBROT template to audio and prints stats.
mkdir -p results
export QT_QPA_PLATFORM=offscreen
for f in manil/ci/renders/*.mpt; do
  [ -f "$f" ] || continue
  n=$(basename "$f" .mpt)
  echo "=== render $n"
  timeout 400 xvfb-run -a inst/bin/lmms render "$f" -o "results/$n.wav" -f wav -s 44100 > "results/$n.render.log" 2>&1
  echo "exit $?"; tail -n 20 "results/$n.render.log"
  if [ -f "results/$n.wav" ]; then
    sox "results/$n.wav" -n stat 2>&1 | tee "results/$n.stat.txt"
    sox "results/$n.wav" -C 96 "results/$n.mp3" 2>/dev/null || sox "results/$n.wav" -r 22050 -b 16 "results/$n.small.wav"
    rm -f "results/$n.wav"
  fi
done
ls -la results
