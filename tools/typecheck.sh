#!/bin/bash
# Compiles the app (core + Compose UI) on the host with the tree's Kotlin compiler and Compose plugin,
# against the prebuilt androidx jars and the framework, xz-java and coroutines jars of a previous build
# (out/soong/.intermediates). A quick check before a full build; it builds no APK.
#   tools/typecheck.sh [WORK_DIR]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
TOP=${ANDROID_BUILD_TOP:-$(cd "$HERE/../../.." && pwd)}
WORK=${1:-$HERE/build/typecheck}
M=$TOP/prebuilts/sdk/current/androidx/m2repository/androidx
INTER=$TOP/out/soong/.intermediates
rm -rf "$WORK" && mkdir -p "$WORK/libs" "$WORK/gen"

for a in $(cd "$M" && ls -d compose/*/* lifecycle/* activity/* savedstate/* navigationevent/* core/core \
           collection/collection-jvm annotation/annotation-jvm annotation/annotation-experimental); do
  v=$(ls "$M/$a" | sort -V | tail -1)
  for f in "$M/$a/$v"/*; do
    case "$f" in
      *sources*) ;;
      *.aar) unzip -p "$f" classes.jar > "$WORK/libs/$(echo "$a" | tr / _).jar" 2>/dev/null || true ;;
      *.jar) cp "$f" "$WORK/libs/" ;;
    esac
  done
done

# R with the string, plural and drawable ids the code uses.
python3 - "$HERE/res" "$WORK/gen/R.kt" <<'PY'
import sys, xml.etree.ElementTree as ET
from pathlib import Path
res = Path(sys.argv[1])
root = ET.parse(res / "values" / "strings.xml").getroot()
kinds = {
    "string": [e.get("name") for e in root if e.tag == "string"],
    "plurals": [e.get("name") for e in root if e.tag == "plurals"],
    "drawable": sorted(f.stem for f in (res / "drawable").glob("*.xml")),
}
blocks = []
for kind, names in kinds.items():
    ids = "\n".join(f"        const val {n} = {i + 1}" for i, n in enumerate(names))
    blocks.append(f"    object {kind} {{\n{ids}\n    }}")
body = "\n".join(blocks)
open(sys.argv[2], "w").write(f"package org.lineageos.pcterminal\n\nobject R {{\n{body}\n}}\n")
PY

CP=$(ls "$WORK"/libs/*.jar | tr '\n' ':')
CP=$CP$INTER/external/kotlinx.coroutines/kotlinx_coroutines/linux_glibc_common/combined/kotlinx_coroutines.jar
CP=$CP:$INTER/frameworks/base/framework-minus-apex/android_common/combined/framework.jar
CP=$CP:$INTER/external/xz-java/xz-java/android_common/javac/xz-java.jar
# android.system.Os lives in the ART module and ConnectivityManager in the Connectivity module.
CP=$CP:$INTER/libcore/core-libart/android_common_apex31/combined/core-libart.jar
CP=$CP:$INTER/packages/modules/Connectivity/framework/framework-connectivity.impl/android_common_apex31/combined/framework-connectivity.jar
CAP=""
command -v systemd-run >/dev/null && systemd-run --user --scope -q true 2>/dev/null && CAP="systemd-run --user --scope -q -p MemoryMax=2G"
$CAP env JAVA_OPTS="-Xmx1536m" nice -n 19 "$TOP/external/kotlinc/bin/kotlinc" -no-reflect -jvm-target 21 \
  -Xplugin="$TOP/external/kotlinc/lib/compose-compiler-plugin.jar" \
  -cp "$CP" -d "$WORK/out" "$HERE/core/src" "$HERE/src" "$WORK/gen"
echo "typecheck: ok"
