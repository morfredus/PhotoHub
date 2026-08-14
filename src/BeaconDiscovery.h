/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QList>

class QUdpSocket;
class QTimer;

// -----------------------------------------------------------------------------
// BeaconDiscovery : ecoute les heartbeats morfBeacon (UDP 45454) et repere les
// services par leur CAPACITE, jamais par leur nom (le nom est renommable, la
// capacite non). PhotoHub cherche ainsi la capacite "photo_index" pour trouver
// morfPhoto sur le LAN, sans aucune adresse codee en dur.
//
// Cote reception : le protocole beacon est ANNONCE par une bibliotheque (cote
// service) ; l'ecoute se resume a lire des datagrammes JSON, comme le fait
// morfMonitor. On ne vendore donc rien : on lit le contrat documente.
// -----------------------------------------------------------------------------
namespace photohub {

struct ServiceInfo {
    QString     app;
    QString     instance;
    QString     host;
    QString     ip;            // adresse de l'emetteur (couche reseau), joignable
    quint16     port = 0;      // status_port annonce
    QString     version;
    QString     state;
    QStringList capabilities;
    qint64      lastSeen = 0;  // epoch s

    QString baseUrl() const { return QStringLiteral("http://%1:%2").arg(ip).arg(port); }
    QString label()   const { return QStringLiteral("%1 (%2)").arg(app, host.isEmpty() ? ip : host); }
};

class BeaconDiscovery : public QObject {
    Q_OBJECT
public:
    explicit BeaconDiscovery(QObject* parent = nullptr);

    // Demarre l'ecoute. false si le bind echoue (port occupe autrement).
    bool start();

    // Services entendus recemment qui annoncent la capacite demandee.
    QList<ServiceInfo> withCapability(const QString& capability) const;

signals:
    // Emis quand l'ensemble des services entendus change (nouveau, disparu, MAJ).
    void servicesChanged();

private:
    void onDatagram();
    void prune();

    QUdpSocket* m_socket = nullptr;
    QTimer*     m_prune  = nullptr;
    QHash<QString, ServiceInfo> m_services;   // clef = instance
};

} // namespace photohub
