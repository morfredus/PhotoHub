/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "MainWindow.h"
#include "BeaconDiscovery.h"
#include "MorfPhotoClient.h"

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QProgressBar>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QDialog>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QProcess>
#include <QClipboard>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QFile>
#include <QRegularExpression>

#include <morfupdate/UpdateDialog.h>
#include <morfupdate/morfUpdateConfig.h>

namespace photohub {

namespace {
constexpr const char* kCapPhoto     = "photo_index";
constexpr const char* kCapAnalytics = "photo_analytics";

QString folderState(const QJsonObject& f) {
    if (f.value(QStringLiteral("enabled")).toInt() == 1)
        return QStringLiteral("Actif");
    if (f.value(QStringLiteral("auto_disabled")).toInt() == 1)
        return QStringLiteral("Inactif (racine absente)");
    return QStringLiteral("Inactif");
}
} // namespace

// =============================================================================
// Construction
// =============================================================================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_discovery(new BeaconDiscovery(this)),
      m_client(new MorfPhotoClient(this)),
      m_refresh(new QTimer(this)) {
    setWindowTitle(QStringLiteral("PhotoHub %1").arg(QStringLiteral(PHOTOHUB_VERSION)));
    resize(820, 640);
    buildMenu();
    buildUi();

    loadMappings();

    connect(m_discovery, &BeaconDiscovery::servicesChanged, this, &MainWindow::onSourcesChanged);
    connect(m_client, &MorfPhotoClient::summaryReady, this, &MainWindow::onSummary);
    connect(m_client, &MorfPhotoClient::foldersReady, this, &MainWindow::onFolders);
    connect(m_client, &MorfPhotoClient::rootsReady, this, &MainWindow::onRoots);
    connect(m_client, &MorfPhotoClient::indexStatusReady, this, &MainWindow::onIndexStatus);
    connect(m_client, &MorfPhotoClient::actionResult, this, &MainWindow::onActionResult);
    connect(m_client, &MorfPhotoClient::failed, this, &MainWindow::onFailed);

    // Vérification de mise à jour au démarrage, silencieuse (comme SiteWatch /
    // ComponentHub) : laisser l'interface s'afficher d'abord.
    QTimer::singleShot(2000, this, [this]() { checkForUpdates(false); });

    // Rafraichissement periodique : stats et progression d'indexation en direct.
    m_refresh->setInterval(3000);
    connect(m_refresh, &QTimer::timeout, this, [this]() {
        if (m_client->hasBase()) m_client->refreshAll();
    });
    m_refresh->start();

    if (!m_discovery->start())
        statusBar()->showMessage(QStringLiteral("Écoute morfBeacon impossible (port 45454 occupé ?)."));
    onSourcesChanged();
}

// =============================================================================
// Interface
// =============================================================================

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // --- Source (decouverte) ---
    auto* srcRow = new QHBoxLayout;
    srcRow->addWidget(new QLabel(QStringLiteral("morfPhoto :")));
    m_sourceCombo = new QComboBox;
    m_sourceCombo->setMinimumWidth(280);
    srcRow->addWidget(m_sourceCombo);
    m_connLabel = new QLabel;
    srcRow->addWidget(m_connLabel, 1);
    m_analyticsBtn = new QPushButton(QStringLiteral("Analyses avancées…"));
    m_analyticsBtn->setVisible(false);
    srcRow->addWidget(m_analyticsBtn);
    root->addLayout(srcRow);
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSourceSelected);
    connect(m_analyticsBtn, &QPushButton::clicked, this, &MainWindow::openAnalyticsClicked);

    // --- Statistiques ---
    auto* stats = new QGroupBox(QStringLiteral("Photothèque"));
    auto* statsRow = new QHBoxLayout(stats);
    const auto makeStat = [&statsRow](const QString& caption, QLabel*& value) {
        auto* box = new QVBoxLayout;
        value = new QLabel(QStringLiteral("—"));
        auto f = value->font(); f.setPointSize(f.pointSize() + 6); f.setBold(true);
        value->setFont(f);
        box->addWidget(value);
        box->addWidget(new QLabel(caption));
        statsRow->addLayout(box);
    };
    makeStat(QStringLiteral("Photos"), m_statFiles);
    makeStat(QStringLiteral("Boîtiers"), m_statCameras);
    makeStat(QStringLiteral("Objectifs"), m_statLenses);
    makeStat(QStringLiteral("Dossiers actifs"), m_statFolders);
    makeStat(QStringLiteral("Disparues"), m_statMissing);
    statsRow->addStretch(1);
    root->addWidget(stats);

    // --- Dossiers ---
    auto* folders = new QGroupBox(QStringLiteral("Dossiers surveillés"));
    auto* fLayout = new QVBoxLayout(folders);
    m_foldersTable = new QTableWidget(0, 2);
    m_foldersTable->setHorizontalHeaderLabels({QStringLiteral("Dossier"), QStringLiteral("État")});
    m_foldersTable->horizontalHeader()->setStretchLastSection(false);
    m_foldersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_foldersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_foldersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_foldersTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_foldersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fLayout->addWidget(m_foldersTable);
    auto* fBtns = new QHBoxLayout;
    m_addBtn    = new QPushButton(QStringLiteral("Ajouter un dossier…"));
    m_toggleBtn = new QPushButton(QStringLiteral("Activer / Désactiver"));
    m_removeBtn = new QPushButton(QStringLiteral("Retirer"));
    fBtns->addWidget(m_addBtn);
    fBtns->addWidget(m_toggleBtn);
    fBtns->addWidget(m_removeBtn);
    fBtns->addStretch(1);
    fLayout->addLayout(fBtns);
    m_rootsLabel = new QLabel(QStringLiteral("Racines autorisées : —"));
    m_rootsLabel->setWordWrap(true);
    m_rootsLabel->setStyleSheet(QStringLiteral("color:#99a1ad"));
    fLayout->addWidget(m_rootsLabel);
    root->addWidget(folders, 1);
    connect(m_addBtn, &QPushButton::clicked, this, &MainWindow::addFolderClicked);
    connect(m_toggleBtn, &QPushButton::clicked, this, &MainWindow::toggleFolderClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &MainWindow::removeFolderClicked);

    // --- Indexation ---
    auto* index = new QGroupBox(QStringLiteral("Indexation"));
    auto* iLayout = new QVBoxLayout(index);
    auto* iBtns = new QHBoxLayout;
    m_indexIncrBtn = new QPushButton(QStringLiteral("Indexer (incrémental)"));
    m_indexFullBtn = new QPushButton(QStringLiteral("Réindexer (complet)"));
    iBtns->addWidget(m_indexIncrBtn);
    iBtns->addWidget(m_indexFullBtn);
    iBtns->addStretch(1);
    iLayout->addLayout(iBtns);
    m_indexLabel = new QLabel(QStringLiteral("—"));
    iLayout->addWidget(m_indexLabel);
    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    iLayout->addWidget(m_progress);
    root->addWidget(index);
    connect(m_indexIncrBtn, &QPushButton::clicked, this, [this]() { m_client->triggerIndex(QStringLiteral("incremental")); });
    connect(m_indexFullBtn, &QPushButton::clicked, this, [this]() { m_client->triggerIndex(QStringLiteral("full")); });

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Recherche de morfPhoto sur le réseau…"));
}

