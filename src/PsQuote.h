/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>

namespace photohub {

// Prepare une valeur pour l'inserer entre apostrophes dans un script PowerShell : une
// apostrophe se double. Dans une chaine '...', PowerShell n'interprete rien d'autre
// (ni $, ni backtick), ce doublement suffit donc a neutraliser toute injection.
inline QString psQuote(QString s) {
    return s.replace(QLatin1Char('\''), QStringLiteral("''"));
}

} // namespace photohub
