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
#include <QJsonArray>
#include <QStringList>
#include <QUrl>
#include <QTimer>
#include <memory>

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
                           const QByteArray& body, Handler handler, int timeoutMs, bool quiet) {
    if (m_base.isEmpty()) {
        if (!quiet)
            emit failed(QStringLiteral("aucun morfPhoto sélectionné"));
        if (handler) handler(0, {});
        return;
    }
    QNetworkRequest req{QUrl(m_base + path)};
    req.setTransferTimeout(timeoutMs);
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_net->sendCustomRequest(req, verb, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler, quiet]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            if (!quiet)
                emit failed(reply->errorString());
            if (handler)
                handler(0, {});
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

void MorfPhotoClient::addFolder(const QString& path, bool removable, const QString& volumeLabel) {
    QJsonObject in{{"path", path}};
    if (removable)
        in["removable"] = true;
    if (!volumeLabel.isEmpty())
        in["volume_label"] = volumeLabel;
    const QByteArray body = QJsonDocument(in).toJson(QJsonDocument::Compact);
    send("POST", QStringLiteral("/api/v1/folders"), body, [this](int s, const QJsonDocument& d) {
        if (s == 201) { emit actionResult(true, QStringLiteral("Dossier ajouté.")); refreshAll(); }
        else          emit actionResult(false, errorText(d, s));
    });
}

void MorfPhotoClient::checkSourcesReady(std::function<void(bool, const QJsonObject&)> cb) {
    send("GET", QStringLiteral("/api/v1/sources/ready"), {}, [this, cb](int s, const QJsonDocument& d) {
        QJsonObject report = d.object();
        const bool ok = (s == 200) && report.value(QStringLiteral("ok")).toBool();
        if (!ok && !report.contains(QStringLiteral("detail")))
            report[QStringLiteral("detail")] = errorText(d, s);
        if (cb) cb(ok, report);
    }, 15000);
}

void MorfPhotoClient::pushSource(const QString& host, const QString& share, const QString& username,
                                 const QString& password, const QString& hostname, bool writable,
                                 std::function<void(bool, const QJsonObject&)> cb) {
    QJsonObject in{{"host", host}, {"share", share},
                   {"username", username}, {"password", password},
                   {"hostname", hostname}, {"writable", writable}};
    const QByteArray body = QJsonDocument(in).toJson(QJsonDocument::Compact);
    // Montage CIFS + fstab + JSON : largement au-dela du timeout HTTP usuel.
    send("POST", QStringLiteral("/api/v1/sources"), body, [this, cb](int s, const QJsonDocument& d) {
        QJsonObject report = d.object();
        if (s == 201) {
            const QString mp = report.value(QStringLiteral("mountpoint")).toString();
            emit actionResult(true, QStringLiteral("Source montée sur le serveur : %1").arg(mp));
            if (cb) cb(true, report);
        } else {
            if (!report.contains(QStringLiteral("detail")))
                report[QStringLiteral("detail")] = errorText(d, s);
            emit actionResult(false, errorText(d, s));
            if (cb) cb(false, report);
        }
    }, 120000);
}

void MorfPhotoClient::confirmSourceRoot(const QString& mountpoint, bool waitRestart,
                                        std::function<void(bool, const QJsonObject&)> cb) {
    const auto check = [this, mountpoint, cb]() {
        send("GET", QStringLiteral("/status"), {}, [this, mountpoint, cb](int s, const QJsonDocument& d) {
            const QString state = d.object().value(QStringLiteral("state")).toString();
            const bool statusOk = (s == 200) && (state.isEmpty() || state == QLatin1String("ok")
                || state == QLatin1String("idle") || state == QLatin1String("indexing"));
            send("GET", QStringLiteral("/api/v1/roots"), {},
                 [this, mountpoint, cb, statusOk, s](int s2, const QJsonDocument& d2) {
                bool found = false;
                for (const QJsonValue& v : d2.object().value(QStringLiteral("items")).toArray()) {
                    if (v.toString() == mountpoint)
                        found = true;
                }
                QJsonObject extra;
                extra[QStringLiteral("service_active")] = statusOk;
                extra[QStringLiteral("status_http")]    = s;
                extra[QStringLiteral("roots_http")]     = s2;
                extra[QStringLiteral("root_in_api")]    = found;
                extra[QStringLiteral("mountpoint")]     = mountpoint;
                if (statusOk && found)
                    refreshAll();
                if (cb) cb(statusOk && found, extra);
            }, 8000, true);
        }, 8000, true);
    };

    if (!waitRestart) {
        check();
        return;
    }

    // Le service vient d'etre relance : attendre /healthz jusqu'a ~45 s.
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    const auto tries = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [this, timer, tries, check]() {
        send("GET", QStringLiteral("/healthz"), {}, [timer, tries, check](int s, const QJsonDocument&) {
            if (s == 200) {
                timer->stop();
                timer->deleteLater();
                check();
                return;
            }
            if (++(*tries) >= 45) {
                timer->stop();
                timer->deleteLater();
                check();
            }
        }, 3000, true);
    });
    timer->start();
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

void MorfPhotoClient::setFolderMedia(int folderId, bool removable, const QString& volumeLabel) {
    // volume_label explicitement null si vide : efface un ancien libellé côté base.
    QJsonObject in{{"removable", removable}};
    in["volume_label"] = volumeLabel.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(volumeLabel);
    const QByteArray body = QJsonDocument(in).toJson(QJsonDocument::Compact);
    send("PATCH", QStringLiteral("/api/v1/folders/%1").arg(folderId), body,
         [this](int s, const QJsonDocument& d) {
        if (s == 200) { emit actionResult(true, QStringLiteral("Support mis à jour.")); refreshAll(); }
        else          emit actionResult(false, errorText(d, s));
    });
}

void MorfPhotoClient::setFolderAnalyticsExcluded(int folderId, bool excluded) {
    const QByteArray body = QJsonDocument(QJsonObject{{"analytics_excluded", excluded}}).toJson(QJsonDocument::Compact);
    send("PATCH", QStringLiteral("/api/v1/folders/%1").arg(folderId), body,
         [this, excluded](int s, const QJsonDocument& d) {
        if (s == 200) { emit actionResult(true, excluded ? QStringLiteral("Dossier exclu des analyses (données conservées).")
                                                         : QStringLiteral("Dossier réintégré aux analyses.")); refreshAll(); }
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

void MorfPhotoClient::reindexAndWait(const QString& mode,
                                    std::function<void(bool, const QString&)> cb) {
    // Étape 1 : mémoriser l'id de la dernière passe. Une passe crée TOUJOURS une ligne
    // (même sans nouveau fichier) : voir cet id augmenter = la passe qu'on lance est
    // finie. Signal fiable, contrairement à l'état seul (course au démarrage du worker).
    send("GET", QStringLiteral("/api/v1/index/status"), {}, [this, mode, cb](int s, const QJsonDocument& d) {
        const int baseline = (s == 200)
            ? d.object().value(QStringLiteral("last_run")).toObject()
                  .value(QStringLiteral("id")).toInt(-1)
            : -1;

        // Étape 2 : déclencher. 202 = accepté ; 409 = une passe tourne déjà (on attend
        // quand même sa fin pour rafraîchir). Tout autre code = échec immédiat.
        const QByteArray body = QJsonDocument(QJsonObject{{"mode", mode}}).toJson(QJsonDocument::Compact);
        send("POST", QStringLiteral("/api/v1/index"), body, [this, baseline, cb](int s2, const QJsonDocument& d2) {
            if (s2 != 202 && s2 != 409) {
                if (cb) cb(false, errorText(d2, s2));
                return;
            }

            // Étape 3 : sonder l'état jusqu'à « idle » ET un id de passe supérieur au
            // repère. Garde-fou ~10 min (source SMB lente) pour ne jamais rester bloqué.
            auto* timer = new QTimer(this);
            timer->setInterval(1500);
            const auto tries = std::make_shared<int>(0);
            connect(timer, &QTimer::timeout, this, [this, timer, tries, baseline, cb]() {
                send("GET", QStringLiteral("/api/v1/index/status"), {},
                     [timer, tries, baseline, cb](int s3, const QJsonDocument& d3) {
                    if (s3 == 200) {
                        const QJsonObject o = d3.object();
                        const QString state = o.value(QStringLiteral("state")).toString();
                        const int lastId = o.value(QStringLiteral("last_run")).toObject()
                                               .value(QStringLiteral("id")).toInt(-1);
                        if (state == QLatin1String("idle") && lastId > baseline) {
                            timer->stop();
                            timer->deleteLater();
                            if (cb) cb(true, o.value(QStringLiteral("last_error")).toString());
                            return;
                        }
                    }
                    if (++(*tries) >= 400) {   // 1500 ms * 400 ≈ 10 min
                        timer->stop();
                        timer->deleteLater();
                        if (cb) cb(false, QStringLiteral("délai d'indexation dépassé"));
                    }
                }, 8000, true);
            });
            timer->start();
        });
    });
}

void MorfPhotoClient::purge(const QString& scope, const QVariant& value) {
    QJsonObject in{{"scope", scope}};
    if (value.isValid() && !value.isNull())
        in["value"] = QJsonValue::fromVariant(value);
    const QByteArray body = QJsonDocument(in).toJson(QJsonDocument::Compact);
    send("POST", QStringLiteral("/api/v1/purge"), body, [this](int s, const QJsonDocument& d) {
        if (s == 200) {
            const int n = d.object().value(QStringLiteral("deleted")).toInt();
            emit actionResult(true, QStringLiteral("Suppression effectuée : %1 photo(s) retirée(s) définitivement.").arg(n));
            refreshAll();
        } else {
            emit actionResult(false, errorText(d, s));
        }
    });
}

void MorfPhotoClient::fetchYears(std::function<void(const QJsonArray&)> cb) {
    send("GET", QStringLiteral("/api/v1/photos/years"), {}, [cb](int s, const QJsonDocument& d) {
        if (cb) cb(s == 200 ? d.object().value(QStringLiteral("items")).toArray() : QJsonArray{});
    });
}

void MorfPhotoClient::fetchCameras(std::function<void(const QJsonArray&)> cb) {
    send("GET", QStringLiteral("/api/v1/photos/cameras"), {}, [cb](int s, const QJsonDocument& d) {
        if (cb) cb(s == 200 ? d.object().value(QStringLiteral("items")).toArray() : QJsonArray{});
    });
}

void MorfPhotoClient::fetchContexts(const QString& status, std::function<void(const QJsonArray&)> cb) {
    QString path = QStringLiteral("/api/v1/contexts");
    if (!status.isEmpty())
        path += QStringLiteral("?status=") + status;
    send("GET", path, {}, [cb](int s, const QJsonDocument& d) {
        if (cb) cb(s == 200 ? d.object().value(QStringLiteral("items")).toArray() : QJsonArray{});
    });
}

void MorfPhotoClient::putContext(const QString& directory, const QString& context,
                                 const QString& subject, const QString& motif,
                                 const QString& description,
                                 std::function<void(bool, const QJsonObject&, const QString&)> cb) {
    QJsonObject in{{"directory", directory}, {"context", context}, {"subject", subject}};
    if (!motif.isEmpty())       in["motif"] = motif;
    if (!description.isEmpty())  in["description"] = description;
    const QByteArray body = QJsonDocument(in).toJson(QJsonDocument::Compact);
    send("PUT", QStringLiteral("/api/v1/context"), body, [cb](int s, const QJsonDocument& d) {
        if (s == 200) { if (cb) cb(true, d.object(), QString()); }
        else          { if (cb) cb(false, QJsonObject{}, errorText(d, s)); }
    });
}

void MorfPhotoClient::fetchDirectorySample(const QString& directory, int limit,
                                           std::function<void(const QStringList&)> cb) {
    // ATTENTION : ne PAS construire cette URL avec des `.arg()` chaînés. Le répertoire
    // encodé contient des `%2F` (les `/`), `%20`, `%2C`... et QString::arg lit `%2F`
    // comme le marqueur `%2` suivi d'un `F`. Un second `.arg(limit)` réécrivait alors
    // tous ces `%2` (ex. `%2Fmnt` -> `6Fmnt`), corrompant le chemin : le serveur ne
    // trouvait aucune photo et l'aperçu restait vide. Concaténation simple = pas de
    // re-balayage des marqueurs, l'encodage du chemin est préservé tel quel.
    const QString path = QStringLiteral("/api/v1/photos?directory=")
        + QString::fromUtf8(QUrl::toPercentEncoding(directory))
        + QStringLiteral("&page_size=") + QString::number(limit);
    send("GET", path, {}, [cb](int s, const QJsonDocument& d) {
        QStringList paths;
        if (s == 200)
            for (const QJsonValue& v : d.object().value(QStringLiteral("items")).toArray())
                paths << v.toObject().value(QStringLiteral("path")).toString();
        if (cb) cb(paths);
    });
}

void MorfPhotoClient::fetchThumbnail(const QString& path,
                                     std::function<void(const QByteArray&)> cb) {
    // Reponse BINAIRE (image/jpeg) : on n'utilise pas send() (qui parse du JSON), mais
    // un GET direct dont on lit les octets bruts. 404 (pas de vignette) => octets vides.
    if (m_base.isEmpty()) { if (cb) cb(QByteArray{}); return; }
    const QString url = m_base + QStringLiteral("/api/v1/thumbnail?path=")
        + QString::fromUtf8(QUrl::toPercentEncoding(path));
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray bytes;
        if (status == 200)
            bytes = reply->readAll();
        if (cb) cb(bytes);
    });
}

} // namespace photohub
