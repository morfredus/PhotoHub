/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ApiKeyStore.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QSettings>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wincrypt.h>
#endif

namespace photohub {
namespace ApiKeyStore {

namespace {

// Cle de rangement : l'URL contient des « / » et « : » que QSettings interpreterait
// comme des sous-groupes. Une empreinte donne un nom plat et stable.
QString slot(const QString& baseUrl) {
    return QStringLiteral("apiKeys/")
         + QString::fromLatin1(QCryptographicHash::hash(baseUrl.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QSettings settings() {
    return QSettings(QStringLiteral("morfredus"), QStringLiteral("PhotoHub"));
}

#ifdef Q_OS_WIN
// Chiffre/dechiffre avec DPAPI (lie au compte Windows courant). Vide si l'appel echoue.
QByteArray dpapi(const QByteArray& in, bool protect) {
    DATA_BLOB src{static_cast<DWORD>(in.size()),
                  reinterpret_cast<BYTE*>(const_cast<char*>(in.constData()))};
    DATA_BLOB out{0, nullptr};
    const BOOL ok = protect
        ? CryptProtectData(&src, L"PhotoHub", nullptr, nullptr, nullptr, 0, &out)
        : CryptUnprotectData(&src, nullptr, nullptr, nullptr, nullptr, 0, &out);
    if (!ok)
        return {};
    const QByteArray result(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return result;
}
#endif

} // namespace

QString load(const QString& baseUrl) {
    if (baseUrl.isEmpty())
        return {};
    const QString stored = settings().value(slot(baseUrl)).toString();
#ifdef Q_OS_WIN
    if (stored.startsWith(QLatin1String("dpapi:"))) {
        const QByteArray raw = dpapi(QByteArray::fromBase64(stored.mid(6).toLatin1()), false);
        return QString::fromUtf8(raw);
    }
#endif
    if (stored.startsWith(QLatin1String("plain:")))
        return stored.mid(6);
    return {};
}

void save(const QString& baseUrl, const QString& key) {
    if (baseUrl.isEmpty())
        return;
    QSettings s = settings();
    if (key.isEmpty()) {
        s.remove(slot(baseUrl));
        return;
    }
#ifdef Q_OS_WIN
    const QByteArray blob = dpapi(key.toUtf8(), true);
    if (!blob.isEmpty()) {
        s.setValue(slot(baseUrl), QStringLiteral("dpapi:") + QString::fromLatin1(blob.toBase64()));
        return;
    }
    // DPAPI indisponible (cas tres rare) : ne pas memoriser plutot qu'ecrire en clair.
    s.remove(slot(baseUrl));
#else
    s.setValue(slot(baseUrl), QStringLiteral("plain:") + key);
#endif
}

} // namespace ApiKeyStore
} // namespace photohub
