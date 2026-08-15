/*
 * PhotoHub - application desktop de gestion de la phototheque.
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Client PUR de morfPhoto : decouvre le service par sa capacite morfBeacon
 * ("photo_index") et dialogue avec son API /api/v1. Ne lit jamais les fichiers,
 * ne lance jamais ExifTool, ne connait pas SQLite.
 */

#include <QApplication>
#include <QIcon>
#include <QJsonObject>

#include <morfbeacon/PresenceService.h>
#include <morfbeacon/IMetricsProvider.h>

#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PhotoHub"));
    QApplication::setApplicationVersion(QStringLiteral(PHOTOHUB_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));

    photohub::MainWindow window;
    window.show();

    // --- Annonce de presence sur le LAN (morfBeacon), comme ComponentHub et
    // SiteWatch. PhotoHub reste un CLIENT (il decouvre morfPhoto par capacite),
    // mais se rend lui-meme visible dans la carte de l'ecosysteme : heartbeat UDP
    // « je suis actif » + un /status interroge a la demande. L'application etant
    // lancee ponctuellement, elle apparait quand elle est ouverte et passe « hors
    // ligne » a sa fermeture, exactement comme les deux autres applications.
    morfbeacon::PresenceConfig beaconCfg;
    beaconCfg.appName      = QStringLiteral("PhotoHub");
    beaconCfg.version      = QStringLiteral(PHOTOHUB_VERSION);
    beaconCfg.statusPort   = 8882;   // appRange (cf. morfTools/ecosystem.json ; ComponentHub 8880, SiteWatch 8881)
    beaconCfg.capabilities = {QStringLiteral("photo_client")};

    // PhotoHub n'heberge pas de donnees : les metriques restent minimales, juste de
    // quoi affirmer son role. Relues a la demande sur le thread Qt (frequence faible).
    morfbeacon::FunctionMetricsProvider beaconMetrics([]() {
        QJsonObject m;
        m["role"] = QStringLiteral("client");
        return m;
    });

    morfbeacon::PresenceService presence(beaconCfg, &beaconMetrics);
    presence.start();

    return app.exec();
}
