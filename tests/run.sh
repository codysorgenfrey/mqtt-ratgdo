#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build="$(mktemp -d "${TMPDIR:-/tmp}/ratgdo-tests.XXXXXX")"
trap 'rm -f "$build/store" "$build/transmission"; rmdir "$build"' EXIT HUP INT TERM
cxx="${CXX:-c++}"
"$cxx" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Isrc src/rolling_store.cpp tests/rolling_store_test.cpp -o "$build/store"
"$build/store"
# Existing decoder/ISR warnings are not promoted to errors by this host harness.
"$cxx" -std=c++11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-but-set-variable \
  -fsanitize=address,undefined -DESP8266 -Itests/fakes -Isrc \
  src/rolling_store.cpp src/rolling_storage.cpp src/rolling_code.cpp \
  src/ratgdo.cpp src/static_code.cpp tests/transmission_test.cpp -o "$build/transmission"
for mode in mount blank normal exhaustion corrupt truncated pending interrupted open short flush rename readback; do
  "$build/transmission" "$mode"
done
