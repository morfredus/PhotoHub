/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "MorfPhotoClient.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>

namespace photohub {

namespace {
// Extrait un message lisible du corps d'erreur JSON { error, detail } de morfPhoto.
QString errorText(const QJsonDocument& doc, int status) {
    const QJsonObject o = doc.object();
    const QString detail = o.value(QStringLiteral("detail")).toString();
    const QString err    = o.value(QStringLiteral("error")).toString();
    if (!detail.isEmpty()) return detail;
    if (!err.isEmpty())    return err;
    return QStringLiteral("HTTP %1").arg(status);
}
} // namespace

MorfPhotoClient::MorfPhotoClient(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

void MorfPhotoClient::setBaseUrl(const QString& url) {
    m_base = url;
    while (m_base.endsWith('/'))
        m_base.chop(1);
}

void MorfPhotoClient::send(const QByteArray& verb, const QString& path,
                           const QByteArray& body, Handler handler) {
    if (m_base.isEmpty()) {
        emit failed(QStringLiteral("aucun morfPhoto sélectionné"));
        return;
    }
    QNetworkRequest req{QUrl(m_base + path)};
    req.setTransferTimeout(8000);
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_net->sendCustomRequest(req, verb, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        // Erreur de transport (hote injoignable, timeout) : pas de statut HTTP.
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            emit failed(reply->errorString());
            return;
        }
        if (handler)
            handler(status, doc);
    });
}

void MorfPhotoClient::refreshAll() {
    send("GET", QStringLiteral("/api/v1/photos/summary"), {}, [this](int s, const QJsonDocument& d) {
        if (s == 200) emit summaryReady(d.object());
    });
    send("GET", QStringLiteral("/api/v1/folders"), {}, [this](int s, const QJsonDocument& d) {
        if (s == 200) emit foldersReady(d.object().value(QStringLiteral("items")).toArray());
    });
    send("GET", QStringLiteral("/api/v1/roots"), {}, [this](int s, const QJsonDocument& d) {
        if (s != 200) return;
        QStringList roots;
        for (const QJsonValue& v : d.object().value(QStringLiteral("items")).toArray())
            roots << v.toString();
        emit rootsReady(roots);
    });
    send("GET", QStringLiteral("/api/v1/index/status"), {}, [this](int s, const QJsonDocument& d) {
        if (s == 200) emit indexStatusReady(d.object());
    });
}

void MorfPhotoClient::addFolder(const QString& path) {
    const QByteArray body = QJsonDocument(QJsonObject{{"path", path}}).toJson(QJsonDocument::Compact);
    send("POST", QStringLiteral("/api/v1/folders"), body, [this](int s, const QJsonDocument& d) {
        if (s == 201) { emit actionResult(true, QStringLiteral("Dossier ajouté.")); refreshAll(); }
        else          emit actionResult(false, errorText(d, s));
    });
}

void MorfPhotoClient::setFolderEnabled(int folderId, bool enabled) {
    const QByteArray body = QJsonDocument(QJsonObject{{"enabled", enabled}}).toJson(QJsonDocument::Compact);
    send("PATCH", QStringLiteral("/api/v1/folders/%1").arg(folderId), body,
         [this, enabled](int s, const QJsonDocument& d) {
        if (s == 200) { emit actionResult(true, enabled ? QStringLiteral("Dossier activé.")
                                                        : QStringLiteral("Dossier désactivé.")); refreshAll(); }
        else          emit actionResult(false, errorText(d, s));
    });
}

void MorfPhotoClient::removeFolder(int folderId) {
    send("DELETE", QStringLiteral("/api/v1/folders/%1").arg(folderId), {},
         [this](int s, const QJsonDocument& d) {
        if (s == 200) { emit actionResult(true, QStringLiteral("Dossier retiré (historique conservé).")); refreshAll(); }
        else          emit actionResult(false, errorText(d, s));
    });
}

void MorfPhotoClient::restoreFolder(int folderId) {
    send("POST", QStringLiteral("/api/v1/folders/%1/restore").arg(folderId), {},
         [this](int s, const QJsonDocument& d) {
        if (s == 200) { emit actionResult(true, QStringLiteral("Dossier restauré.")); refreshAll(); }
        else          emit actionResult(false, errorText(d, s));
    });
}

void MorfPhotoClient::triggerIndex(const QString& mode) {
    const QByteArray body = QJsonDocument(QJsonObject{{"mode", mode}}).toJson(QJsonDocument::Compact);
    send("POST", QStringLiteral("/api/v1/index"), body, [this](int s, const QJsonDocument& d) {
        if (s == 202)      emit actionResult(true, QStringLiteral("Indexation lancée."));
        else if (s == 409) emit actionResult(false, QStringLiteral("Une indexation est déjà en cours."));
        else               emit actionResult(false, errorText(d, s));
    });
}

} // namespace photohub
