#!/usr/bin/env bash
# Build a static QuickJS-NG on POSIX systems into native/bin/<platform>.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps="$root/native/quickjs"
build="$root/_build/qjs-native"

if [ ! -d "$deps" ]; then
  echo "fetching quickjs-ng (pinned)..."
  git -C "$root" submodule update --init --recursive
fi

cmake -S "$deps" -B "$build" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DQJS_BUILD_EXAMPLES=OFF
cmake --build "$build" --target qjs -j

case "$(uname -s)" in
  Darwin) plat="mac" ;;
  *)      plat="linux-x64" ;;
esac

out="$root/native/bin/$plat"
mkdir -p "$out"
cp "$build/libqjs.a" "$out/"
echo "wrote $out/libqjs.a"
