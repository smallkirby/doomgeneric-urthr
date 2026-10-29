#!/bin/bash
#
# Build doomgeneric for Urthr as a static musl binary.
#
# Usage: ./build-for-urthr.bash [x86_64|aarch64] [output]

set -euo pipefail

arch="${1:-x86_64}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${2:-build-urthr/doom-$arch}"

case "$arch" in
x86_64 | aarch64) ;;
*)
  echo "Unsupported architecture: $arch" >&2
  exit 1
  ;;
esac

case "$out" in
/*) ;;
*) out="$root/$out" ;;
esac

cd "$root/doomgeneric"

# Take the source list from the Makefile.
srcs=$(
  sed -n 's/^SRC_DOOM *= *//p' Makefile |
    tr ' ' '\n' |
    sed -e '/^$/d' -e 's/^doomgeneric_xlib\.o$/doomgeneric_urthr.o/' -e 's/\.o$/.c/'
)

mkdir -p "$(dirname "$out")"

zig cc \
  -target "$arch-linux-musl" \
  -static \
  -Os \
  -Wall \
  -DNORMALUNIX -DLINUX -DSNDSERV -D_DEFAULT_SOURCE \
  -Wl,--gc-sections \
  $srcs \
  -lm \
  -o "$out"

echo "Built: $out"
