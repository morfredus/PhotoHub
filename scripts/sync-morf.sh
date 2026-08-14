#!/usr/bin/env bash
# Resynchronise la copie vendoree de morfUpdate dans third_party/morf/update
# depuis le depot source voisin. PhotoHub n'embarque que morfUpdate (verification
# des mises a jour) ; la decouverte morfBeacon est un simple ecouteur UDP, sans
# bibliotheque a vendorer.
#
# Source par defaut : le dossier parent du projet. Surcharge : MORF_SRC_BASE=...
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_BASE="${MORF_SRC_BASE:-$(cd "$ROOT/.." && pwd)}"

if [ -d "$SRC_BASE/morfUpdate" ]; then
  SRC="$SRC_BASE/morfUpdate"
else
  SRC="$SRC_BASE/morfUpdate_travail"
fi
DST="$ROOT/third_party/morf/update"

if [ ! -d "$SRC" ]; then
  echo "!! Source introuvable pour morfUpdate : $SRC" >&2
  echo "   (definir MORF_SRC_BASE si les depots sont ailleurs)" >&2
  exit 1
fi

# Le CMakeLists vendore est volontairement allege : on ne recopie que include/,
# src/ et VERSION, jamais le CMakeLists (comme pour morfBeacon).
rm -rf "$DST/include" "$DST/src"
cp -r "$SRC/include" "$DST/include"
cp -r "$SRC/src"     "$DST/src"
cp    "$SRC/VERSION" "$DST/VERSION"
echo "OK  morfUpdate  (version $(cat "$DST/VERSION"))"
echo "Synchronisation terminee. Le CMakeLists vendore n'est pas modifie."
