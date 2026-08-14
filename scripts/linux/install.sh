#!/usr/bin/env bash
#
# PhotoHub — intégration au bureau Linux (Option 1).
#
# Copie un binaire PhotoHub DÉJÀ COMPILÉ dans les dossiers standards Linux et
# crée l'icône de lancement (raccourci d'application). Utilise le Qt du système
# (ne regroupe aucune bibliothèque) : à réserver aux machines où PhotoHub a été
# compilé localement (cmake --preset linux). Pour une distribution sans
# compilation, voir package-appimage.sh.
#
# Usage :
#   scripts/linux/install.sh [options]
#     --system            Installation pour tous les utilisateurs (/usr/local, sudo)
#                         Par défaut : installation pour l'utilisateur courant (~/.local)
#     --binary <chemin>   Chemin explicite du binaire PhotoHub à installer
#     --uninstall         Désinstalle (retire binaire, icône et raccourci)
#     -h, --help          Affiche cette aide
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP_NAME="PhotoHub"      # nom affiché
CMD="PhotoHub"          # nom de la commande / du fichier .desktop / de l'icône

MODE="user"
UNINSTALL=0
BINARY=""

die() { printf 'Erreur : %s\n' "$1" >&2; exit 1; }

usage() {
    cat <<'EOF'
PhotoHub — intégration au bureau Linux (Option 1).
Copie un binaire PhotoHub déjà compilé et crée l'icône de lancement.

Usage : scripts/linux/install.sh [options]
  --system            Installation pour tous les utilisateurs (/usr/local, sudo)
                      Défaut : utilisateur courant (~/.local, sans sudo)
  --binary <chemin>   Chemin explicite du binaire PhotoHub à installer
  --uninstall         Désinstalle (binaire, icône et raccourci)
  -h, --help          Affiche cette aide
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --system)    MODE="system" ;;
        --uninstall) UNINSTALL=1 ;;
        --binary)    BINARY="${2:-}"; shift ;;
        -h|--help)   usage; exit 0 ;;
        *) die "option inconnue : $1 (voir --help)" ;;
    esac
    shift
done

# --- Emplacements selon le mode -------------------------------------------
if [ "$MODE" = "system" ]; then
    PREFIX="/usr/local"
    if [ "$(id -u)" -ne 0 ]; then
        die "l'installation --system nécessite les droits root (relance avec sudo)."
    fi
else
    PREFIX="$HOME/.local"
fi

BIN_DIR="$PREFIX/bin"
APPS_DIR="$PREFIX/share/applications"
ICONS_DIR="$PREFIX/share/icons/hicolor"
DESKTOP_FILE="$APPS_DIR/$CMD.desktop"

refresh_caches() {
    command -v update-desktop-database >/dev/null 2>&1 && \
        update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && \
        gtk-update-icon-cache -f "$ICONS_DIR" >/dev/null 2>&1 || true
}

# --- Désinstallation -------------------------------------------------------
if [ "$UNINSTALL" -eq 1 ]; then
    rm -f "$BIN_DIR/$CMD" "$DESKTOP_FILE"
    find "$ICONS_DIR" -name "$CMD.png" -delete 2>/dev/null || true
    rm -f "$PREFIX/share/pixmaps/$CMD.png"
    refresh_caches
    echo "PhotoHub désinstallé de $PREFIX."
    exit 0
fi

# --- Localisation du binaire ----------------------------------------------
if [ -z "$BINARY" ]; then
    for candidate in "$ROOT/build/$APP_NAME" "$ROOT/build-linux/$APP_NAME"; do
        if [ -x "$candidate" ]; then BINARY="$candidate"; break; fi
    done
fi
[ -n "$BINARY" ] && [ -x "$BINARY" ] || die \
"binaire PhotoHub introuvable.
  Compile d'abord :  cmake --preset linux && cmake --build --preset linux
  ou indique-le   :  scripts/linux/install.sh --binary /chemin/vers/PhotoHub"

ICON_SRC="$ROOT/resources/logo.png"
[ -f "$ICON_SRC" ] || die "icône introuvable : $ICON_SRC"

# --- Copie du binaire ------------------------------------------------------
install -Dm755 "$BINARY" "$BIN_DIR/$CMD"

# --- Icônes ----------------------------------------------------------------
# Avec ImageMagick, on génère les tailles standard du thème hicolor ; sinon on
# dépose l'image d'origine en 256x256 (les environnements de bureau savent la
# redimensionner) plus un repli dans pixmaps/.
if command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1; then
    CONVERT="convert"; command -v magick >/dev/null 2>&1 && CONVERT="magick"
    for size in 16 24 32 48 64 128 256; do
        dest="$ICONS_DIR/${size}x${size}/apps/$CMD.png"
        mkdir -p "$(dirname "$dest")"
        "$CONVERT" "$ICON_SRC" -resize "${size}x${size}" "$dest"
    done
else
    install -Dm644 "$ICON_SRC" "$ICONS_DIR/256x256/apps/$CMD.png"
    install -Dm644 "$ICON_SRC" "$PREFIX/share/pixmaps/$CMD.png"
fi

# --- Raccourci d'application (.desktop) ------------------------------------
# On part du modèle partagé et on fige le chemin absolu du binaire installé.
mkdir -p "$APPS_DIR"
sed "s|^Exec=.*|Exec=$BIN_DIR/$CMD|" "$SCRIPT_DIR/PhotoHub.desktop" > "$DESKTOP_FILE"
chmod 644 "$DESKTOP_FILE"

refresh_caches

echo "PhotoHub installé dans $PREFIX."
echo "  Binaire   : $BIN_DIR/$CMD"
echo "  Raccourci : $DESKTOP_FILE"
echo "Lance-le depuis le menu des applications, ou tape « $CMD » dans un terminal."

# Avertissement si ~/.local/bin n'est pas dans le PATH (fréquent en mode user).
case ":$PATH:" in
    *":$BIN_DIR:"*) : ;;
    *) [ "$MODE" = "user" ] && echo \
       "Note : $BIN_DIR n'est pas dans votre PATH ; le menu des applications fonctionnera quand même." ;;
esac
