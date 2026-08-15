# PhotoHub

*Read in another language: **English** (this document) · [Français](README.fr.md).*

[![Version](https://img.shields.io/badge/version-0.6.3-blue)](CHANGELOG.md)
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
C:\Users\frede\Pictures\  ←── Samba/SMB ───→  /mnt/photos/   (read-only mount)
                                               morfPhoto       (indexer, HTTP API)
PhotoHub  ──────────────────── HTTP /api/v1 ──→ morfPhoto
```

- **Photos stay on Windows**, never moved or modified by morfPhoto.
- morfPhoto mounts the Samba share **read-only** and never writes to Windows folders.
- PhotoHub runs on Windows and uses a **path mapping** system
  (`File > Path mapping…`) to translate local Windows paths
  (`C:\Users\frede\Pictures\…`) into Linux mount paths (`/mnt/photos/…`)
  before sending them to morfPhoto's API.

## Setting up network access (assistant)

Network sharing is the step that most often trips people up: the Windows account
name differs from the profile folder, the IP is unknown, `mount` syntax is unfriendly.
The **Network access assistant** (`File > Network access assistant…`) does it for you,
since PhotoHub runs on the very PC that holds the photos:

1. It auto-detects the **PC name**, the **LAN IP** and the **Windows account**.
2. It creates the **read-only Windows share** of the photo folder in one click
   (a UAC elevation prompt appears).
3. It generates the **Raspberry Pi commands** (mount, then `fstab` to make it
   permanent), pre-filled with the right values, ready to paste.

The only thing left to type is the **password**: your Windows sign-in password (or
your Microsoft account password if that is how you sign in). A PIN does not work for
network access, and Windows rejects an empty password. The share stays **read-only**:
morfPhoto can never modify your photos.

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