void MainWindow::buildMenu() {
    QMenu* fichier = menuBar()->addMenu(QStringLiteral("Fichier"));
    QAction* netAssist = fichier->addAction(QStringLiteral("Assistant d'accès réseau…"));
    connect(netAssist, &QAction::triggered, this, &MainWindow::showNetworkAccessDialog);
    QAction* mappings = fichier->addAction(QStringLiteral("Mappage de chemins…"));
    connect(mappings, &QAction::triggered, this, &MainWindow::showMappingsDialog);
    m_removedAction = fichier->addAction(QStringLiteral("Dossiers retirés…"));
    m_removedAction->setEnabled(false);   // activé dès qu'un dossier retiré existe
    connect(m_removedAction, &QAction::triggered, this, &MainWindow::showRemovedFoldersDialog);
    fichier->addSeparator();
    QAction* quitter = fichier->addAction(QStringLiteral("Quitter"));
    quitter->setShortcut(QKeySequence::Quit);
    connect(quitter, &QAction::triggered, qApp, &QApplication::quit);

    QMenu* aide = menuBar()->addMenu(QStringLiteral("Aide"));
    QAction* maj = aide->addAction(QStringLiteral("Rechercher les mises à jour…"));
    connect(maj, &QAction::triggered, this, [this]() { checkForUpdates(true); });
    aide->addSeparator();
    QAction* apropos = aide->addAction(QStringLiteral("À propos de PhotoHub"));
    connect(apropos, &QAction::triggered, this, &MainWindow::showAbout);
}

// =============================================================================
// Mappage de chemins (persistance QSettings)
// =============================================================================

void MainWindow::loadMappings() {
    QSettings s(QStringLiteral("morfredus"), QStringLiteral("PhotoHub"));
    const int n = s.beginReadArray(QStringLiteral("pathMappings"));
    m_pathMappings.clear();
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        const QString local  = s.value(QStringLiteral("local")).toString();
        const QString server = s.value(QStringLiteral("server")).toString();
        if (!local.isEmpty() && !server.isEmpty())
            m_pathMappings.append({local, server});
    }
    s.endArray();
}

void MainWindow::saveMappings() {
    QSettings s(QStringLiteral("morfredus"), QStringLiteral("PhotoHub"));
    s.beginWriteArray(QStringLiteral("pathMappings"), m_pathMappings.size());
    for (int i = 0; i < m_pathMappings.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("local"),  m_pathMappings[i].first);
        s.setValue(QStringLiteral("server"), m_pathMappings[i].second);
    }
    s.endArray();
}

// Traduit un chemin local Windows en chemin serveur Linux.
// Normalise les séparateurs (tous en '/') et compare sans tenir compte de la casse
// pour être robuste sur Windows (insensible à la casse) et les montages réseau.
// Exemple : "C:/Users/frede/Pictures/Photos/2016" + mappage
//           {"C:/Users/frede/Pictures" -> "/mnt/photos"}
//           => "/mnt/photos/Photos/2016"
QString MainWindow::applyPathMapping(const QString& localPath) const {
    const QString normLocal = QDir::fromNativeSeparators(localPath);
    for (const auto& [localRoot, serverRoot] : m_pathMappings) {
        const QString normRoot = QDir::fromNativeSeparators(localRoot);
        // Comparaison insensible à la casse (Windows).
        if (normLocal.toLower().startsWith(normRoot.toLower())) {
            // On remplace le préfixe localRoot par serverRoot.
            const QString tail = normLocal.mid(normRoot.length());
            // S'assurer qu'on a un '/' de séparation correct.
            if (tail.isEmpty())
                return serverRoot;
            if (tail.startsWith('/'))
                return serverRoot + tail;
            return serverRoot + '/' + tail;
        }
    }
    // Aucun mappage : on retourne le chemin avec séparateurs Unix au cas où
    // morfPhoto tourne localement (même machine, ex. WSL).
    return QDir::fromNativeSeparators(localPath);
}

// =============================================================================
// Dialogue de configuration des mappages
// =============================================================================

