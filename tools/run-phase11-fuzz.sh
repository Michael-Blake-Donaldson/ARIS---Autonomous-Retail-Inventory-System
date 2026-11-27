#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
build=${1:-out/build/linux-fuzz}
headless=${2:-out/build/linux-ci/argus-headless}
runs=${3:-20000}
mkdir -p out/evidence
evidence=$(mktemp -d out/evidence/phase-11-fuzz.XXXXXXXX)
mkdir -p "$evidence"/{scenario,catalog,map,rfid}
cp -- assets/warehouse/{small-backroom,rfid-audit,broad-stockroom}.json "$evidence/scenario/"
cp -- assets/inventory/fictional-catalog.csv "$evidence/catalog/"
printf '%s' '{"version":1,"records":[]}' > "$evidence/catalog/empty.json"
"$headless" --warehouse small-backroom --export-map 80 > "$evidence/map/small.argmap"
printf '\001\000\000\000\000' > "$evidence/rfid/single-read"
printf '\000' > "$evidence/rfid/empty"

export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for parser in scenario catalog map rfid; do
    case "$parser" in
        scenario) max_len=2097153 ;;
        catalog) max_len=524289 ;;
        map) max_len=1000069 ;;
        rfid) max_len=16384 ;;
    esac
    "$build/argus-fuzz-$parser" "$evidence/$parser" -runs="$runs" -seed=20261124 \
        -max_len="$max_len" -timeout=10 -rss_limit_mb=2048 \
        -artifact_prefix="$evidence/" > "$evidence/$parser.log" 2>&1
    tail -n 2 "$evidence/$parser.log"
done
echo "Evidence directory $evidence"
