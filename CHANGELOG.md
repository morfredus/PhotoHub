# Journal des versions - PhotoHub

Le format s'inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/)
et du [versionnage sémantique](https://semver.org/lang/fr/).

## [0.6.0] - 2026-08-14

### Modifié

- **Analyses avancées : le contexte de la source est conservé.** Le bouton
  « Analyses avancées… » ouvre désormais `/photo?source=<morfPhoto sélectionné>` :
  morfAnalytics ouvre directement les analyses de la MÊME photothèque que celle en
  cours dans PhotoHub, sans redemander la source. L'URL est construite au clic à
  partir de l'instance morfPhoto active.

## [0.5.0] - 2026-08-13

### Ajouté

- **Assistant d'accès réseau** (`Fichier > Assistant d'accès réseau…`) : rend le
  partage des photos accessible à un utilisateur non spécialiste. PhotoHub détecte
  automatiquement le nom du PC, l'adresse IP du LAN et le **compte Windows** (souvent
  différent du dossier de profil, source classique d'échec du montage SMB). Il crée
  le partage Windows **en lecture seule** en un clic (élévation UAC) et génère les
  commandes du Raspberry Pi (montage + `fstab`) déjà remplies, prêtes à coller, avec
  un rappel clair sur le mot de passe (session Windows / compte Microsoft, jamais un
  code PIN). Les commandes se copient dans le presse-papiers.

## [0.4.0] - 2026-08-13

### Ajouté

- **Fenêtre « Dossiers retirés »** (`Fichier > Dossiers retirés…`) : les dossiers
  retirés (retrait doux) quittent la liste principale pour ne plus l'encombrer et se
  consultent dans une fenêtre dédiée, avec un bouton **Restaurer** et l'horodatage du
  retrait. Le nombre de dossiers retirés apparaît dans l'entrée de menu, grisée tant
  qu'il n'y en a aucun.
- **`restoreFolder`** dans le client morfPhoto (`POST /api/v1/folders/{id}/restore`).

### Modifié

- La liste principale des dossiers surveillés n'affiche plus que les dossiers actifs :
  les dossiers retirés en sont filtrés (champ `deleted_at`).

## [0.3.0] - 2026-08-13

### Ajouté

- **Mappage de chemins Windows ↔ Linux** (`Fichier > Mappage de chemins…`) :
  morfPhoto tourne sur Linux ARM64 (Raspberry Pi) et accède aux photos via un
  montage réseau (ex. `/mnt/photos` → `C:\Users\frede\Pictures`). PhotoHub
  stocke désormais ces correspondances dans `QSettings` (registre Windows) et
  les applique automatiquement lors de l'ajout d'un dossier.
  Le dialogue de configuration présente un tableau éditable pré-rempli avec le
  dossier Images de l'utilisateur et la racine `/mnt/photos` par défaut.

### Modifié

- **"Ajouter un dossier"** : retour au sélecteur natif Windows (`QFileDialog`)
  démarrant dans `Pictures`. PhotoHub traduit le chemin local en chemin serveur
  via les mappages configurés, puis affiche un dialogue de confirmation avec
  les deux chemins. L'utilisateur peut corriger le chemin serveur à la main si
  nécessaire. morfPhoto reste l'autorité finale (périmètre, existence, droits).
  Un avertissement s'affiche si aucun mappage n'est configuré.

## [0.2.3] - 2026-08-13

### Corrigé

- **Dialogue "Ajouter un dossier" guidé (cross-platform)** : le `QInputDialog` texte
  libre de 0.2.2 ne suffisait pas à empêcher la saisie d'un chemin Windows alors que
  morfPhoto s'exécute sur un serveur Linux. Remplacé par un `QDialog` structuré :
  - un **combo** liste toutes les racines autorisées (chemins serveur) ;
  - un **champ texte** est initialisé sur la racine sélectionnée et se met à jour
    automatiquement si l'on change de racine dans le combo ;
  - un avertissement explicite signale que le chemin doit correspondre au
    système de fichiers du serveur, pas au poste local.
  L'utilisateur ne peut qu'étendre un chemin serveur existant, ce qui élimine
  la confusion avec les chemins Windows (`C:\...`).

## [0.2.2] - 2026-08-13

### Corrigé

- **Ajout de dossier inutilisable depuis Windows** : la correction 0.2.1 introduisait
  une validation locale des chemins qui était structurellement incorrecte en contexte
  cross-platform : PhotoHub tournant sur Windows ouvrait un sélecteur de fichiers
  *local* (`QFileDialog`) et comparait les chemins Windows (`C:/...`) aux racines
  autorisées de morfPhoto, qui sont des chemins *serveur* Linux (`/mnt/photos`…).
  La validation bloquait donc toujours. Corrigé en remplaçant `QFileDialog` par
  `QInputDialog` (saisie texte) pré-remplie avec la première racine autorisée : l'utilisateur
  saisit directement le chemin tel que le serveur morfPhoto le voit. morfPhoto reste
  l'autorité de validation (périmètre, existence, droits). `#include <QDir>` supprimé
  (devenu inutile).

## [0.2.1] - 2026-08-13

### Corrigé

- **Sélection d'un dossier hors périmètre** : lorsque l'utilisateur choisissait un
  dossier non couvert par les racines autorisées de morfPhoto, l'erreur renvoyée par
  l'API (403) remontait sous forme d'un message générique peu explicite
  ("n'a aucune racine autorisée"). PhotoHub valide désormais le chemin localement
  avant l'appel API : les deux chemins sont normalisés (`QDir::cleanPath`, casse
  ignorée) pour être robustes sur Windows et les montages réseau. Si le dossier choisi
  n'est sous aucune racine autorisée, un message clair liste le dossier sélectionné et
  les racines permises, sans solliciter morfPhoto.