void MainWindow::showMappingsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Mappage de chemins – PhotoHub"));
    dlg.resize(620, 320);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 16, 16, 16);

    // Explication.
    auto* info = new QLabel(
        QStringLiteral(
            "morfPhoto (Linux) accède aux photos via un montage réseau Samba/SMB <b>en lecture seule</b>.<br>"
            "Les fichiers restent sur Windows et ne sont jamais modifiés par morfPhoto.<br><br>"
            "Déclarez ici la correspondance entre votre dossier Windows local et son point de montage Linux.<br>"
            "Exemple : <b>C:\\Users\\frede\\Pictures</b> → <b>/mnt/photos</b>"));
    info->setWordWrap(true);
    layout->addWidget(info);

    // Tableau (chemin local | chemin serveur).
    auto* table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({
        QStringLiteral("Chemin local (Windows)"),
        QStringLiteral("Chemin serveur (Linux)")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    // Charger les mappages existants.
    for (const auto& [loc, srv] : m_pathMappings) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(loc));
        table->setItem(row, 1, new QTableWidgetItem(srv));
    }
    layout->addWidget(table, 1);

    // Boutons Ajouter / Supprimer.
    auto* editBtns = new QHBoxLayout;
    auto* addRowBtn = new QPushButton(QStringLiteral("Ajouter une ligne"));
    auto* delRowBtn = new QPushButton(QStringLiteral("Supprimer la ligne"));
    editBtns->addWidget(addRowBtn);
    editBtns->addWidget(delRowBtn);
    editBtns->addStretch(1);
    layout->addLayout(editBtns);

    connect(addRowBtn, &QPushButton::clicked, &dlg, [table]() {
        const int row = table->rowCount();
        table->insertRow(row);
        // Pré-remplir avec le dossier Images de l'utilisateur courant.
        const QString pictures = QDir::toNativeSeparators(
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
        table->setItem(row, 0, new QTableWidgetItem(pictures));
        table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("/mnt/photos")));
        table->setCurrentCell(row, 0);
        table->editItem(table->item(row, 0));
    });

    connect(delRowBtn, &QPushButton::clicked, &dlg, [table]() {
        const int row = table->currentRow();
        if (row >= 0) table->removeRow(row);
    });

    // OK / Annuler.
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;

    // Relire le tableau et sauvegarder.
    m_pathMappings.clear();
    for (int i = 0; i < table->rowCount(); ++i) {
        const QString loc = table->item(i, 0) ? table->item(i, 0)->text().trimmed() : QString();
        const QString srv = table->item(i, 1) ? table->item(i, 1)->text().trimmed() : QString();
        if (!loc.isEmpty() && !srv.isEmpty())
            m_pathMappings.append({loc, srv});
    }
    saveMappings();
    statusBar()->showMessage(QStringLiteral("Mappage de chemins mis à jour."), 3000);
}

// =============================================================================
// Assistant d'accès réseau
//
// Le montage SMB est le point le plus fragile de l'installation : un utilisateur
// non spécialiste bute sur le nom de compte Windows (souvent différent du dossier
// de profil), l'IP de la machine, la syntaxe de `mount`/`fstab`. PhotoHub tourne
// justement sur le PC où sont les photos : il connaît le chemin local, détecte le
// nom du PC, l'IP LAN et le compte Windows, crée le partage en lecture seule et
// génère les commandes du Pi déjà remplies. L'utilisateur n'a plus qu'à coller.
// =============================================================================

QString MainWindow::detectPcName() {
    const QString h = QHostInfo::localHostName();
    return h.isEmpty() ? QStringLiteral("MON-PC") : h;
}

QString MainWindow::detectLanIp() {
    // Première IPv4 privée non-loopback d'une interface active. On privilégie les
    // plages RFC 1918 (192.168/10/172.16-31) pour ignorer le virtuel (WSL, Docker…
    // qui restent possibles, mais on préfère une adresse de LAN réelle).
    const auto isPrivate = [](const QString& ip) {
        if (ip.startsWith(QStringLiteral("192.168."))) return true;
        if (ip.startsWith(QStringLiteral("10.")))      return true;
        const QRegularExpression r172(QStringLiteral("^172\\.(1[6-9]|2\\d|3[0-1])\\."));
        return r172.match(ip).hasMatch();
    };
    QString fallback;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const QString ip = addr.toString();
            if (isPrivate(ip))
                return ip;                 // idéal : une vraie adresse de LAN
            if (fallback.isEmpty())
                fallback = ip;             // à défaut, la première IPv4 trouvée
        }
    }
    return fallback;
}

QString MainWindow::detectWindowsUser() {
    // Le NOM DE COMPTE (pas le dossier de profil) : c'est lui qu'attend SMB.
    QString u = qEnvironmentVariable("USERNAME");
    if (u.isEmpty())
        u = qEnvironmentVariable("USER");
    return u;
}

QString MainWindow::shareNameFor(const QString& localRoot) {
    // Nom de partage propre à partir du dernier segment du chemin. Windows interdit
    // certains caractères ; on ne garde que l'alphanumérique, le tiret et l'underscore.
    const QString leaf = QDir(QDir::fromNativeSeparators(localRoot)).dirName();
    QString clean;
    for (const QChar c : leaf)
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            clean.append(c);
    if (clean.isEmpty())
        clean = QStringLiteral("Photos");
    return QStringLiteral("morfPhoto-") + clean;
}

