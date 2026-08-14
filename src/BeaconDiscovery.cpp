/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "BeaconDiscovery.h"

#include <QUdpSocket>
#include <QTimer>
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QHostAddress>

namespace photohub {

namespace {
constexpr quint16 kBeaconPort = 45454;                 // port du parc morfSystem
constexpr int     kStaleSeconds = 35;                  // ~2 heartbeats manques (15 s)
const char* const kProto = "morfbeacon/1";
} // namespace

BeaconDiscovery::BeaconDiscovery(QObject* parent) : QObject(parent) {}

bool BeaconDiscovery::start() {
    m_socket = new QUdpSocket(this);
    // ShareAddress : d'autres consommateurs (morfMonitor, un autre PhotoHub) ecoutent
    // le meme port sur la meme machine. ReuseAddressHint pour rebinder proprement.
    if (!m_socket->bind(QHostAddress::AnyIPv4, kBeaconPort,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &BeaconDiscovery::onDatagram);

    m_prune = new QTimer(this);
    m_prune->setInterval(10000);
    connect(m_prune, &QTimer::timeout, this, &BeaconDiscovery::prune);
    m_prune->start();
    return true;
}

void BeaconDiscovery::onDatagram() {
    bool changed = false;
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_socket->receiveDatagram();
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        if (o.value(QStringLiteral("proto")).toString() != QLatin1String(kProto))
            continue;
        const QString app = o.value(QStringLiteral("app")).toString();
        if (app.isEmpty())
            continue;

        ServiceInfo s;
        s.app        = app;
        s.instance   = o.value(QStringLiteral("instance")).toString();
        s.host       = o.value(QStringLiteral("host")).toString();
        s.version    = o.value(QStringLiteral("version")).toString();
        s.state      = o.value(QStringLiteral("state")).toString();
        s.port       = static_cast<quint16>(o.value(QStringLiteral("status_port")).toInt());
        s.lastSeen   = QDateTime::currentSecsSinceEpoch();
        for (const QJsonValue& c : o.value(QStringLiteral("capabilities")).toArray())
            s.capabilities << c.toString();

        // L'adresse joignable vient de la couche reseau, pas du datagramme. Qt
        // prefixe les IPv4 mappees en IPv6 (« ::ffff:192.168.1.55 ») : a nettoyer.
        s.ip = dg.senderAddress().toString();
        if (s.ip.startsWith(QLatin1String("::ffff:")))
            s.ip = s.ip.mid(s.ip.lastIndexOf(QLatin1Char(':')) + 1);

        // Clef = identite d'INSTANCE (deux machines du meme service = deux entrees).
        const QString key = s.instance.isEmpty()
            ? app + QLatin1Char('@') + s.ip : s.instance;
        const bool isNew = !m_services.contains(key);
        m_services.insert(key, s);
        if (isNew)
            changed = true;   // une simple MAJ de lastSeen ne « change » pas la liste
    }
    if (changed)
        emit servicesChanged();
}

void BeaconDiscovery::prune() {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    bool changed = false;
    for (auto it = m_services.begin(); it != m_services.end();) {
        if (now - it->lastSeen > kStaleSeconds) { it = m_services.erase(it); changed = true; }
        else ++it;
    }
    if (changed)
        emit servicesChanged();
}

QList<ServiceInfo> BeaconDiscovery::withCapability(const QString& capability) const {
    QList<ServiceInfo> out;
    for (const ServiceInfo& s : m_services)
        if (s.port != 0 && s.capabilities.contains(capability))
            out.append(s);
    return out;
}

} // namespace photohub
