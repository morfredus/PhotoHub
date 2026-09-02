/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QList>
#include <QString>

class QComboBox;
class QLabel;
class QTableWidget;
class QPushButton;
class QProgressBar;
class QTimer;
class QAction;

namespace photohub {

class BeaconDiscovery;
class MorfPhotoClient;

// -----------------------------------------------------------------------------
// MainWindow : l'interface de PhotoHub.
//
// Trois choses, alignees sur la mission : DECOUVRIR morfPhoto (par capacite),
// GERER les dossiers surveilles (declarer, activer, retirer), et SUIVRE
// l'indexation et l'etat de la phototheque. Rien de plus : PhotoHub reste un
// client, la source de verite est morfPhoto.
//
// Mappage de chemins
// ------------------
// morfPhoto peut tourner sur un hote different (ex. Raspberry Pi Linux) pendant
// que les photos resident sur la machine locale (Windows). Les dossiers sont
// partages en reseau : le serveur y accede via un montage (ex. /mnt/photos ->
// \\PC\Photos). m_pathMappings stocke ces correspondances localRoot <->
// serverRoot pour convertir un chemin Windows selectionne par l'utilisateur
// en chemin serveur avant de l'envoyer a l'API. Persiste dans QSettings.
// -----------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onSourcesChanged();
    void onSourceSelected(int index);
    void onSummary(const QJsonObject& summary);
    void onFolders(const QJsonArray& folders);
    void onIndexStatus(const QJsonObject& status);
    void onActionResult(bool ok, const QString& message);
    void onFailed(const QString& message);
    void onRoots(const QStringList& roots);

    void addFolderClicked();
    void toggleFolderClicked();
    void removeFolderClicked();
    void editMediaClicked();                 // Support amovible : drapeau + libellé de volume
    void toggleAnalyticsExclusionClicked();  // Exclure / réintégrer des analyses
    void showPurgeDialog();                  // Supprimer des données (définitif, sélectif ou total)
    void showRemovedFoldersDialog();         // Fichier > Dossiers retirés...
    void openAnalyticsClicked();
    void showContextDialog();                // Contextes photographiques (qualification)
    void showMappingsDialog();               // Fichier > Mappage de chemins...
    void showNetworkAccessDialog();          // Fichier > Assistant d'accès réseau...
    void checkForUpdates(bool manual);       // manual=true : menu ; false : démarrage silencieux
    void showAbout();

private:
    void buildUi();
    void buildMenu();
    int  selectedFolderId(bool* enabled = nullptr) const;
    // Objet JSON complet de la sélection courante (vide si aucune ligne choisie).
    QJsonObject selectedFolder() const;
    // Construit et exécute le dialogue de suppression, une fois années et boîtiers
    // récupérés auprès de morfPhoto (pour proposer des choix pré-remplis).
    void buildPurgeDialog(const QJsonObject& selectedFolder, const QJsonArray& years,
                          const QJsonArray& cameras);

    // Charge / sauvegarde les mappages depuis QSettings.
    void loadMappings();
    void saveMappings();

    // Traduit un chemin local (Windows) en chemin serveur (Linux) en appliquant
    // le premier mappage dont localRoot est un préfixe du chemin.
    // Retourne le chemin inchangé si aucun mappage ne correspond.
    QString applyPathMapping(const QString& localPath) const;

    // --- Assistant d'accès réseau (partage Windows + montage Pi) ---
    // Détections locales, pour retirer à l'utilisateur tout le travail de devinette
    // qui rend le montage SMB fragile (nom de compte ≠ dossier de profil, IP, etc.).
    static QString detectPcName();
    static QString detectLanIp();     // première IPv4 privée non-loopback, sinon ""
    static QString detectWindowsUser();
    static QString shareNameFor(const QString& localRoot);
    // Identite canonique de CE poste (hostname Windows -> slug Linux).
    static QString canonicalSourceSlug();
    static QString predictedMountpoint();

    BeaconDiscovery* m_discovery;
    MorfPhotoClient* m_client;
    QTimer*          m_refresh;

    QComboBox*    m_sourceCombo;
    QLabel*       m_connLabel;
    QLabel*       m_statFiles;
    QLabel*       m_statCameras;
    QLabel*       m_statLenses;
    QLabel*       m_statFolders;
    QLabel*       m_statMissing;
    QTableWidget* m_foldersTable;
    QPushButton*  m_addBtn;
    QPushButton*  m_toggleBtn;
    QPushButton*  m_removeBtn;
    QPushButton*  m_mediaBtn;         // Support amovible…
    QPushButton*  m_analyticsExclBtn; // Exclure / réintégrer des analyses
    QPushButton*  m_purgeBtn;         // Supprimer des données…
    QPushButton*  m_indexIncrBtn;
    QPushButton*  m_indexFullBtn;
    QPushButton*  m_contextsBtn;      // Contextes photographiques…
    QPushButton*  m_analyticsBtn;
    QLabel*       m_indexLabel;
    QProgressBar* m_progress;
    QLabel*       m_rootsLabel;
    QAction*      m_removedAction = nullptr;  // Fichier > Dossiers retirés (N)

    QJsonArray  m_folders;         // dossiers ACTIFS, aligne sur les lignes du tableau
    QJsonArray  m_removedFolders;  // dossiers retires (retrait doux), fenetre separee
    QStringList m_allowedRoots;    // racines autorisees (perimetre, cote morfPhoto)

    // Mappages chemin local <-> chemin serveur. first = localRoot, second = serverRoot.
    // Exemple : {"C:/Users/frede/Pictures", "/mnt/photos"}
    QList<QPair<QString, QString>> m_pathMappings;
};

} // namespace photohub
