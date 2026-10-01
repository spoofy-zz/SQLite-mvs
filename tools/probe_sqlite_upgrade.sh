#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
version=${1:?usage: probe_sqlite_upgrade.sh VERSION [c|full|tso]}
phase=${2:-full}

case "$version" in
  3.15.2) year=2016; number=3150200; module=SQLT315 ;;
  3.22.0) year=2018; number=3220000; module=SQLT322 ;;
  3.31.1) year=2020; number=3310100; module=SQLT331 ;;
  3.35.5) year=2021; number=3350500; module=SQLT355 ;;
  3.37.2) year=2022; number=3370200; module=SQLT372 ;;
  3.40.1) year=2022; number=3400100; module=SQLT401 ;;
  3.45.3) year=2024; number=3450300; module=SQLT453 ;;
  3.49.2) year=2025; number=3490200; module=SQLT492 ;;
  3.53.4) year=2026; number=3530400; module=SQLT534 ;;
  *) echo "unsupported upgrade-ladder version: $version" >&2; exit 2 ;;
esac

base="$root/build/upgrades/$version"
archive="$base/sqlite-amalgamation-$number.zip"
source_dir="$base/sqlite-amalgamation-$number"
probe_dir="$base/probe"
names="$base/sqlite3_mvs_names.h"

mkdir -p "$base"
if [[ ! -f "$source_dir/sqlite3.c" || ! -f "$source_dir/sqlite3.h" ]]; then
  curl --fail --location --silent --show-error \
    "https://www.sqlite.org/$year/sqlite-amalgamation-$number.zip" \
    --output "$archive"
  unzip -q -o "$archive" -d "$base"
fi

target=probe
if [[ "$phase" == c ]]; then
  target=probe-c
elif [[ "$phase" == tso ]]; then
  target=tso
elif [[ "$phase" != full ]]; then
  echo "phase must be c, full, or tso" >&2
  exit 2
fi

make -C "$root" "$target" \
  SQLITE_SRC="build/upgrades/$version/sqlite-amalgamation-$number/sqlite3.c" \
  SQLITE_HEADER="build/upgrades/$version/sqlite-amalgamation-$number/sqlite3.h" \
  SQLITE_INCLUDE_DIR="build/upgrades/$version/sqlite-amalgamation-$number" \
  NAMES_HEADER="build/upgrades/$version/sqlite3_mvs_names.h" \
  BUILDDIR="build/upgrades/$version/probe" \
  LOAD_MODULE="$module" \
  TSO_MODULE="SQLI${module#SQLT}" \
  AS370_LARGE=build/tools/as370

grep -m1 '#define SQLITE_VERSION ' "$source_dir/sqlite3.c"
echo "upgrade probe $version $phase PASSED"