void MainWindow::showNetworkAccessDialog() {
    if (m_pathMappings.isEmpty()) {
        const auto answer = QMessageBox::question(this, QStringLiteral("Assistant d'accès réseau"),
            QStringLiteral("Aucun mappage de chemins n'est configuré.\n\n"
                           "L'assistant part d'un mappage (dossier local Windows → point de "
                           "montage Linux) pour préparer le partage et les commandes.\n\n"
                           "Ouvrir maintenant « Mappage de chemins… » ?"));
        if (answer == QMessageBox::Yes)
            showMappingsDialog();
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Assistant d'accès réseau – PhotoHub"));
    dlg.resize(720, 620);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 16, 16, 16);

    const QString pc   = detectPcName();
    const QString ip   = detectLanIp();
    const QString user = detectWindowsUser();

    auto* intro = new QLabel(QStringLiteral(
        "Cet assistant partage vos photos <b>en lecture seule</b> pour que morfPhoto "
        "(sur le Raspberry Pi) puisse les indexer <b>sans jamais les déplacer ni les "
        "modifier</b>. Deux étapes : créer le partage sur ce PC, puis monter le dossier "
        "sur le Pi."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* detected = new QLabel(QStringLiteral(
        "Détecté automatiquement : PC <b>%1</b> · adresse <b>%2</b> · compte Windows <b>%3</b>")
        .arg(pc, ip.isEmpty() ? QStringLiteral("(introuvable)") : ip,
             user.isEmpty() ? QStringLiteral("(introuvable)") : user));
    detected->setWordWrap(true);
    detected->setStyleSheet(QStringLiteral("color:#99a1ad;"));
    layout->addWidget(detected);

    // Choix du mappage à préparer (dossier local ↔ montage serveur).
    auto* mapRow = new QHBoxLayout;
    mapRow->addWidget(new QLabel(QStringLiteral("Dossier à partager :")));
    auto* mapCombo = new QComboBox;
    for (const auto& [loc, srv] : m_pathMappings)
        mapCombo->addItem(QStringLiteral("%1  →  %2").arg(QDir::toNativeSeparators(loc), srv));
    mapRow->addWidget(mapCombo, 1);
    layout->addLayout(mapRow);

    // Étape 1 : commande de partage Windows.
    layout->addWidget(new QLabel(QStringLiteral("<b>Étape 1 — sur ce PC (Windows)</b>")));
    auto* winCmd = new QPlainTextEdit;
    winCmd->setReadOnly(true);
    winCmd->setMaximumHeight(70);
    winCmd->setFont(QFont(QStringLiteral("Consolas"), 9));
    layout->addWidget(winCmd);
    auto* winBtns = new QHBoxLayout;
    auto* createBtn = new QPushButton(QStringLiteral("Créer le partage (administrateur)"));
    auto* copyWinBtn = new QPushButton(QStringLiteral("Copier la commande"));
    winBtns->addWidget(createBtn);
    winBtns->addWidget(copyWinBtn);
    winBtns->addStretch(1);
    layout->addLayout(winBtns);

    // Étape 2 : commandes de montage Pi.
    layout->addWidget(new QLabel(QStringLiteral("<b>Étape 2 — sur le Raspberry Pi (à coller dans un terminal)</b>")));
    auto* piCmd = new QPlainTextEdit;
    piCmd->setReadOnly(true);
    piCmd->setFont(QFont(QStringLiteral("Consolas"), 9));
    layout->addWidget(piCmd, 1);
    auto* copyPiBtn = new QPushButton(QStringLiteral("Copier les commandes du Pi"));
    layout->addWidget(copyPiBtn);

    auto* note = new QLabel(QStringLiteral(
        "<span style='color:#e09000;'>Le mot de passe à mettre dans le fichier est celui "
        "de votre session Windows (compte <b>%1</b>). Si vous vous connectez avec un compte "
        "Microsoft, c'est le mot de passe de ce compte Microsoft. Un code PIN ne fonctionne "
        "pas pour le réseau, et un mot de passe vide est refusé par Windows.</span>")
        .arg(user.isEmpty() ? QStringLiteral("Windows") : user));
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    // (Re)génère les deux blocs de commandes selon le mappage sélectionné.
    const auto refresh = [&, this]() {
        const int i = mapCombo->currentIndex();
        if (i < 0 || i >= m_pathMappings.size())
            return;
        const QString localRoot  = m_pathMappings[i].first;
        const QString serverRoot = m_pathMappings[i].second;
        const QString share = shareNameFor(localRoot);
        const QString ipShown = ip.isEmpty() ? QStringLiteral("IP_DU_PC") : ip;
        const QString userShown = user.isEmpty() ? QStringLiteral("VOTRE_COMPTE") : user;

        winCmd->setPlainText(QStringLiteral(
            "net share %1=\"%2\" /GRANT:%3,READ /REMARK:\"morfPhoto - lecture seule\"")
            .arg(share, QDir::toNativeSeparators(localRoot), userShown));

        // uid/gid 1000 = user du service morfPhoto sur le Pi (morfredus).
        piCmd->setPlainText(QStringLiteral(
            "sudo mkdir -p %1\n"
            "sudo tee /etc/morfsystem/smb-photos.cred >/dev/null <<'EOF'\n"
            "username=%2\n"
            "password=VOTRE_MOT_DE_PASSE_WINDOWS\n"
            "EOF\n"
            "sudo chmod 600 /etc/morfsystem/smb-photos.cred\n"
            "sudo mount -t cifs //%3/%4 %1 -o credentials=/etc/morfsystem/smb-photos.cred,ro,uid=1000,gid=1000,iocharset=utf8,vers=3.0\n"
            "ls %1\n"
            "\n"
            "# Montage permanent (au redémarrage du Pi) :\n"
            "echo '//%3/%4 %1 cifs credentials=/etc/morfsystem/smb-photos.cred,ro,uid=1000,gid=1000,iocharset=utf8,vers=3.0,nofail,x-systemd.automount 0 0' | sudo tee -a /etc/fstab")
            .arg(serverRoot, userShown, ipShown, share));
    };
    connect(mapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [refresh](int){ refresh(); });
    refresh();

    connect(copyWinBtn, &QPushButton::clicked, &dlg, [winCmd]() {
        QGuiApplication::clipboard()->setText(winCmd->toPlainText());
    });
    connect(copyPiBtn, &QPushButton::clicked, &dlg, [piCmd]() {
        QGuiApplication::clipboard()->setText(piCmd->toPlainText());
    });

#ifdef Q_OS_WIN
    connect(createBtn, &QPushButton::clicked, &dlg, [&, this]() {
        const int i = mapCombo->currentIndex();
        if (i < 0 || i >= m_pathMappings.size())
            return;
        const QString localRoot = QDir::toNativeSeparators(m_pathMappings[i].first);
        const QString share = shareNameFor(m_pathMappings[i].first);
        const QString userShown = user.isEmpty() ? QString() : user;
        if (userShown.isEmpty()) {
            QMessageBox::warning(&dlg, QStringLiteral("Assistant d'accès réseau"),
                QStringLiteral("Compte Windows introuvable : créez le partage à la main "
                               "avec la commande affichée (en tant qu'administrateur)."));
            return;
        }
        // Échapper les apostrophes pour PowerShell (doublage).
        const auto psq = [](QString s) { return s.replace(QLatin1Char('\''), QStringLiteral("''")); };
        // Script élévé : crée le partage, ou (s'il existe) garantit l'accès lecture.
        const QString script = QStringLiteral(
            "$ErrorActionPreference='Stop'\n"
            "$n='%1'; $p='%2'; $u='%3'\n"
            "if (Get-SmbShare -Name $n -ErrorAction SilentlyContinue) {\n"
            "  Grant-SmbShareAccess -Name $n -AccountName $u -AccessRight Read -Force | Out-Null\n"
            "} else {\n"
            "  New-SmbShare -Name $n -Path $p -ReadAccess $u -Description 'morfPhoto - lecture seule' | Out-Null\n"
            "}\n"
            "Write-Host 'Partage pret'\n")
            .arg(psq(share), psq(localRoot), psq(userShown));
        const QString scriptPath = QDir(QDir::tempPath()).filePath(QStringLiteral("photohub-share.ps1"));
        QFile f(scriptPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(&dlg, QStringLiteral("Assistant d'accès réseau"),
                QStringLiteral("Impossible d'écrire le script temporaire."));
            return;
        }
        f.write(script.toUtf8());
        f.close();
        // Lance une élévation UAC : PowerShell rejoue le script en administrateur.
        const bool ok = QProcess::startDetached(QStringLiteral("powershell"), {
            QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            QStringLiteral("Start-Process powershell -Verb RunAs -ArgumentList "
                           "'-NoProfile','-ExecutionPolicy','Bypass','-File','%1'").arg(scriptPath)});
        if (ok)
            QMessageBox::information(&dlg, QStringLiteral("Assistant d'accès réseau"),
                QStringLiteral("Une fenêtre d'autorisation (UAC) va s'ouvrir pour créer le "
                               "partage « %1 » en lecture seule.\n\nAcceptez-la, puis passez à "
                               "l'étape 2 sur le Pi.").arg(share));
        else
            QMessageBox::warning(&dlg, QStringLiteral("Assistant d'accès réseau"),
                QStringLiteral("Lancement de PowerShell impossible. Créez le partage à la main "
                               "avec la commande affichée (en tant qu'administrateur)."));
    });
#else
    createBtn->setEnabled(false);
    createBtn->setToolTip(QStringLiteral("Création automatique disponible sous Windows uniquement."));
#endif

    dlg.exec();
}

// =============================================================================
// Mises à jour / À propos
// =============================================================================

void MainWindow::checkForUpdates(bool manual) {
    // Même mécanisme que SiteWatch / ComponentHub (morfUpdate, GitHub Releases).
    morfupdate::morfUpdateConfig cfg;
    cfg.owner          = QStringLiteral("morfredus");
    cfg.repo           = QStringLiteral("PhotoHub");
    cfg.currentVersion = QStringLiteral(PHOTOHUB_VERSION);
    // manual : affiche aussi « à jour » et les erreurs ; démarrage : silencieux.
    morfupdate::checkAndNotify(this, QStringLiteral("PhotoHub"), cfg, /*silentIfUpToDate=*/!manual);
}

void MainWindow::showAbout() {
    QMessageBox::about(this, QStringLiteral("À propos de PhotoHub"),
        QStringLiteral("<b>PhotoHub</b> %1<br><br>"
                       "Application desktop du domaine photo de morfSystem.<br>"
                       "Client de <b>morfPhoto</b> : découverte par capacité "
                       "<code>photo_index</code>, gestion des dossiers surveillés et "
                       "de l'indexation. Ne lit jamais les fichiers, ne connaît pas la base.<br><br>"
                       "© 2026 morfredus - GPL-3.0-only").arg(QStringLiteral(PHOTOHUB_VERSION)));
}

// =============================================================================
// Découverte
// =============================================================================

void MainWindow::onSourcesChanged() {
    const QList<ServiceInfo> found = m_discovery->withCapability(QLatin1String(kCapPhoto));
    const QString current = m_sourceCombo->currentData().toString();

    m_sourceCombo->blockSignals(true);
    m_sourceCombo->clear();
    int restore = -1;
    for (const ServiceInfo& s : found) {
        m_sourceCombo->addItem(s.label(), s.baseUrl());
        if (s.baseUrl() == current)
            restore = m_sourceCombo->count() - 1;
    }
    m_sourceCombo->blockSignals(false);

    if (found.isEmpty()) {
        m_connLabel->setText(QStringLiteral("Aucun morfPhoto détecté sur le réseau."));
        m_client->setBaseUrl(QString());
    } else if (restore >= 0) {
        m_sourceCombo->setCurrentIndex(restore);        // conserve le choix courant
    } else {
        m_sourceCombo->setCurrentIndex(0);              // auto-sélection du premier
        onSourceSelected(0);
    }

    // Enrichissement optionnel : bouton vers morfAnalytics si au moins une instance
    // annonce la capacité. L'instance CIBLE et l'URL finale sont choisies au clic,
    // en fonction du morfPhoto sélectionné (voir openAnalyticsClicked) : le contexte
    // et l'hôte sont ainsi conservés même quand plusieurs machines du parc tournent.
    const QList<ServiceInfo> analytics = m_discovery->withCapability(QLatin1String(kCapAnalytics));
    m_analyticsBtn->setVisible(!analytics.isEmpty());
}

void MainWindow::onSourceSelected(int index) {
    if (index < 0) return;
    const QString url = m_sourceCombo->itemData(index).toString();
    m_client->setBaseUrl(url);
    m_connLabel->setText(QStringLiteral("Connecté à %1").arg(m_sourceCombo->itemText(index)));
    m_client->refreshAll();
}

// =============================================================================
// Retours de morfPhoto
// =============================================================================

void MainWindow::onSummary(const QJsonObject& s) {
    const auto num = [&s](const char* k) { return QString::number(s.value(QLatin1String(k)).toInt()); };
    m_statFiles->setText(num("files_present"));
    m_statCameras->setText(num("cameras"));
    m_statLenses->setText(num("lenses"));
    m_statFolders->setText(num("folders_active"));
    m_statMissing->setText(num("files_missing"));
}

void MainWindow::onFolders(const QJsonArray& folders) {
    // Séparer les dossiers RETIRÉS (retrait doux, deleted_at renseigné) des dossiers
    // surveillés : la fenêtre principale ne montre que ces derniers, pour ne pas
    // l'encombrer. Les retirés vivent dans une fenêtre dédiée (Fichier > Dossiers
    // retirés). m_folders reste aligné sur les lignes du tableau (selectedFolderId).
    m_folders = QJsonArray();
    m_removedFolders = QJsonArray();
    for (const QJsonValue& v : folders) {
        const QJsonObject f = v.toObject();
        if (f.value(QStringLiteral("deleted_at")).isNull())
            m_folders.append(f);
        else
            m_removedFolders.append(f);
    }

    m_foldersTable->setRowCount(m_folders.size());
    for (int i = 0; i < m_folders.size(); ++i) {
        const QJsonObject f = m_folders[i].toObject();
        m_foldersTable->setItem(i, 0, new QTableWidgetItem(f.value(QStringLiteral("path")).toString()));
        m_foldersTable->setItem(i, 1, new QTableWidgetItem(folderState(f)));
    }

    // Refléter le nombre de retirés dans l'entrée de menu (grisée si aucun).
    if (m_removedAction) {
        const int n = m_removedFolders.size();
        m_removedAction->setText(n > 0
            ? QStringLiteral("Dossiers retirés (%1)…").arg(n)
            : QStringLiteral("Dossiers retirés…"));
        m_removedAction->setEnabled(n > 0);
    }
}

void MainWindow::onIndexStatus(const QJsonObject& status) {
    const bool indexing = status.value(QStringLiteral("state")).toString() == QLatin1String("indexing");
    m_progress->setVisible(indexing);
    if (indexing) {
        // Progression exposée par morfPhoto : le nombre de dossiers donne une barre
        // DÉTERMINÉE (dénominateur fiable, connu d'avance) ; le compteur de fichiers
        // matérialise l'avancée à l'intérieur d'un gros dossier. À défaut (morfPhoto
        // plus ancien, ou tout début de passe), on retombe sur la barre indéterminée.
        const QJsonObject prog = status.value(QStringLiteral("progress")).toObject();
        const int total = prog.value(QStringLiteral("folders_total")).toInt();
        const int done  = prog.value(QStringLiteral("folders_done")).toInt();

        if (total > 0) {
            m_progress->setRange(0, total);
            m_progress->setValue(done);
        } else {
            m_progress->setRange(0, 0);   // indéterminée tant que rien n'est connu
        }

        const qint64 files = static_cast<qint64>(prog.value(QStringLiteral("files_seen")).toDouble());
        const QString folder = prog.value(QStringLiteral("current_folder")).toString();
        QString txt;
        if (total > 0)
            txt = QStringLiteral("Indexation… dossier %1/%2 · %3 fichiers")
                      .arg(done).arg(total).arg(QLocale().toString(files));
        else
            txt = QStringLiteral("Indexation en cours…");
        if (!folder.isEmpty())
            txt += QStringLiteral(" · %1").arg(QFileInfo(folder).fileName());
        m_indexLabel->setText(txt);
        return;
    }
    // Cadence de l'indexation automatique (exposée par morfPhoto). Utile pour voir
    // d'un coup d'œil si une passe de fond tourne, et à quelle fréquence, ou si tout
    // se fait à la demande. Le bouton « indexer les changements » reste la voie manuelle.
    const QJsonObject watch = status.value(QStringLiteral("watch")).toObject();
    QString autoTxt;
    if (watch.contains(QStringLiteral("auto"))) {
        if (!watch.value(QStringLiteral("auto")).toBool()) {
            autoTxt = QStringLiteral("  ·  auto désactivée (à la demande)");
        } else {
            const qint64 ms = static_cast<qint64>(watch.value(QStringLiteral("interval_ms")).toDouble());
            QString every;
            if (ms % 86400000 == 0)
                every = (ms / 86400000 == 1) ? QStringLiteral("une fois par jour")
                                             : QStringLiteral("tous les %1 jours").arg(ms / 86400000);
            else if (ms % 3600000 == 0) every = QStringLiteral("toutes les %1 h").arg(ms / 3600000);
            else if (ms % 60000 == 0)   every = QStringLiteral("toutes les %1 min").arg(ms / 60000);
            else                        every = QStringLiteral("toutes les %1 s").arg(ms / 1000);
            autoTxt = QStringLiteral("  ·  auto : %1").arg(every);
        }
    }

    const QJsonObject run = status.value(QStringLiteral("last_run")).toObject();
    if (run.isEmpty()) {
        m_indexLabel->setText(QStringLiteral("Aucune indexation encore exécutée.%1").arg(autoTxt));
        return;
    }
    m_indexLabel->setText(QStringLiteral(
        "Dernière passe (%1) : %2 nouveaux · %3 modifiés · %4 disparus · %5 erreurs%6")
        .arg(run.value(QStringLiteral("mode")).toString())
        .arg(run.value(QStringLiteral("files_new")).toInt())
        .arg(run.value(QStringLiteral("files_updated")).toInt())
        .arg(run.value(QStringLiteral("files_missing")).toInt())
        .arg(run.value(QStringLiteral("errors_count")).toInt())
        .arg(autoTxt));
}

void MainWindow::onActionResult(bool ok, const QString& message) {
    statusBar()->showMessage(message, 5000);
    if (!ok)
        QMessageBox::warning(this, QStringLiteral("PhotoHub"), message);
}

void MainWindow::onFailed(const QString& message) {
    statusBar()->showMessage(QStringLiteral("morfPhoto injoignable : %1").arg(message), 4000);
}

void MainWindow::onRoots(const QStringList& roots) {
    m_allowedRoots = roots;
    if (roots.isEmpty())
        m_rootsLabel->setText(QStringLiteral("Racines autorisées : aucune. "
            "Déclarer un périmètre dans la configuration de morfPhoto (champ « roots »)."));
    else
        m_rootsLabel->setText(QStringLiteral("Racines autorisées : %1").arg(roots.join(QStringLiteral("  ·  "))));
}

// =============================================================================
// Actions dossiers
// =============================================================================

int MainWindow::selectedFolderId(bool* enabled) const {
    const int row = m_foldersTable->currentRow();
    if (row < 0 || row >= m_folders.size())
        return -1;
    const QJsonObject f = m_folders[row].toObject();
    if (enabled) *enabled = f.value(QStringLiteral("enabled")).toInt() == 1;
    return f.value(QStringLiteral("id")).toInt();
}

// -----------------------------------------------------------------------------
// addFolderClicked
//
// Workflow cross-platform :
//   1. QFileDialog ouvre le dossier Pictures local (Windows) : navigation naturelle.
//   2. applyPathMapping() traduit le chemin local en chemin serveur si un mappage
//      est configuré (ex. C:\Users\frede\Pictures\2016 → /mnt/photos/2016).
//   3. Un dialogue de confirmation affiche les deux chemins et laisse l'utilisateur
//      corriger le chemin serveur si nécessaire.
//   4. morfPhoto reçoit le chemin serveur et reste l'autorité finale.
//
// Si aucun mappage n'est configuré, le dialogue de confirmation affiche un
// avertissement et laisse l'utilisateur saisir le chemin serveur à la main.
// -----------------------------------------------------------------------------
void MainWindow::addFolderClicked() {
    if (!m_client->hasBase()) {
        statusBar()->showMessage(QStringLiteral("Aucun morfPhoto sélectionné."), 4000);
        return;
    }
    if (m_allowedRoots.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("PhotoHub"),
            QStringLiteral("Aucune racine autorisée n'est configurée dans morfPhoto.\n\n"
                           "Ajouter le périmètre voulu au champ « roots » de sa configuration "
                           "(morfphoto.json), puis redémarrer morfPhoto."));
        return;
    }

    // Étape 1 : sélection du dossier local via le sélecteur natif Windows.
    // On démarre dans le dossier Images de l'utilisateur.
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString localDir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Sélectionner le dossier à surveiller"),
        startDir);
    if (localDir.isEmpty())
        return;

    // Étape 2 : traduction chemin local → chemin serveur.
    const QString serverDir = applyPathMapping(localDir);

    // Étape 3 : dialogue de confirmation avec les deux chemins.
    // L'utilisateur peut corriger le chemin serveur si le mappage est imparfait
    // ou absent.
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Confirmer le dossier à ajouter"));
    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    // Chemin local (informatif, non éditable).
    layout->addWidget(new QLabel(QStringLiteral("Dossier local sélectionné :")));
    auto* localLabel = new QLabel(QDir::toNativeSeparators(localDir));
    localLabel->setStyleSheet(QStringLiteral("color:#99a1ad; font-family: monospace;"));
    localLabel->setWordWrap(true);
    layout->addWidget(localLabel);

    // Avertissement si aucun mappage ne s'est appliqué.
    const bool mappingApplied = (serverDir != QDir::fromNativeSeparators(localDir));
    if (!mappingApplied) {
        auto* warn = new QLabel(
            QStringLiteral("<span style='color:#e09000;'>⚠ Aucun mappage configuré. "
                           "Vérifiez ou corrigez le chemin serveur ci-dessous. "
                           "(<i>Fichier &gt; Mappage de chemins…</i>)</span>"));
        warn->setWordWrap(true);
        layout->addWidget(warn);
    }

    // Chemin serveur (éditable) : c'est ce qui sera envoyé à morfPhoto.
    layout->addWidget(new QLabel(QStringLiteral("Chemin sur le serveur morfPhoto (envoyé à l'API) :")));
    auto* serverEdit = new QLineEdit(serverDir);
    serverEdit->setMinimumWidth(420);
    serverEdit->setFont(QFont(QStringLiteral("Courier New"), 9));
    layout->addWidget(serverEdit);

    // Rappel des racines autorisées.
    auto* rootsInfo = new QLabel(
        QStringLiteral("Racines autorisées : %1").arg(m_allowedRoots.join(QStringLiteral("  ·  "))));
    rootsInfo->setStyleSheet(QStringLiteral("color:#99a1ad;"));
    rootsInfo->setWordWrap(true);
    layout->addWidget(rootsInfo);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Ajouter"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString dir = serverEdit->text().trimmed();
    if (dir.isEmpty())
        return;

    // morfPhoto est l'autorité finale : il valide le périmètre, l'existence du
    // dossier côté serveur et les droits. Le message d'erreur éventuel remonte
    // tel quel via onActionResult.
    m_client->addFolder(dir);
}

