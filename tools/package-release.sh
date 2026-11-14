#!/usr/bin/env bash
# Build a release archive from an already-configured build tree.
#
#   tools/package-release.sh <version> <build-dir> <out-dir>
#
# Produces quickjs-forge-<version>-<platform>.zip|tar.gz containing:
#   lib/        qjs + qjs_bridge static libraries
#   include/    quickjs.h + qjs_bridge.h
#   LICENSE NOTICE README.md VERSION BUILD-INFO.txt
set -euo pipefail

ver="${1:?usage: package-release.sh <version> <build-dir> <out-dir>}"
build="${2:?}"
out="${3:?}"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) plat="windows-x64"; ext="zip" ;;
  Darwin)               plat="macos-$(uname -m)"; ext="tar.gz" ;;
  *)                    plat="linux-x64"; ext="tar.gz" ;;
esac

name="quickjs-forge-${ver}-${plat}"
stage="$(mktemp -d)"
pkg="$stage/$name"
mkdir -p "$pkg"

cmake --install "$build" --config Release --prefix "$pkg" > /dev/null
cp "$root/LICENSE" "$root/NOTICE" "$root/README.md" "$pkg/"
printf '%s\n' "$ver" > "$pkg/VERSION"

{
  echo "name: $name"
  echo "version: $ver"
  echo "platform: $plat"
  echo "commit: ${GITHUB_SHA:-$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)}"
  echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$pkg/BUILD-INFO.txt"

if [ "$ext" = "zip" ]; then
  ( cd "$stage" && cmake -E tar cf "$out/$name.zip" --format=zip "$name" )
  echo "$out/$name.zip"
else
  ( cd "$stage" && cmake -E tar czf "$out/$name.tar.gz" "$name" )
  echo "$out/$name.tar.gz"
fi

rm -rf "$stage"