## [0.2.0] - 2026-08-11

### Ajouté

- **Icône de l'application** (fenêtre + exécutable Windows) : appareil photo stylisé.
- **Barre de menus** : *Fichier > Quitter* ; *Aide > Rechercher les mises à jour…*
  et *À propos de PhotoHub*.
- **Vérification des mises à jour** via **morfUpdate** (GitHub Releases), vendoré dans
  `third_party/morf/update` (resync par `scripts/sync-morf`). Vérification
  automatique et silencieuse au démarrage (comme ComponentHub et SiteWatch) ;
  vérification manuelle depuis le menu Aide (affiche aussi « à jour » et les erreurs).
- **Racines autorisées affichées** : PhotoHub lit `GET /api/v1/roots` de morfPhoto,
  montre le périmètre permis, ouvre le sélecteur de dossier sur la première racine, et
  prévient clairement si aucune racine n'est configurée. morfPhoto reste l'autorité
  (refus 403 hors périmètre).

## [0.1.1] - 2026-08-11

### Corrigé

- **Le dossier de build Windows n'était pas autonome** : les DLL Qt et MinGW
  manquaient à côté de l'exécutable, qui ne se lançait qu'avec MinGW sur le PATH.
  Ajout des post-builds `windeployqt` (DLL Qt, plugin `platforms/qwindows.dll`,
  runtime du compilateur) et `scripts/windows/deploy-mingw.sh` (DLL non-Qt de
  `/mingw64/bin` : runtime gcc, ICU, pcre2, zlib...). L'exe se lance désormais en
  autonome (double-clic, machine sans MinGW). Même mécanisme que SiteWatch.

## [0.1.0] - 2026-08-11

Première version. Application desktop du domaine photo, client pur de morfPhoto.

### Ajouté

- **Découverte par capacité** (`BeaconDiscovery`) : écoute des heartbeats morfBeacon
  (UDP 45454), repère morfPhoto par sa capacité `photo_index` sans adresse codée en
  dur, purge les instances qui ne s'annoncent plus. Sélection de la source dans une
  liste, auto-sélection du premier détecté.
- **Client HTTP** (`MorfPhotoClient`) : dialogue asynchrone avec `/api/v1` (résumé,
  dossiers, déclenchement d'indexation, état). Ne lit jamais les fichiers, ne lance
  jamais ExifTool, ne connaît pas SQLite.
- **Interface** (`MainWindow`, Qt Widgets) : statistiques de la photothèque (photos,
  boîtiers, objectifs, dossiers actifs, disparues) ; gestion des dossiers surveillés
  (ajouter via un sélecteur, activer/désactiver, retirer en retrait doux avec
  confirmation) ; indexation incrémentale ou complète avec suivi d'état et barre de
  progression ; rafraîchissement périodique.
- **Enrichissement optionnel** : bouton vers les analyses avancées si un service
  annonce la capacité `photo_analytics` (morfAnalytics). Aucune dépendance forte.
- Multi-plateforme : Windows, Linux x86_64, Raspberry Pi (ARM64). Qt Widgets +
  Network uniquement, aucune autre dépendance.
