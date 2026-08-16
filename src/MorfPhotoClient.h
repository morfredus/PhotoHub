/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QVariant>
#include <functional>

class QNetworkAccessManager;

// -----------------------------------------------------------------------------
// MorfPhotoClient : dialogue avec l'API HTTP /api/v1 de morfPhoto.
//
// PhotoHub est un client PUR : il ne lit jamais les fichiers, ne lance jamais
// ExifTool, ne connait pas SQLite. Tout passe par ce client, en asynchrone
// (Qt Network), pour ne jamais figer l'interface. morfPhoto reste la source de
// verite ; PhotoHub declare des dossiers, lance une indexation, suit son etat.
// -----------------------------------------------------------------------------
namespace photohub {

class MorfPhotoClient : public QObject {
    Q_OBJECT
public:
    explicit MorfPhotoClient(QObject* parent = nullptr);

    void setBaseUrl(const QString& url);
    QString baseUrl() const { return m_base; }
    bool hasBase() const { return !m_base.isEmpty(); }

    void refreshAll();                                 // resume + dossiers + etat d'index
    // Ajoute une selection. `removable` : support amovible (CD/DVD, archive) dont
    // l'absence ne vaudra jamais suppression ; `volumeLabel` : nom du support (vide = aucun).
    void addFolder(const QString& path, bool removable = false, const QString& volumeLabel = {});
    void setFolderEnabled(int folderId, bool enabled);
    // Regle le support amovible d'une selection (drapeau + libelle de volume).
    void setFolderMedia(int folderId, bool removable, const QString& volumeLabel);
    // Sort/reintegre une selection des analyses sans effacer ses donnees.
    void setFolderAnalyticsExcluded(int folderId, bool excluded);
    void removeFolder(int folderId);
    void restoreFolder(int folderId);                  // annule un retrait doux
    void triggerIndex(const QString& mode);            // "incremental" | "full"

    // Purge DEFINITIVE (irreversible) selon une portee : scope = "folder" (value =
    // id), "year" (value = annee), "camera" (value = nom), "all" (value ignoree).
    void purge(const QString& scope, const QVariant& value);
    // Recuperations ponctuelles pour peupler le dialogue de suppression selective.
    void fetchYears(std::function<void(const QJsonArray&)> cb);
    void fetchCameras(std::function<void(const QJsonArray&)> cb);

signals:
    void summaryReady(const QJsonObject& summary);
    void foldersReady(const QJsonArray& folders);
    void rootsReady(const QStringList& roots);
    void indexStatusReady(const QJsonObject& status);
    void actionResult(bool ok, const QString& message);  // retour d'une action
    void failed(const QString& message);                 // erreur reseau/HTTP

private:
    using Handler = std::function<void(int, const QJsonDocument&)>;
    void send(const QByteArray& verb, const QString& path, const QByteArray& body, Handler handler);

    QNetworkAccessManager* m_net;
    QString                m_base;
};

} // namespace photohub
