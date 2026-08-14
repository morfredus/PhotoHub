#!/usr/bin/env bash
#
# SiteWatch — construction d'un paquet Debian (.deb).
#
# Produit dist/sitewatch_<version>_<arch>.deb : un paquet installable avec
#   sudo apt install ./sitewatch_<version>_<arch>.deb
# Le binaire est lié dynamiquement au Qt du système : le paquet DÉCLARE ses
# dépendances (Qt6, libssh2, zlib…) pour qu'apt les installe automatiquement.
#
# À utiliser sur Debian / Ubuntu / Raspberry Pi OS, après avoir compilé
# NATIVEMENT sur la même famille de distribution :
#   cmake --preset linux        (x86_64)   ->  build/SiteWatch
#   cmake --preset linux-arm64  (ARM64)    ->  build-arm64/SiteWatch
#
# Contrairement à l'AppImage (autonome), le .deb s'intègre proprement au système
# (menu, mises à jour, désinstallation via apt) mais suppose un Qt compatible
# dans les dépôts de la distribution.
#
# Usage :
#   scripts/linux/package-deb.sh [options]
#     --build <dossier>       Dossier de compilation (défaut : auto, cherche
#                             build/ puis build-arm64/)
#     --maintainer "Nom <email>"  Champ Maintainer du paquet
#     --depends "pkg, pkg…"   Force la liste des dépendances (sinon détection auto)
#     -h, --help              Affiche cette aide
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP_NAME="SiteWatch"     # nom du binaire compilé
CMD="sitewatch"          # nom du paquet / de la commande / du .desktop / de l'icône
BUILD_DIR=""             # vide = détection auto (build, build-arm64…)
MAINTAINER="${DEB_MAINTAINER:-morfredus <morfredus@users.noreply.github.com>}"
DEPENDS_OVERRIDE=""

# Dossiers de compilation natifs candidats, dans l'ordre d'essai. Le préfixe
# native x86_64 va dans build/ ; le natif ARM64 (Raspberry Pi) dans build-arm64/.
BUILD_CANDIDATES=("$ROOT/build" "$ROOT/build-arm64" "$ROOT/build-arm64-cross")

die() { printf 'Erreur : %s\n' "$1" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --build)      BUILD_DIR="${2:-}"; shift ;;
        --maintainer) MAINTAINER="${2:-}"; shift ;;
        --depends)    DEPENDS_OVERRIDE="${2:-}"; shift ;;
        -h|--help)
            cat <<'EOF'
SiteWatch — construction d'un paquet Debian (.deb).
Usage : scripts/linux/package-deb.sh [options]
  --build <dossier>            Dossier de compilation
                               (défaut : auto — build/ puis build-arm64/)
  --maintainer "Nom <email>"   Champ Maintainer du paquet
  --depends "pkg, pkg…"        Force les dépendances (sinon détection auto)
  -h, --help                   Affiche cette aide
EOF
            exit 0 ;;
        *) die "option inconnue : $1 (voir --help)" ;;
    esac
    shift
done

[ "$(uname -s)" = "Linux" ] || die "ce script doit être exécuté sous Linux."
command -v dpkg-deb >/dev/null 2>&1 || die "dpkg-deb introuvable (paquet 'dpkg')."

# --- Localisation du binaire ----------------------------------------------
# Sans --build, on cherche le binaire dans les dossiers candidats (build,
# build-arm64…) : ainsi le même script fonctionne sur un build x86_64 comme
# sur un build ARM64 natif (Raspberry Pi), sans option à préciser.
if [ -n "$BUILD_DIR" ]; then
    BINARY="$BUILD_DIR/$APP_NAME"
    [ -x "$BINARY" ] || die \
"binaire introuvable : $BINARY
  Compile d'abord ce dossier, ou indique le bon :  --build <dossier>"
else
    for dir in "${BUILD_CANDIDATES[@]}"; do
        if [ -x "$dir/$APP_NAME" ]; then
            BUILD_DIR="$dir"
            BINARY="$dir/$APP_NAME"
            break
        fi
    done
    [ -n "$BUILD_DIR" ] || die \
"binaire introuvable dans : ${BUILD_CANDIDATES[*]}
  Compile d'abord (build natif) :
    cmake --preset linux        && cmake --build --preset linux        (x86_64)
    cmake --preset linux-arm64  && cmake --build --preset linux-arm64  (ARM64)
  ou indique le dossier :  scripts/linux/package-deb.sh --build <dossier>"
fi
echo "Binaire : $BINARY"

VERSION="$(head -n1 "$ROOT/VERSION" | tr -d '[:space:]')"
[ -n "$VERSION" ] || die "fichier VERSION vide ou absent."

ARCH="$(dpkg --print-architecture)"   # amd64, arm64, armhf…
ICON_SRC="$ROOT/resources/logo.png"
[ -f "$ICON_SRC" ] || die "icône introuvable : $ICON_SRC"

# --- Détection des dépendances --------------------------------------------
# On résout les bibliothèques partagées du binaire, puis les paquets qui les
# fournissent : apt installera exactement ce dont ce binaire a besoin.
detect_depends() {
    ldd "$BINARY" 2>/dev/null | awk '/=> \//{print $3}' | sort -u \
        | while read -r lib; do dpkg -S "$lib" 2>/dev/null | cut -d: -f1; done \
        | sort -u | paste -sd, - | sed 's/,/, /g'
}

