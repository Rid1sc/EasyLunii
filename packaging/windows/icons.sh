#!/bin/sh
# Embarque le sous-ensemble du thème Breeze (LGPL) utilisé par l'appli : Windows n'a pas de
# thème d'icônes freedesktop. Usage : icons.sh <dossier_de_sortie>
set -eu
SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$1
NAMES=$(grep -rhoE 'fromTheme\("[^"]+"' "$SRC/src" | sed 's/.*("//; s/"$//' | sort -u)
for theme in breeze breeze-dark; do
    from=/usr/share/icons/$theme
    to=$OUT/$theme
    dirs=""
    for name in $NAMES; do
        for f in $(cd "$from" && find . \( -name "$name.svg" -o -name "$name.png" \) | grep -vE '@[0-9]x'); do
            d=$(dirname "${f#./}")
            mkdir -p "$to/$d"
            cp -L "$from/$f" "$to/$d/"
            case " $dirs " in *" $d "*) ;; *) dirs="$dirs $d" ;; esac
        done
    done
    # index.theme réduit aux dossiers copiés, en reprenant leur description d'origine
    {
        printf '[Icon Theme]\nName=%s\nInherits=\nDirectories=%s\n\n' "$theme" "$(echo $dirs | tr ' ' ',')"
        for d in $dirs; do
            awk -v sec="[$d]" '$0 == sec { p = 1; print; next } /^\[/ { p = 0 } p' "$from/index.theme"
            echo
        done
    } > "$to/index.theme"
done
cp /usr/share/licenses/breeze-icon-theme*/COPYING-ICONS "$OUT/COPYING-ICONS.txt" 2>/dev/null || true
echo "Icônes : $(find "$OUT" -type f | wc -l) fichiers"
