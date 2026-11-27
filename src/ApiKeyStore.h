/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>

namespace photohub {

// -----------------------------------------------------------------------------
// ApiKeyStore : cle d'API de chaque morfPhoto, rangee cote poste.
//
// Une cle par serveur (cle de rangement = URL de base). Sous Windows la cle est
// chiffree avec DPAPI : seul le compte Windows courant peut la relire, elle n'apparait
// donc jamais en clair dans le registre ni dans une sauvegarde. Ailleurs (Linux, macOS),
// repli en clair dans les preferences de l'utilisateur : PhotoHub vise Windows d'abord.
// -----------------------------------------------------------------------------
namespace ApiKeyStore {

// Cle memorisee pour ce serveur, ou chaine vide.
QString load(const QString& baseUrl);

// Memorise la cle ; une cle vide l'oublie.
void save(const QString& baseUrl, const QString& key);

} // namespace ApiKeyStore
} // namespace photohub
