/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <QWizard>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <functional>
#include <memory>

namespace photohub {

class MorfPhotoClient;

// Libelle lisible d'une etape du rapport du helper de morfPhoto, et rapport complet
// mis en forme (une ligne par etape, ✓/✗). Partages avec le dialogue manuel.
QString stepLabel(const QString& id);
QString formatSourceReport(const QJsonObject& report);

// Conseil en francais clair pour un code d'erreur du helper (auth_failed,
// host_unreachable...). Vide si le code n'appelle aucun conseil particulier.
QString networkErrorAdvice(const QString& code);

// Ce que l'assistant recoit de la fenetre principale (il ne connait pas MainWindow).
struct NetworkAccessContext {
    MorfPhotoClient* client = nullptr;
    QString pcName;                  // nom de ce PC
    QString lanIp;                   // IPv4 LAN de ce PC (cible du montage cote serveur)
    QString winUser;                 // nom de compte Windows (pas le dossier de profil)
    QList<QPair<QString, QString>> mappings;                 // dossier local <-> chemin serveur
    std::function<QString(const QString&)> shareNameFor;     // nom de partage d'un dossier
    // Appelee quand le montage est confirme : a MainWindow de memoriser le mappage.
    std::function<void(const QString& localRoot, const QString& mountpoint)> onMounted;
};

// Assistant pas a pas : chaque page ne demande que ce dont elle a besoin et ne
// laisse passer a la suivante qu'une fois son action VERIFIEE (serveur pret,
// partage cree, identifiants acceptes puis partage monte et visible dans l'API).
class NetworkAccessWizard : public QWizard {
public:
    NetworkAccessWizard(const NetworkAccessContext& ctx, QWidget* parent = nullptr);

    // true si l'utilisateur a demande le mode manuel (les commandes a copier).
    bool manualRequested() const { return m_manual; }

private:
    bool m_manual = false;
};

} // namespace photohub
