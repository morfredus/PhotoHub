# Resynchronise la copie vendoree de morfUpdate dans third_party\morf\update
# depuis le depot source voisin. PhotoHub n'embarque que morfUpdate.
#
# Source par defaut : le dossier parent du projet.
# Surcharge : $env:MORF_SRC_BASE = "C:\chemin\vers\depots"
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$srcBase = if ($env:MORF_SRC_BASE) { $env:MORF_SRC_BASE } else { Split-Path -Parent $root }

$src = if (Test-Path "$srcBase\morfUpdate") { "$srcBase\morfUpdate" } else { "$srcBase\morfUpdate_travail" }
$dst = "$root\third_party\morf\update"

if (-not (Test-Path $src)) {
    Write-Error "Source introuvable pour morfUpdate : $src (definir MORF_SRC_BASE si ailleurs)"
}

# CMakeLists vendore volontairement allege : on ne recopie que include/, src/ et VERSION.
Remove-Item -Recurse -Force "$dst\include", "$dst\src" -ErrorAction SilentlyContinue
Copy-Item -Recurse "$src\include" "$dst\include"
Copy-Item -Recurse "$src\src"     "$dst\src"
Copy-Item "$src\VERSION" "$dst\VERSION"
$v = (Get-Content "$dst\VERSION" -First 1).Trim()
Write-Output "OK  morfUpdate  (version $v)"
Write-Output "Synchronisation terminee. Le CMakeLists vendore n'est pas modifie."