if [ -n "$DEPENDS_OVERRIDE" ]; then
    DEPENDS="$DEPENDS_OVERRIDE"
else
    DEPENDS="$(detect_depends || true)"
    [ -n "$DEPENDS" ] || DEPENDS="libc6, libqt6core6, libqt6gui6, libqt6widgets6, libqt6network6, libqt6charts6, zlib1g, libssh2-1"

    # Le plugin de plate-forme Qt « xcb » est chargé dynamiquement (dlopen) au
    # démarrage : ldd ne le voit pas, donc la détection auto ci-dessus le rate.
    # Depuis Qt 6.5 il exige libxcb-cursor0, sinon l'appli refuse de démarrer
    # (« xcb-cursor0 or libxcb-cursor0 is needed »). On l'ajoute explicitement.
    case ", $DEPENDS," in
        *", libxcb-cursor0,"*) : ;;
        *) DEPENDS="$DEPENDS, libxcb-cursor0" ;;
    esac
fi

# --- Arborescence du paquet ------------------------------------------------
PKGDIR="$ROOT/dist/deb/${CMD}_${VERSION}_${ARCH}"
rm -rf "$PKGDIR"
install -Dm755 "$BINARY" "$PKGDIR/usr/bin/$CMD"

# .desktop : on fige le chemin absolu du binaire installé.
# On crée le dossier parent AVANT la redirection : bash n'ouvrira pas un
# fichier en écriture dans un dossier inexistant (sinon : "No such file or
# directory" et set -e arrête le script, donc pas de .deb produit).
install -d "$PKGDIR/usr/share/applications"
sed "s|^Exec=.*|Exec=/usr/bin/$CMD|" "$SCRIPT_DIR/sitewatch.desktop" \
    > "$PKGDIR/usr/share/applications/$CMD.desktop"
chmod 644 "$PKGDIR/usr/share/applications/$CMD.desktop"

# Icônes du thème hicolor (tailles standard si ImageMagick est présent).
if command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1; then
    CONVERT="convert"; command -v magick >/dev/null 2>&1 && CONVERT="magick"
    for size in 16 24 32 48 64 128 256; do
        dest="$PKGDIR/usr/share/icons/hicolor/${size}x${size}/apps/$CMD.png"
        mkdir -p "$(dirname "$dest")"
        "$CONVERT" "$ICON_SRC" -resize "${size}x${size}" "$dest"
    done
else
    install -Dm644 "$ICON_SRC" "$PKGDIR/usr/share/icons/hicolor/256x256/apps/$CMD.png"
fi

# Documentation Debian : copyright (obligatoire) + journal des modifications.
install -d "$PKGDIR/usr/share/doc/$CMD"
cat > "$PKGDIR/usr/share/doc/$CMD/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: SiteWatch
Source: https://morfredus.fr

Files: *
Copyright: 2026 morfredus
License: GPL-3.0-only
 This program is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License, version 3.
 .
 On Debian systems, the full text is in /usr/share/common-licenses/GPL-3.
EOF
printf '%s (%s) unstable; urgency=low\n\n  * SiteWatch %s.\n\n -- %s  %s\n' \
    "$CMD" "$VERSION" "$VERSION" "$MAINTAINER" "$(date -R)" \
    | gzip -9n > "$PKGDIR/usr/share/doc/$CMD/changelog.Debian.gz"

# Taille installée (en Ko), attendue par dpkg dans le champ Installed-Size.
INSTALLED_KB="$(du -sk "$PKGDIR" | cut -f1)"

# --- Fichier de contrôle ---------------------------------------------------
install -d "$PKGDIR/DEBIAN"
cat > "$PKGDIR/DEBIAN/control" <<EOF
Package: $CMD
Version: $VERSION
Architecture: $ARCH
Maintainer: $MAINTAINER
Installed-Size: $INSTALLED_KB
Depends: $DEPENDS
Section: net
Priority: optional
Homepage: https://morfredus.fr
Description: Apache/LiteSpeed access log investigation tool
 SiteWatch downloads Apache/LiteSpeed access logs over SFTP, analyzes them
 locally and highlights what really happened on a web server: bots (including
 AI crawlers), probable attacks, HTTP errors and WordPress activity.
 .
 It is a cross-platform Qt/C++ desktop application (Windows, Linux, Raspberry
 Pi). No SQL database, no WordPress plugin: it reads the compressed logs
 directly.
EOF

# --- Construction ----------------------------------------------------------
OUT="$ROOT/dist/${CMD}_${VERSION}_${ARCH}.deb"
# --root-owner-group : les fichiers appartiennent à root (dpkg >= 1.19).
if dpkg-deb --help 2>&1 | grep -q -- '--root-owner-group'; then
    dpkg-deb --build --root-owner-group "$PKGDIR" "$OUT"
else
    dpkg-deb --build "$PKGDIR" "$OUT"
fi

SIZE="$(du -h "$OUT" | cut -f1)"
echo "OK — $OUT ($SIZE, $ARCH)"
echo "Dépendances : $DEPENDS"
echo "Installation :  sudo apt install \"$OUT\""
echo "Vérification :  lintian \"$OUT\"   (optionnel)"