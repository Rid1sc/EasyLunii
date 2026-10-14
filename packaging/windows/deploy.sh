#!/bin/sh
# Copie dans <dossier> les DLL nécessaires aux .exe (dépendances suivies récursivement)
# et les greffons Qt utilisés (plateforme, style, images, SQLite, multimédia, TLS).
set -eu
STAGE=$1
ROOT=${MINGW_ROOT:-/usr/x86_64-w64-mingw32/sys-root/mingw}
OBJDUMP=${OBJDUMP:-x86_64-w64-mingw32-objdump}
PLUGINS="$ROOT/lib/qt6/plugins"

for d in platforms styles imageformats sqldrivers multimedia tls iconengines; do
    [ -d "$PLUGINS/$d" ] || continue
    mkdir -p "$STAGE/$d"
done
cp "$PLUGINS/platforms/qwindows.dll" "$STAGE/platforms/"
[ -f "$PLUGINS/styles/qmodernwindowsstyle.dll" ] && cp "$PLUGINS/styles/qmodernwindowsstyle.dll" "$STAGE/styles/"
for f in qjpeg qgif qico qsvg qwebp; do [ -f "$PLUGINS/imageformats/$f.dll" ] && cp "$PLUGINS/imageformats/$f.dll" "$STAGE/imageformats/"; done
cp "$PLUGINS/sqldrivers/qsqlite.dll" "$STAGE/sqldrivers/"
cp "$PLUGINS"/multimedia/*.dll "$STAGE/multimedia/" 2>/dev/null || true
cp "$PLUGINS"/tls/*.dll "$STAGE/tls/" 2>/dev/null || true
[ -f "$PLUGINS/iconengines/qsvgicon.dll" ] && cp "$PLUGINS/iconengines/qsvgicon.dll" "$STAGE/iconengines/"
printf '[Paths]\nPlugins = .\n' > "$STAGE/qt.conf"

# fermeture transitive des DLL importées, limitée à celles fournies par MinGW
seen=""
queue=$(find "$STAGE" -name '*.exe' -o -name '*.dll')
while [ -n "$queue" ]; do
    next=""
    for bin in $queue; do
        for dll in $($OBJDUMP -p "$bin" | sed -n 's/^\s*DLL Name: //p'); do
            case " $seen " in *" $dll "*) continue ;; esac
            seen="$seen $dll"
            src=$(find "$ROOT/bin" -maxdepth 1 -iname "$dll" | head -1)
            [ -n "$src" ] || continue           # DLL système Windows
            [ -f "$STAGE/$(basename "$src")" ] && continue
            cp "$src" "$STAGE/"
            next="$next $STAGE/$(basename "$src")"
        done
    done
    queue=$next
done
echo "Déploiement : $(ls "$STAGE"/*.dll | wc -l) DLL copiées"
