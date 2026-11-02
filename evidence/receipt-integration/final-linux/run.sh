#!/bin/bash
set -uo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update > /out/apt.log 2>&1 && apt-get install -y cmake g++ make python3 unzip git >> /out/apt.log 2>&1 || exit 10
mkdir -p /work/src /work/build /work/spike
 tar -xzf /out/source.tar.gz -C /work/src
cmake -S /work/src -B /work/build -DCMAKE_BUILD_TYPE=Release -DSCO_SDK_ARCHIVE_DIR=/downloads > /out/configure.log 2>&1 || exit 11
cmake --build /work/build -j 4 > /out/build.log 2>&1 || exit 12
ctest --test-dir /work/build --output-on-failure > /out/ctest.log 2>&1; a=$?
sdk=$(find /work/build/_sdk -path '*/sco-sdk-core-source-2.4.0/CMakeLists.txt' -print -quit | xargs dirname)
printf '%s\n' "$sdk" > /out/sdk-root.log
cmake -S /work/src/spikes/authority-sdk -B /work/spike -DSDK_ROOT="$sdk" -DCMAKE_BUILD_TYPE=Release > /out/spike-configure.log 2>&1 || exit 13
cmake --build /work/spike -j 4 > /out/spike-build.log 2>&1 || exit 14
ctest --test-dir /work/spike --output-on-failure > /out/spike-ctest.log 2>&1; b=$?
/work/spike/authority-cli test > /out/legacy.log 2>&1; c=$?
for mode in solo listen dedicated rejected old sc-world; do /work/spike/authority-cli "$mode" > "/out/cli-$mode.stdout" 2> "/out/cli-$mode.stderr"; printf '%s\n' "$?" > "/out/cli-$mode.exit"; done
mkdir -p /out/artifacts
cp /work/build/plugins/multiplayer/multiplayer.so /out/artifacts/ || true
find /work/build -type f -name 'sco-relay*' -executable -exec cp {} /out/artifacts/ \;
sha256sum /out/artifacts/* > /out/artifacts-sha256.log 2>&1
printf 'combined=%s spike=%s legacy=%s\n' "$a" "$b" "$c" > /out/status.log
[ "$a" -eq 0 ] && [ "$b" -eq 0 ] && [ "$c" -eq 0 ]
