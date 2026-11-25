#!/bin/bash
# Replaces LMMS's setup-mingw-packages.sh: newer Ubuntu runners have no apt-key, so the
# tobydox MinGW PPA (Qt 5 + libgig for MinGW) is added with a keyring file instead.
set -e
install -d -m 0755 /etc/apt/keyrings
rm -f /tmp/tobydox.asc
for k in 72931B477E22FEFD47F8DECE02FE5F12ADDE29B2 9C0C72C11E0AF891; do
  curl -fsSL "https://keyserver.ubuntu.com/pks/lookup?op=get&options=mr&search=0x$k" >> /tmp/tobydox.asc
done
rm -f /tmp/tobydox.kbx
gpg --batch --no-default-keyring --keyring /tmp/tobydox.kbx --import /tmp/tobydox.asc
gpg --batch --no-default-keyring --keyring /tmp/tobydox.kbx --export > /etc/apt/keyrings/tobydox-mingw.gpg
echo "deb [signed-by=/etc/apt/keyrings/tobydox-mingw.gpg] http://ppa.launchpad.net/tobydox/mingw-w64/ubuntu focal main" \
  > /etc/apt/sources.list.d/tobydox-mingw-w64.list
apt-get update -y
