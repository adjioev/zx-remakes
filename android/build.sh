#!/bin/bash
# The Android APK (arm64), signed with a local debug key, for installing by hand:
#   android/build.sh            -> android/build/ZXRemakes.apk
# (target SDK 34: the back key reaches the game, as a key.) Needs the Android SDK (build-tools, platform 36, NDK 27) as Android Studio installs it, and the
# raylib 6.0 source in android/raylib (git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="$SDK/ndk/$(ls "$SDK/ndk" | sort -V | tail -1)"
BT="$SDK/build-tools/$(ls "$SDK/build-tools" | sort -V | grep -v rc | tail -1)"
JAR="$SDK/platforms/android-36/android.jar"
JDK="${JAVA_HOME:-/Applications/Android Studio.app/Contents/jbr/Contents/Home}"
OUT="$HERE/build"
VERSION="$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo dev)"
API=26
mkdir -p "$OUT"
TC=(-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a
    -DANDROID_PLATFORM=android-$API -DCMAKE_BUILD_TYPE=Release)
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc)

echo "== raylib"
if [ ! -f "$OUT/rl/lib/libraylib.a" ]; then
  cmake -S "$HERE/raylib" -B "$OUT/raylib" "${TC[@]}" -DPLATFORM=Android -DOPENGL_VERSION="ES 3.0" \
    -DBUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX="$OUT/rl" >/dev/null
  cmake --build "$OUT/raylib" -j "$JOBS" --target install >/dev/null
fi

echo "== the game"
cmake -S "$HERE" -B "$OUT/app" "${TC[@]}" -DRAYLIB_DIR="$OUT/rl" -DAPP_VERSION="$VERSION" >/dev/null
cmake --build "$OUT/app" -j "$JOBS"

echo "== java"
rm -rf "$OUT/classes" "$OUT/dex" && mkdir -p "$OUT/classes" "$OUT/dex"
"$JDK/bin/javac" --release 11 -nowarn -classpath "$JAR" -d "$OUT/classes" $(find "$HERE/java" -name '*.java')
"$BT/d8" --release --lib "$JAR" --min-api $API --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')

echo "== assets"
rm -rf "$OUT/apk_assets" && mkdir -p "$OUT/apk_assets/assets"
LIST="$OUT/apk_assets/files.txt"
echo "$VERSION-$(date +%s)" > "$LIST"
(cd "$ROOT/remaster/assets" && git ls-files --cached --others --exclude-standard . | grep -v '\.md$') | while read -r f; do
  mkdir -p "$OUT/apk_assets/assets/$(dirname "$f")"
  cp "$ROOT/remaster/assets/$f" "$OUT/apk_assets/assets/$f"
  echo "$f" >> "$LIST"
done

echo "== apk"
rm -rf "$OUT/res" "$OUT/stage" && mkdir -p "$OUT/res/drawable" "$OUT/stage/lib/arm64-v8a"
sips -s format png -z 192 192 "$ROOT/remaster/assets/ballbreaker/adventure/title.jpg" --cropToHeightWidth 1080 1080 \
  --out "$OUT/res/drawable/icon.png" >/dev/null 2>&1 || cp "$ROOT/remaster/assets/ballbreaker/intro.png" "$OUT/res/drawable/icon.png"
"$BT/aapt2" compile --dir "$OUT/res" -o "$OUT/res.zip"
sed "s/VERSION_NAME/$VERSION/" "$HERE/AndroidManifest.xml" > "$OUT/AndroidManifest.xml"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$JAR" --manifest "$OUT/AndroidManifest.xml" -A "$OUT/apk_assets" \
  --min-sdk-version $API --target-sdk-version 34 "$OUT/res.zip" -0 jpg -0 png -0 ttf
cp "$OUT/app/libzxremakes.so" "$OUT/stage/lib/arm64-v8a/"
"$NDK/toolchains/llvm/prebuilt/"*/bin/llvm-strip "$OUT/stage/lib/arm64-v8a/libzxremakes.so"
cp "$OUT/dex/classes.dex" "$OUT/stage/"
(cd "$OUT/stage" && zip -q -r "$OUT/base.apk" classes.dex lib)
"$BT/zipalign" -P 16 -f 4 "$OUT/base.apk" "$OUT/aligned.apk"
KS="$HERE/keystore/zxremakes.keystore"  # (kept: an update installs over the last only with the same key)
mkdir -p "$HERE/keystore"
[ -f "$KS" ] || "$JDK/bin/keytool" -genkeypair -keystore "$KS" -storepass android -keypass android -alias zx \
  -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=ZX Remakes debug" >/dev/null 2>&1
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --ks-key-alias zx --out "$OUT/ZXRemakes.apk" "$OUT/aligned.apk"
ls -lh "$OUT/ZXRemakes.apk"
