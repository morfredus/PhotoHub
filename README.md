# PhotoHub

*Read in another language: **English** (this document) · [Français](README.fr.md).*

[![Version](https://img.shields.io/badge/version-0.10.12-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![Build](https://img.shields.io/badge/CMake-3.21+-064F8C?logo=cmake)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**PhotoHub is the desktop application of the photo domain.** It is a **pure client**
of morfPhoto: it never reads the photo files, never runs ExifTool, never touches
SQLite. It finds morfPhoto on the LAN by its morfBeacon **capability**
(`photo_index`) - no hard-coded address - and talks to its `/api/v1` HTTP contract.

## Typical deployment architecture

```
Windows workstation                 Linux ARM64 (Raspberry Pi)
───────────────────                 ──────────────────────────
C:\Users\frede\Pictures\  ←── Samba/SMB ───→  /mnt/photos_<hostname>/   (read-only mount)
                                               morfPhoto       (indexer, HTTP API)
PhotoHub  ──────────────────── HTTP /api/v1 ──→ morfPhoto
```

- **Photos stay on Windows**, never moved or modified by morfPhoto.
- morfPhoto mounts the Samba share **read-only** and never writes to Windows folders.
- PhotoHub runs on Windows and uses a **path mapping** system
  (`File > Path mapping…`) to translate local Windows paths
  (`C:\Users\frede\Pictures\…`) into Linux mount paths (`/mnt/photos_<hostname>/…`)
  before sending them to morfPhoto's API.

## Setting up access (assistant)

morfPhoto indexes **paths local to the machine it runs on** (its `roots`). How you
expose the photos depends on where it runs. The **Network access assistant**
(`File > Network access assistant…`) starts from a choice of situation and adapts the
steps. It covers three cases:

- **A Linux server (Raspberry Pi or other), photos shared from this PC.** It creates the
  **read-only Windows share** in one click (UAC prompt) and **sends the config**
  to morfPhoto, which mounts `//IP/share` under `/mnt/photos_<hostname>` with a
  dedicated credentials file. It also still generates the equivalent server
  commands (PC name, IP, account, share name auto-detected).
- **This Windows PC (morfPhoto and photos on the same machine).** No share, no mount:
  the assistant gives the `roots` block for `morfphoto.json` with the local folder
  (forward slashes). No path mapping is needed then.
- **Another Windows PC, photos shared from this PC.** It creates the share here, then
  gives the **UNC root** (`//PC-NAME/share`) to declare in the morfPhoto machine's
  `roots` — no mount. The account running the morfPhoto service must have access to the
  share.

For the SMB-from-Linux cases, type the **Windows username of this machine**
(not the Microsoft email) and the matching password:

- **local account**: Windows session password;
- **Microsoft-linked account**: password of the associated Microsoft account.

A PIN, Windows Hello, fingerprint, face or a passkey does not work for network
access. Windows rejects an empty password. You do not need a dedicated local
account. Problems, SMB status codes (`STATUS_ACCOUNT_LOCKED_OUT`, …) and the
validated layout (several Windows PhotoHub clients, one morfPhoto on the Pi)
are documented in the morfSystem notice
[PHOTOS-SOURCES-RESEAU](https://github.com/morfredus/morfSystem/blob/main/docs/PHOTOS-SOURCES-RESEAU.md).
The share stays **read-only**. In every case **exiftool**
must be installed on the morfPhoto machine (otherwise files are indexed but EXIF
metadata stays empty).

While indexing, PhotoHub shows one status line and a percentage bar. The pass
breakdown (known, added, errors) comes back when the bar disappears, as a summary.

## What it does

- **Discovers morfPhoto** by capability on the local network (UDP heartbeats).
- **Manages the watched folders**: declare a folder, enable/disable it, remove it
  (a soft retire that preserves history - files are marked missing, never deleted).
- **Drives indexing**: trigger an incremental or full pass and follow its state.
- **Shows the library at a glance**: photos present, cameras, lenses, active
  folders, missing files, and the result of the last indexing pass.
- **Path mapping** (`File > Path mapping…`): automatically translates Windows paths
  into Linux server paths. Configured once, persisted in Windows preferences
  (registry).
- **Network access assistant** (`File > Network access assistant…`): creates the
  read-only Windows share and generates the Pi mount commands (see the dedicated
  section above).
- **Optional enrichment**: if a service announces the `photo_analytics` capability
  (morfAnalytics), a button opens its advanced Photo analysis. No hard dependency:
  PhotoHub works fully on its own.

morfPhoto refuses (403) any folder outside the allowed roots declared in its own
configuration; PhotoHub simply surfaces that message.

## Build

Needs **Qt 6** (Widgets, Network). No other dependency.

```sh
cmake --preset mingw        # or linux / linux-arm64
cmake --build --preset mingw
```

## Run

```sh
./build-mingw/PhotoHub.exe   # Windows ; ./build/PhotoHub on Linux
```

Start it on a network where a morfPhoto instance is announcing itself, and it
appears in the source list automatically.

## License

GPL-3.0-only - © 2026 morfredus (Frédéric Biron).
