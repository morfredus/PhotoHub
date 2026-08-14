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
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PhotoHub"));
    QApplication::setApplicationVersion(QStringLiteral(PHOTOHUB_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));

    photohub::MainWindow window;
    window.show();
    return app.exec();
}
