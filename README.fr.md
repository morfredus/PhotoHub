# PhotoHub

*Lire dans une autre langue : [English](README.md) · **Français** (ce document).*

[![Version](https://img.shields.io/badge/version-0.6.0-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![Build](https://img.shields.io/badge/CMake-3.21+-064F8C?logo=cmake)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**PhotoHub est l'application desktop du domaine photo.** C'est un **client pur** de
morfPhoto : il ne lit jamais les fichiers, ne lance jamais ExifTool, ne connaît pas
SQLite. Il trouve morfPhoto sur le réseau local par sa **capacité** morfBeacon
(`photo_index`) - aucune adresse codée en dur - et dialogue avec son contrat HTTP
`/api/v1`.

## Architecture de déploiement type

```
Windows (poste de travail)          Linux ARM64 (Raspberry Pi)
─────────────────────────           ──────────────────────────
C:\Users\frede\Pictures\  ←── Samba/SMB ───→  /mnt/photos/   (montage en lecture)
                                               morfPhoto       (indexeur, API HTTP)
PhotoHub  ──────────────────── HTTP /api/v1 ──→ morfPhoto
```

- Les **photos restent sur Windows**, jamais déplacées ni modifiées par morfPhoto.
- morfPhoto monte le partage Samba **en lecture seule** et n'écrit jamais dans les
  dossiers Windows.
- PhotoHub tourne sur Windows et utilise un **mappage de chemins**
  (`Fichier > Mappage de chemins…`) pour traduire les chemins locaux Windows
  (`C:\Users\frede\Pictures\…`) en chemins de montage Linux (`/mnt/photos/…`)
  avant de les envoyer à l'API de morfPhoto.

## Mettre en place l'accès réseau (assistant)

Le partage réseau est l'étape qui bloque le plus souvent : le nom du compte Windows
diffère du dossier de profil, l'adresse IP est inconnue, la syntaxe de `mount` est
ingrate. L'**Assistant d'accès réseau** (`Fichier > Assistant d'accès réseau…`) fait
ce travail à votre place, puisque PhotoHub tourne justement sur le PC où sont les photos :

1. Il détecte automatiquement le **nom du PC**, l'**adresse IP** du réseau local et le
   **compte Windows**.
2. Il crée le **partage Windows en lecture seule** du dossier photos, en un clic
   (une autorisation administrateur / UAC s'affiche).
3. Il génère les **commandes à coller sur le Raspberry Pi** (montage puis `fstab` pour
   le rendre permanent), déjà remplies avec les bonnes valeurs.

Le seul élément qui reste à saisir est le **mot de passe** : celui de votre session
Windows (ou de votre compte Microsoft si vous vous connectez ainsi). Un code PIN ne
fonctionne pas pour un accès réseau, et Windows refuse un mot de passe vide. Le partage
reste **en lecture seule** : morfPhoto ne peut jamais modifier vos photos.

## Ce qu'il fait

- **Découvre morfPhoto** par capacité sur le réseau local (heartbeats UDP).
- **Gère les dossiers surveillés** : déclarer un dossier, l'activer/désactiver, le
  retirer (retrait doux qui conserve l'historique - les fichiers passent absents,
  jamais supprimés).
- **Pilote l'indexation** : lancer une passe incrémentale ou complète et suivre son
  état.
- **Montre la photothèque d'un coup d'œil** : photos présentes, boîtiers, objectifs,
  dossiers actifs, fichiers disparus, et le résultat de la dernière passe.
- **Mappage de chemins** (`Fichier > Mappage de chemins…`) : traduit automatiquement
  les chemins Windows en chemins serveur Linux. Configuré une seule fois, persisté
  dans les préférences Windows (registre).
- **Assistant d'accès réseau** (`Fichier > Assistant d'accès réseau…`) : crée le
  partage Windows en lecture seule et génère les commandes de montage du Pi (voir
  la section dédiée ci-dessus).
- **Enrichissement optionnel** : si un service annonce la capacité `photo_analytics`
  (morfAnalytics), un bouton ouvre l'analyse Photo avancée. Aucune dépendance forte :
  PhotoHub fonctionne seul.

morfPhoto refuse (403) tout dossier hors des racines autorisées déclarées dans sa
propre configuration ; PhotoHub ne fait que relayer ce message.

## Compiler

Nécessite **Qt 6** (Widgets, Network). Aucune autre dépendance.

```sh
cmake --preset mingw        # ou linux / linux-arm64
cmake --build --preset mingw
```

## Lancer

```sh
./build-mingw/PhotoHub.exe   # Windows ; ./build/PhotoHub sous Linux
```

Le lancer sur un réseau où une instance morfPhoto s'annonce : elle apparaît
automatiquement dans la liste des sources.

## Licence

GPL-3.0-only - © 2026 morfredus (Frédéric Biron).
