# Journal des versions - PhotoHub

Le format s'inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/)
et du [versionnage sémantique](https://semver.org/lang/fr/).

## [0.10.13] - 2026-08-24

### Ajouté

- Section « Aperçu » / « Overview » dans les README (fr + en) avec une capture
  d'illustration de la fenêtre principale (`docs/pictures/interface.png`) :
  source morfPhoto, statistiques de la photothèque et dossiers surveillés. Les
  valeurs, chemins et hôtes sont des données d'exemple anonymisées.

## [0.10.12] - 2026-08-23

### Corrigé

- L'exemple de chemin du dialogue « Mappage de chemins » n'affiche plus un nom
  d'utilisateur réel (`C:\Users\frede\...`) mais un repère générique
  (`C:\Users\<vous>\...`), pour ne pas diffuser une valeur propre à un poste.

## [0.10.11] - 2026-08-23

### Ajouté

- Avant « Envoyer la config », PhotoHub interroge `GET /api/v1/sources/ready`.
  Si le helper privilégié de morfPhoto n'est pas démarrable, l'envoi s'arrête
  sans transmettre le mot de passe.

## [0.10.10] - 2026-08-23

### Ajouté

- Livrable Windows : `morfproject.json` (`windows-x86_64-zip`) et
  `scripts/windows/package-win.ps1`, pour que `package-all --sync` sous Windows
  publie le zip sur la release GitHub.
- Copie vendorée de morfUpdate alignée sur 0.4.5.

## [0.10.9] - 2026-08-22

### Modifié

- Pendant une passe, une seule ligne d'état (`Indexation… dossier 2/6 · 1 247
  fichiers examinés`) et la barre en pourcentage. Le bilan (connus, ajoutés,
  mis à jour, disparus, erreurs) n'apparaît que lorsque la barre disparaît.

## [0.10.8] - 2026-08-22

### Modifié

- Resynchroniser la copie vendorée de morfUpdate vers 0.4.3.

## [0.10.7] - 2026-08-22

### Modifié

- Barre d'indexation : le pourcentage est affiché sur la barre. Première passe explicitement nommée « comptage des fichiers » (sans EXIF), avec un % de dossiers comptés. Ensuite la barre réelle suit `files_seen / files_total`.

## [0.10.6] - 2026-08-22

### Corrigé

- Identifiants SMB validés en réel : **nom d'utilisateur Windows** dans les deux cas ; mot de passe de session (compte local) ou mot de passe **Microsoft** (compte lié). L'e-mail n'est pas l'identifiant SMB. L'assistant distingue les deux situations à côté du champ mot de passe (PIN / Hello / passkey exclus).
- Barre d'indexation : pourcentage sur les **fichiers** (`percent` de morfPhoto), barre indéterminée pendant la découverte ou si `percent` est `null`. Plus de 33 % / 66 % basés sur le nombre de dossiers. Interrogation plus fréquente pendant une passe. Résumé en fin de passe (examinés, déjà connus, ajoutés, erreurs).

## [0.10.5] - 2026-08-22

### Corrigé

- Déploiement MinGW après le lien : le Bash MSYS2 (déduit du compilateur, jamais du PATH Windows/WSL) reçoit `/usr/bin` et le dossier du `g++` dans `PATH`, pour que `grep`/`awk`/`ldd` existent aussi depuis PowerShell. `windeployqt` est cherché aussi sous `$QT_ROOT/bin`. Aucun `C:/msys64` en dur.

## [0.10.4] - 2026-08-22

### Corrigé

- Autoconfiguration SMB : le hostname du poste est l'identité canonique (`ASUS-DEV` → `/mnt/photos_asus-dev` + `smb-photos-asus-dev.cred`). Plus de fichier générique `smb-photos.cred` ni de point de montage `/mnt/photos` pour une nouvelle source.
- L'assistant affiche chaque étape (montage, fstab, JSON, redémarrage, `GET /status` et `GET /api/v1/roots`) et refuse un succès global si l'une d'elles échoue. Les erreurs d'authentification SMB sont distinguées des autres causes.
- Compte Windows local et compte Microsoft sont tous deux acceptés : l'identifiant est le nom de session ou l'e-mail, jamais un PIN ; aucun compte technique dédié n'est exigé.

## [0.10.3] - 2026-08-21

### Modifié

- Assistant d'accès réseau : identifiant ET mot de passe désormais éditables avant l'envoi de la config, et précision qu'ils sont ceux du compte Microsoft connecté (l'identifiant est l'email, pas le nom de profil local). Corrige l'échec d'authentification (STATUS_LOGON_FAILURE) sur les sessions à compte Microsoft.

## [0.10.2] - 2026-08-21

### Ajouté

- Enregistrement des compilations au niveau CMake (record_compile, vendoré) : la durée de compile est signalée à morfAnalytics quel que soit le déclencheur (cmake --build direct, morf upgrade, déploiement morfDeploy).

## [0.10.1] - 2026-08-21

### Corrigé

- Déploiement Windows : déduire le Bash MSYS2 du compilateur MinGW au lieu d'appeler `bash` du PATH, que PowerShell résout vers le Bash WSL (échec « No such file or directory » hors de VSCode). Aligné sur ComponentHub/SiteWatch.

## [0.10.0] - 2026-08-21

### Ajouté

- Bouton « Envoyer la config au serveur morfPhoto » (topologie serveur Linux) : un clic pousse la source SMB au lieu de coller des commandes ; le mappage local s'aligne sur le point de montage choisi par le serveur.

## [0.9.1] - 2026-08-21

### Modifié

- Resynchroniser la copie vendorée de morfUpdate vers 0.4.1.

## [0.9.0] - 2026-08-17

### Ajouté

- **Ajout de plusieurs dossiers en une fois.** « Ajouter un dossier… » permet désormais
  de sélectionner **plusieurs dossiers** (Ctrl/Maj) au lieu d'un seul — pensé d'abord
  pour un CD qui contient plusieurs dossiers (par année, par événement), mais utile aussi
  sur un poste de travail. Le dialogue de confirmation liste tous les dossiers avec leur
  **chemin serveur éditable** (pré-rempli par le mappage), signale ceux sans mappage, et
  le **support amovible + nom de volume** s'appliquent à toute la sélection (les dossiers
  d'un même CD partagent le volume). Chaque dossier est envoyé à morfPhoto qui reste
  l'autorité ; un échec sur l'un n'empêche pas les autres. (Sélecteur Qt non natif, requis
  pour la multi-sélection de dossiers.)

## [0.8.0] - 2026-08-17

### Modifié

- **Assistant d'accès réseau : trois topologies au lieu d'une.** Il ne suppose plus que
  morfPhoto tourne sur un Raspberry Pi. Un choix « morfPhoto tourne sur : » adapte les
  étapes :
  - **serveur Linux (Pi ou autre), photos partagées depuis ce PC** — partage SMB en un
    clic + commandes serveur généralisées (`cifs-utils` au besoin, montage `cifs`,
    `fstab`, rappel d'ajouter le point de montage à `roots`) ;
  - **ce PC Windows (morfPhoto et photos sur la même machine)** — aucun partage ni
    montage, juste le bloc `roots` à mettre dans `morfphoto.json` avec le dossier local ;
  - **autre PC Windows, photos partagées depuis ce PC** — partage ici + racine UNC
    (`//NOM-DU-PC/partage`) à déclarer dans le `roots` de la machine morfPhoto.
  La note « mot de passe » n'apparaît que pour le cas SMB depuis Linux ; un rappel
  « exiftool requis sur la machine morfPhoto » est ajouté. README FR + EN mis à jour.

## [0.7.0] - 2026-08-17

### Ajouté

- **Gestion des supports amovibles (CD/DVD, disques d'archive).** À l'ajout d'un
  dossier, une case « Support amovible » et un champ « Nom du support » (ex.
  `PHOTOS-2015`) déclarent une archive : morfPhoto ne marquera jamais ses photos
  disparues quand le disque est absent (elles restent dans les analyses, support
  éjecté et après un redémarrage). Un bouton **« Support amovible… »** permet de régler
  ce statut après coup. La table des dossiers gagne une colonne **« Support / Analyses »**
  (Fixe / Amovible « volume » / hors analyses). S'appuie sur morfPhoto 0.6.0.
- **Exclure un dossier des analyses sans perdre ses données.** Bouton
  **« Exclure des analyses » / « Réintégrer aux analyses »** : les photos restent
  indexées et consultables, seules les analyses de morfAnalytics les ignorent. Geste
  réversible, clairement distinct de la suppression.
- **Supprimer des données (définitif).** Bouton **« Supprimer des données… »** :
  suppression irréversible par dossier sélectionné, par année, par boîtier, ou totale
  (remise à zéro). Choix pré-remplis (années et boîtiers connus), double confirmation
  et saisie de `SUPPRIMER` exigée pour la remise à zéro complète. Les fichiers d'origine
  sur le disque ou le CD ne sont jamais touchés.

## [0.6.4] - 2026-08-15

### Ajouté

- **Cadence de l'indexation automatique affichée.** Sous le résumé de la dernière
  passe, PhotoHub indique maintenant si l'indexation de fond est active et à quelle
  fréquence (« auto : une fois par jour ») ou si elle est désactivée (« auto désactivée
  (à la demande) »), d'après le nouveau champ `watch` de morfPhoto 0.5.4. Le bouton
  d'indexation à la demande reste la voie manuelle pour prendre les ajouts tout de suite.

## [0.6.3] - 2026-08-15

### Ajouté

- **Annonce de présence sur morfBeacon**, comme ComponentHub et SiteWatch. PhotoHub
  reste un client (il découvre morfPhoto par capacité), mais se rend désormais visible
  dans la carte de l'écosystème : heartbeat UDP `morfbeacon/1` + endpoint `/status`
  (et `/healthz`) via `morfbeacon::PresenceService`, sur le port **8882** de l'`appRange`
  (réservé dans `morfTools/ecosystem.json`, hors du bloc des services). L'application
  apparaît quand elle est ouverte et passe « hors ligne » à sa fermeture. Capacité
  annoncée : `photo_client`. Bibliothèque **morfBeacon** vendorée dans
  `third_party/morf/beacon` (0.6.1, alignée sur la source).

## [0.6.2] - 2026-08-15

### Modifié

- **Barre de progression d'indexation déterminée.** Elle tournait en boucle
  (indéterminée, « Indexation en cours… ») faute de données. Elle reflète désormais la
  progression exposée par morfPhoto 0.5.3 : barre remplie selon les dossiers traités
  (`folders_done` / `folders_total`) et libellé « Indexation… dossier X/N · M fichiers ·
  <dossier courant> ». Repli propre sur l'ancienne barre indéterminée si morfPhoto ne
  fournit pas encore la progression (compatibilité descendante).

## [0.6.1] - 2026-08-14

### Corrigé

- **Analyses avancées : choix du morfAnalytics selon le morfPhoto sélectionné.** Le
  bouton ouvrait toujours le premier morfAnalytics entendu, quel que soit le morfPhoto
  choisi. Il vise désormais l'instance morfAnalytics du **même hôte** que le morfPhoto
  sélectionné (parc multi-machines : chaque machine a son couple morfPhoto/morfAnalytics,
  l'analyse reste locale à sa source). À défaut d'instance co-localisée, repli sur la
  première entendue, le paramètre `source` garantissant l'analyse de la bonne photothèque.

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
