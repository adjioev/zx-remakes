#!/usr/bin/env bash
# Package the macOS app from a release build (cmake -DAPP_BUNDLE=ON, raylib static):
#   tools/mac_app.sh <build dir> <out dir>
# -> <out dir>/ZX Remakes/ZX Remakes.app (+ HOW TO PLAY.txt, LICENSE, README.md), and
#    <out dir>/ZXRemakes-macos.zip of that folder. Signed ad hoc (no Apple developer ID): macOS
#    asks once before the first start (see HOW TO PLAY.txt).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$1"
OUT="$2"
VERSION="${APP_VERSION:-dev}"
DIR="$OUT/ZX Remakes"
APP="$DIR/ZX Remakes.app"

rm -rf "$DIR" "$OUT/ZXRemakes-macos.zip"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BUILD/zxremakes" "$APP/Contents/MacOS/ZXRemakes"
# the HD art, not the tools' working folders (raw/, guides/: local only)
rsync -a --exclude raw --exclude guides --exclude .DS_Store "$ROOT/remaster/assets" "$APP/Contents/Resources/"

# the icon: the title picture's glass ball, square
ICONSET="$(mktemp -d)/AppIcon.iconset"
mkdir -p "$ICONSET"
SQUARE="$(mktemp -d)/square.png"
sips -c 900 900 --cropOffset 0 1030 "$ROOT/remaster/assets/ballbreaker/intro.png" --out "$SQUARE" >/dev/null
for s in 16 32 128 256 512; do
    sips -z $s $s "$SQUARE" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$SQUARE" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>ZX Remakes</string>
  <key>CFBundleDisplayName</key><string>ZX Remakes</string>
  <key>CFBundleIdentifier</key><string>io.github.adjioev.zxremakes</string>
  <key>CFBundleExecutable</key><string>ZXRemakes</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION#v}</string>
  <key>CFBundleVersion</key><string>${VERSION#v}</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.arcade-games</string>
</dict>
</plist>
EOF

codesign --force --deep --sign - "$APP"
cp "$ROOT/LICENSE" "$ROOT/README.md" "$DIR/"
cat > "$DIR/HOW TO PLAY.txt" <<'EOF'
ZX Remakes: HD remakes of ZX Spectrum games (macOS 11 or newer, Apple silicon or Intel)

1. Move "ZX Remakes.app" to Applications (or anywhere) and open it.
   The app is not signed by Apple, so macOS asks once:
   - macOS 15 and newer: open it, choose Done, then System Settings > Privacy & Security,
     scroll down to "ZX Remakes was blocked" and choose Open Anyway.
   - macOS 11 to 14: right-click (or Control-click) the app, choose Open, then Open.
2. A list of the games opens. Press Enter on a game to download it from
   spectrumcomputing.co.uk (the app asks first), or drag your own copy onto the window:
   Ball Breaker: BallBreaker.tap.zip, or "Ball Breaker (1987)(CRL Group)[128K].z80"
3. Enter to play.

Keys: Esc pause, Tab original / HD graphics, T HD theme, F11 full screen.
Ball Breaker: arrows move the bat, Space fires, L unlimited lives, K unlimited missiles.

Source and license (GPL-2.0): https://github.com/adjioev/zx-remakes
EOF
(cd "$OUT" && ditto -c -k --sequesterRsrc --keepParent "ZX Remakes" ZXRemakes-macos.zip)
echo "$OUT/ZXRemakes-macos.zip"
