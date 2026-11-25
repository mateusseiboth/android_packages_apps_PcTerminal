#!/bin/sh
# Host tests for the app's plain Kotlin core (no Android needed), with the tree's Kotlin compiler and
# JUnit. Soong runs the same tests as PcTerminalCoreTests. The JVMs stay small and run under a 2 GB
# memory cap when systemd-run is there: the machine may be busy with an Android build.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=${ANDROID_BUILD_TOP:-$(cd "$HERE/../../../.." && pwd)}
LIB=$TOP/external/kotlinc/lib
JUNIT=$TOP/prebuilts/devtools/tools/lib/junit-4.12.jar:$TOP/prebuilts/devtools/tools/lib/hamcrest-core-1.3.jar
OUT=$HERE/../build/core-tests
CAP=""
command -v systemd-run >/dev/null && systemd-run --user --scope -q true 2>/dev/null && CAP="systemd-run --user --scope -q -p MemoryMax=2G"
rm -rf "$OUT" && mkdir -p "$OUT"
$CAP env JAVA_OPTS="-Xmx1g" nice -n 19 "$TOP/external/kotlinc/bin/kotlinc" -nowarn -cp "$JUNIT" -d "$OUT" "$HERE/src" "$HERE/tests"
# Test classes are the `class ...Test` declarations of the test sources, so a helper class never runs as a test.
TESTS=$(cd "$HERE/tests" && find . -name '*.kt' | sort | while read -r f; do
  pkg=$(sed -n 's/^package //p' "$f")
  grep -oE '^class [A-Za-z0-9_]+Test\b' "$f" | sed "s/^class /$pkg./"
done)
$CAP java -Xmx256m -cp "$OUT:$JUNIT:$LIB/kotlin-stdlib.jar" org.junit.runner.JUnitCore $TESTS
