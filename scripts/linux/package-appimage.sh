#!/usr/bin/env bash
#
# PhotoHub — construction d'une AppImage autonome (Option 2).
#
# Produit dist/PhotoHub-<version>-x86_64.AppImage : un fichier unique et
# portable qui embarque Qt et ses dépendances. L'utilisateur final n'a qu'à le
# rendre exécutable et le lancer — aucune compilation, aucune installation de Qt.
# C'est l'artefact à joindre aux releases GitHub.
#
# Prérequis : PhotoHub compilé en Release (cmake --preset linux && cmake --build
# --preset linux), et une connexion internet au premier appel (téléchargement de
# linuxdeploy et de son greffon Qt, mis en cache ensuite).
#
# Usage :
#   scripts/linux/package-appimage.sh [--build <dossier>]
#     --build <dossier>   Dossier de compilation (défaut : build)
#     -h, --help          Affiche cette aide
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP_NAME="PhotoHub"
CMD="PhotoHub"
ARCH="x86_64"
BUILD_DIR="$ROOT/build"

die() { printf 'Erreur : %s\n' "$1" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --build) BUILD_DIR="${2:-}"; shift ;;
        -h|--help)
            cat <<'EOF'
PhotoHub — construction d'une AppImage autonome (Option 2).
Usage : scripts/linux/package-appimage.sh [--build <dossier>]
  --build <dossier>   Dossier de compilation (défaut : build)
  -h, --help          Affiche cette aide
EOF
            exit 0 ;;
        *) die "option inconnue : $1 (voir --help)" ;;
    esac
    shift
done

[ "$(uname -s)" = "Linux" ] || die "ce script doit être exécuté sous Linux."

BINARY="$BUILD_DIR/$APP_NAME"
[ -x "$BINARY" ] || die \
"binaire introuvable : $BINARY
  Compile d'abord :  cmake --preset linux && cmake --build --preset linux"

VERSION="$(head -n1 "$ROOT/VERSION" | tr -d '[:space:]')"
[ -n "$VERSION" ] || die "fichier VERSION vide ou absent."

ICON_SRC="$ROOT/resources/logo.png"
[ -f "$ICON_SRC" ] || die "icône introuvable : $ICON_SRC"

# --- Outils linuxdeploy (téléchargés une fois, puis mis en cache) ----------
TOOLS_DIR="$ROOT/.cache/appimage-tools"
mkdir -p "$TOOLS_DIR"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-$ARCH.AppImage"
PLUGIN_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-$ARCH.AppImage"
BASE_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous"

fetch() {   # fetch <url> <destination>
    local url="$1" dst="$2"
    [ -f "$dst" ] && return 0
    echo "Téléchargement : $(basename "$dst")"
    if command -v wget >/dev/null 2>&1; then
        wget -q -O "$dst" "$url" || die "échec du téléchargement de $url"
    elif command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$dst" "$url" || die "échec du téléchargement de $url"
    else
        die "ni wget ni curl disponible pour télécharger les outils AppImage."
    fi
    chmod +x "$dst"
}
fetch "$BASE_URL/linuxdeploy-$ARCH.AppImage"            "$LINUXDEPLOY"
fetch "$QT_URL/linuxdeploy-plugin-qt-$ARCH.AppImage"    "$PLUGIN_QT"

# --- Préparation de l'AppDir ----------------------------------------------
APPDIR="$ROOT/dist/AppDir"
rm -rf "$APPDIR"
install -Dm755 "$BINARY" "$APPDIR/usr/bin/$CMD"
install -Dm644 "$SCRIPT_DIR/PhotoHub.desktop" "$APPDIR/usr/share/applications/$CMD.desktop"

# Icône : tailles standard si ImageMagick est présent, sinon l'image d'origine.
if command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1; then
    CONVERT="convert"; command -v magick >/dev/null 2>&1 && CONVERT="magick"
    for size in 32 48 64 128 256; do
        dest="$APPDIR/usr/share/icons/hicolor/${size}x${size}/apps/$CMD.png"
        mkdir -p "$(dirname "$dest")"
        "$CONVERT" "$ICON_SRC" -resize "${size}x${size}" "$dest"
    done
else
    install -Dm644 "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/256x256/apps/$CMD.png"
fi

# --- Construction de l'AppImage -------------------------------------------
# APPIMAGE_EXTRACT_AND_RUN évite d'exiger FUSE (utile en conteneur / CI).
# QMAKE aide le greffon Qt à localiser l'installation Qt du système.
export APPIMAGE_EXTRACT_AND_RUN=1
export VERSION
if command -v qmake6 >/dev/null 2>&1; then export QMAKE="$(command -v qmake6)"
elif command -v qmake  >/dev/null 2>&1; then export QMAKE="$(command -v qmake)"
fi

echo "Construction de l'AppImage (Qt embarqué)…"
( cd "$ROOT/dist" && "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/$CMD" \
    --desktop-file "$APPDIR/usr/share/applications/$CMD.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/$CMD.png" \
    --plugin qt \
    --output appimage )

# linuxdeploy nomme le fichier PhotoHub-<version>-x86_64.AppImage (via $VERSION).
RESULT="$(ls -t "$ROOT/dist"/*.AppImage 2>/dev/null | head -n1)"
[ -n "$RESULT" ] || die "AppImage non produite (voir les messages ci-dessus)."

FINAL="$ROOT/dist/${APP_NAME}-${VERSION}-${ARCH}.AppImage"
[ "$RESULT" = "$FINAL" ] || mv -f "$RESULT" "$FINAL"
chmod +x "$FINAL"

SIZE="$(du -h "$FINAL" | cut -f1)"
echo "OK — $FINAL ($SIZE)"
echo "Test rapide :  \"$FINAL\" &"
