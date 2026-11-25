#!/bin/bash
# Pushes error summaries, log tails and test renders to a logs branch (readable without Actions access).
branch="$1"; token="$2"; repo="$3"
mkdir -p out
for f in logs/*.log; do
  [ -f "$f" ] || continue
  b=$(basename "$f" .log)
  grep -n -E "error|Error|undefined reference|FAILED|fatal" "$f" | head -n 400 > "out/$b.errors.txt"
  tail -n 300 "$f" > "out/$b.tail.txt"
done
[ -d results ] && cp -r results out/
ls -la dist > out/dist.txt 2>&1
echo "$GITHUB_SHA $(date -u)" > out/STATUS.txt
cd out && git init -q && git checkout -q -b "$branch" && git add -A && \
  git -c user.name=ci -c user.email=ci@local commit -qm "$branch $GITHUB_SHA" && \
  git push -q -f "https://x-access-token:$token@github.com/$repo" "$branch"
cd ..
# the Windows installer, also on a branch, so it can be fetched with plain git (no sign-in needed)
if ls dist/*.exe > /dev/null 2>&1; then
  rm -rf inst-out && mkdir inst-out && cp dist/*.exe inst-out/ && echo "$GITHUB_SHA $(date -u)" > inst-out/STATUS.txt
  cd inst-out && git init -q && git checkout -q -b ci-installer && git add -A && \
    git -c user.name=ci -c user.email=ci@local commit -qm "installer $GITHUB_SHA" && \
    git push -q -f "https://x-access-token:$token@github.com/$repo" ci-installer
fi
