# Resynchronise les copies vendorees de morfBeacon et morfUpdate dans
# third_party\morf\ depuis les depots sources voisins. PhotoHub annonce sa
# presence (morfBeacon) et verifie ses mises a jour (morfUpdate).
#
# Source par defaut : le dossier parent du projet.
# Surcharge : $env:MORF_SRC_BASE = "C:\chemin\vers\depots"
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$srcBase = if ($env:MORF_SRC_BASE) { $env:MORF_SRC_BASE } else { Split-Path -Parent $root }

# Le depot source peut porter le suffixe du bac a sable de la copie de travail
# (celui de ce projet) : prendre le premier des deux noms qui existe.
$suffix = if ((Split-Path -Leaf $root) -match '(_[a-z]+)$') { $Matches[1] } else { '' }
function Resolve-Src($name) {
    if (Test-Path "$srcBase\$name") { return "$srcBase\$name" }
    return "$srcBase\$name$suffix"
}

# CMakeLists vendore volontairement allege : on ne recopie que include/, src/ et VERSION.
function Sync-One($name, $dst) {
    $src = Resolve-Src $name
    if (-not (Test-Path $src)) {
        Write-Error "Source introuvable pour $name : $src (definir MORF_SRC_BASE si ailleurs)"
    }
    Remove-Item -Recurse -Force "$dst\include", "$dst\src" -ErrorAction SilentlyContinue
    Copy-Item -Recurse "$src\include" "$dst\include"
    Copy-Item -Recurse "$src\src"     "$dst\src"
    Copy-Item "$src\VERSION" "$dst\VERSION"
    $v = (Get-Content "$dst\VERSION" -First 1).Trim()
    Write-Output "OK  $name  (version $v)"
}

Sync-One "morfBeacon" "$root\third_party\morf\beacon"
Sync-One "morfUpdate" "$root\third_party\morf\update"
Write-Output "Synchronisation terminee. Le CMakeLists vendore n'est pas modifie."