void MainWindow::toggleFolderClicked() {
    bool enabled = false;
    const int id = selectedFolderId(&enabled);
    if (id < 0) { statusBar()->showMessage(QStringLiteral("Sélectionner un dossier."), 3000); return; }
    m_client->setFolderEnabled(id, !enabled);
}

void MainWindow::removeFolderClicked() {
    const int id = selectedFolderId();
    if (id < 0) { statusBar()->showMessage(QStringLiteral("Sélectionner un dossier."), 3000); return; }
    const auto answer = QMessageBox::question(this, QStringLiteral("Retirer le dossier"),
        QStringLiteral("Retirer ce dossier de la surveillance ?\n\n"
                       "L'historique est conservé (retrait doux) : les fichiers sont "
                       "marqués absents, jamais supprimés."));
    if (answer == QMessageBox::Yes)
        m_client->removeFolder(id);
}

// -----------------------------------------------------------------------------
// showRemovedFoldersDialog
//
// Fenêtre séparée des dossiers retirés (retrait doux). On les sort de la fenêtre
// principale pour ne pas l'encombrer : ici on les consulte et on peut les
// restaurer. Le retrait doux conserve tout l'historique côté morfPhoto ; une
// restauration relance simplement la surveillance et une passe d'indexation
// ravive les fichiers.
// -----------------------------------------------------------------------------
void MainWindow::showRemovedFoldersDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Dossiers retirés – PhotoHub"));
    dlg.resize(620, 340);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 16, 16, 16);

    auto* info = new QLabel(
        QStringLiteral("Ces dossiers ont été retirés de la surveillance. Leur historique "
                       "est conservé (aucune photo n'est supprimée). Restaurer un dossier "
                       "relance sa surveillance et ravive ses photos."));
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color:#99a1ad;"));
    layout->addWidget(info);

    auto* table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({QStringLiteral("Dossier"), QStringLiteral("Retiré le")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (const QJsonValue& v : m_removedFolders) {
        const QJsonObject f = v.toObject();
        const int row = table->rowCount();
        table->insertRow(row);
        auto* pathItem = new QTableWidgetItem(f.value(QStringLiteral("path")).toString());
        // Mémoriser l'id de la sélection sur la ligne : c'est lui qu'attend l'API.
        pathItem->setData(Qt::UserRole, f.value(QStringLiteral("id")).toInt());
        table->setItem(row, 0, pathItem);
        // deleted_at est un horodatage ISO UTC : l'afficher en heure locale lisible.
        const QString iso = f.value(QStringLiteral("deleted_at")).toString();
        const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
        table->setItem(row, 1, new QTableWidgetItem(
            dt.isValid() ? dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : iso));
    }
    if (table->rowCount() > 0)
        table->selectRow(0);
    layout->addWidget(table, 1);

    auto* btns = new QDialogButtonBox;
    auto* restoreBtn = btns->addButton(QStringLiteral("Restaurer"), QDialogButtonBox::AcceptRole);
    btns->addButton(QStringLiteral("Fermer"), QDialogButtonBox::RejectRole);
    restoreBtn->setEnabled(table->rowCount() > 0);
    layout->addWidget(btns);

    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(restoreBtn, &QPushButton::clicked, &dlg, [this, table, &dlg]() {
        const int row = table->currentRow();
        if (row < 0)
            return;
        const int id = table->item(row, 0)->data(Qt::UserRole).toInt();
        m_client->restoreFolder(id);   // le rafraîchissement suit (refreshAll)
        dlg.accept();
    });

    dlg.exec();
}

void MainWindow::openAnalyticsClicked() {
    const QList<ServiceInfo> analytics = m_discovery->withCapability(QLatin1String(kCapAnalytics));
    if (analytics.isEmpty())
        return;

    // morfPhoto sélectionné : c'est LUI qui décide vers quel morfAnalytics aller.
    const QString source = m_sourceCombo->currentData().toString();
    const QString sourceHost = QUrl(source).host();

    // Choisir le morfAnalytics du MÊME hôte que le morfPhoto sélectionné : sur un
    // parc multi-machines (p. ex. pi4fred et un autre hôte), chaque machine a son
    // couple morfPhoto/morfAnalytics, et l'analyse doit rester locale à sa source.
    // À défaut d'instance co-localisée, on retombe sur la première entendue : le
    // paramètre `source` ci-dessous garantit qu'elle analysera quand même la bonne
    // photothèque (morfAnalytics sait la rapatrier à la demande).
    ServiceInfo target = analytics.first();
    for (const ServiceInfo& a : analytics) {
        if (!sourceHost.isEmpty() && (a.ip == sourceHost || a.host == sourceHost)) {
            target = a;
            break;
        }
    }

    // Conserver le contexte : ouvrir les analyses de la MÊME photothèque, sans
    // redemander la source. morfAnalytics lit `source` et rapatrie cette instance.
    QString url = target.baseUrl() + QStringLiteral("/photo");
    if (!source.isEmpty())
        url += QStringLiteral("?source=") + QString::fromUtf8(QUrl::toPercentEncoding(source));
    QDesktopServices::openUrl(QUrl(url));
}

} // namespace photohub
