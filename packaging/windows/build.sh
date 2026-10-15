#!/bin/sh
# Compilation croisée Windows (x86_64) depuis Fedora, puis assemblage d'un dossier portable,
# d'un zip et d'un installeur NSIS.
# Prérequis : sudo dnf install mingw64-gcc-c++ mingw64-qt6-qtbase mingw64-qt6-qtmultimedia \
#   mingw64-qt6-qtsvg mingw64-qt6-qtimageformats mingw64-qt6-qttools mingw64-qt6-qttranslations \
#   mingw64-libarchive mingw64-openssl mingw64-nsis
set -eu
SRC=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD:-$HOME/.cache/easylunii-build-win64}
OUT=${OUT:-$SRC/dist}
VERSION=$(sed -n 's/^project(EasyLunii VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")

mingw64-cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DEASYLUNII_TESTS=OFF
cmake --build "$BUILD"

STAGE="$BUILD/EasyLunii"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BUILD/easylunii.exe" "$BUILD/easylunii-cli.exe" "$STAGE/"
"$SRC/packaging/windows/deploy.sh" "$STAGE"
"$SRC/packaging/windows/icons.sh" "$STAGE/icons"
cp "$SRC/LICENSE" "$STAGE/LICENSE.txt"
cp "$SRC/packaging/windows/LISEZMOI.txt" "$STAGE/"

mkdir -p "$OUT"
rm -f "$OUT/EasyLunii-$VERSION-win64.zip"
(cd "$BUILD" && zip -qr9 "$OUT/EasyLunii-$VERSION-win64.zip" EasyLunii)
if ! command -v makensis >/dev/null; then
    echo "makensis absent (paquet mingw64-nsis) : installeur non créé"
    ls -la "$OUT"
    exit 0
fi
makensis -V2 -DVERSION="$VERSION" -DSTAGE="$STAGE" -DOUTFILE="$OUT/EasyLunii-$VERSION-setup.exe" \
    -DICON="$SRC/res/easylunii.ico" "$SRC/packaging/windows/installer.nsi"
echo "Fichiers créés dans $OUT :"
ls -la "$OUT"
