/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "MainWindow.h"
#include "BeaconDiscovery.h"
#include "MorfPhotoClient.h"
#include "ContextDialog.h"

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
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QFileDialog>
#include <QListView>
#include <QTreeView>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
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
#include <QJsonArray>
#include <QHash>

#include <morfupdate/UpdateDialog.h>
#include <morfupdate/morfUpdateConfig.h>

namespace photohub {

namespace {
constexpr const char* kCapPhoto     = "photo_index";
constexpr const char* kCapAnalytics = "photo_analytics";

QString stepLabel(const QString& id) {
    static const QHash<QString, QString> labels = {
        {QStringLiteral("source_identified"),       QStringLiteral("Machine source identifiée")},
        {QStringLiteral("hostname_normalized"),     QStringLiteral("Hostname normalisé")},
        {QStringLiteral("mountpoint_created"),      QStringLiteral("Point de montage créé")},
        {QStringLiteral("credentials_written"),     QStringLiteral("Credentials propres à la source")},
        {QStringLiteral("credentials_permissions"), QStringLiteral("Permissions des credentials")},
        {QStringLiteral("smb_auth"),                QStringLiteral("Authentification SMB")},
        {QStringLiteral("cifs_mounted"),            QStringLiteral("Partage CIFS monté")},
        {QStringLiteral("share_readable"),          QStringLiteral("Partage accessible en lecture")},
        {QStringLiteral("fstab_configured"),        QStringLiteral("Montage persistant configuré")},
        {QStringLiteral("root_added"),              QStringLiteral("Root ajoutée à morfPhoto")},
        {QStringLiteral("json_valid"),              QStringLiteral("Configuration morfPhoto valide")},
        {QStringLiteral("service_restarted"),       QStringLiteral("morfPhoto redémarré")},
        {QStringLiteral("service_active"),          QStringLiteral("Service opérationnel")},
        {QStringLiteral("root_in_api"),             QStringLiteral("Root confirmée par l'API")},
    };
    return labels.value(id, id);
}

QString formatSourceReport(const QJsonObject& report) {
    QStringList lines;
    for (const QJsonValue& v : report.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject s = v.toObject();
        const bool ok = s.value(QStringLiteral("ok")).toBool();
        QString line = QStringLiteral("%1  %2")
            .arg(stepLabel(s.value(QStringLiteral("id")).toString()), -38)
            .arg(ok ? QStringLiteral("✓") : QStringLiteral("✗"));
        const QString detail = s.value(QStringLiteral("detail")).toString().trimmed();
        if (!ok && !detail.isEmpty())
            line += QStringLiteral("\n  %1").arg(detail);
        else if (ok && (s.value(QStringLiteral("id")).toString() == QLatin1String("hostname_normalized")
                        || s.value(QStringLiteral("id")).toString() == QLatin1String("mountpoint_created")))
            line += QStringLiteral("  %1").arg(detail);
        lines << line;
    }
    const QString detail = report.value(QStringLiteral("detail")).toString().trimmed();
    const QString code   = report.value(QStringLiteral("code")).toString().trimmed();
    if (!report.value(QStringLiteral("ok")).toBool() && !detail.isEmpty()) {
        lines << QString();
        if (!code.isEmpty())
            lines << code;
        lines << detail;
    } else if (report.value(QStringLiteral("already_configured")).toBool()) {
        lines << QString();
        lines << QStringLiteral("Configuration déjà opérationnelle (aucun changement).");
    }
    return lines.join(QLatin1Char('\n'));
}

QString folderState(const QJsonObject& f) {
    if (f.value(QStringLiteral("enabled")).toInt() == 1)
        return QStringLiteral("Actif");
    if (f.value(QStringLiteral("auto_disabled")).toInt() == 1)
        return QStringLiteral("Inactif (racine absente)");
    return QStringLiteral("Inactif");
}

// Colonne « Support / Analyses » : type de support (fixe ou amovible + nom de
// volume) et, le cas échéant, la mention d'exclusion des analyses.
QString folderSupport(const QJsonObject& f) {
    QString s;
    if (f.value(QStringLiteral("removable")).toInt() == 1) {
        const QString vol = f.value(QStringLiteral("volume_label")).toString();
        s = vol.isEmpty() ? QStringLiteral("Amovible")
                          : QStringLiteral("Amovible « %1 »").arg(vol);
    } else {
        s = QStringLiteral("Fixe");
    }
    if (f.value(QStringLiteral("analytics_excluded")).toInt() == 1)
        s += QStringLiteral("  ·  hors analyses");
    return s;
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
    m_contextsBtn = new QPushButton(QStringLiteral("Contextes photographiques…"));
    m_contextsBtn->setEnabled(false);   // activé dès qu'un morfPhoto est sélectionné
    srcRow->addWidget(m_contextsBtn);
    m_analyticsBtn = new QPushButton(QStringLiteral("Analyses avancées…"));
    m_analyticsBtn->setVisible(false);
    srcRow->addWidget(m_analyticsBtn);
    root->addLayout(srcRow);
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSourceSelected);
    connect(m_contextsBtn, &QPushButton::clicked, this, &MainWindow::showContextDialog);
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
    m_foldersTable = new QTableWidget(0, 3);
    m_foldersTable->setHorizontalHeaderLabels(
        {QStringLiteral("Dossier"), QStringLiteral("État"), QStringLiteral("Support / Analyses")});
    m_foldersTable->horizontalHeader()->setStretchLastSection(false);
    m_foldersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_foldersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_foldersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_foldersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_foldersTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_foldersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fLayout->addWidget(m_foldersTable);
    auto* fBtns = new QHBoxLayout;
    m_addBtn    = new QPushButton(QStringLiteral("Ajouter un dossier…"));
    m_toggleBtn = new QPushButton(QStringLiteral("Activer / Désactiver"));
    m_mediaBtn  = new QPushButton(QStringLiteral("Support amovible…"));
    m_analyticsExclBtn = new QPushButton(QStringLiteral("Exclure des analyses"));
    m_removeBtn = new QPushButton(QStringLiteral("Retirer"));
    m_purgeBtn  = new QPushButton(QStringLiteral("Supprimer des données…"));
    fBtns->addWidget(m_addBtn);
    fBtns->addWidget(m_toggleBtn);
    fBtns->addWidget(m_mediaBtn);
    fBtns->addWidget(m_analyticsExclBtn);
    fBtns->addWidget(m_removeBtn);
    fBtns->addStretch(1);
    fBtns->addWidget(m_purgeBtn);
    fLayout->addLayout(fBtns);
    m_rootsLabel = new QLabel(QStringLiteral("Racines autorisées : —"));
    m_rootsLabel->setWordWrap(true);
    m_rootsLabel->setStyleSheet(QStringLiteral("color:#99a1ad"));
    fLayout->addWidget(m_rootsLabel);
    root->addWidget(folders, 1);
    connect(m_addBtn, &QPushButton::clicked, this, &MainWindow::addFolderClicked);
    connect(m_toggleBtn, &QPushButton::clicked, this, &MainWindow::toggleFolderClicked);
    connect(m_mediaBtn, &QPushButton::clicked, this, &MainWindow::editMediaClicked);
    connect(m_analyticsExclBtn, &QPushButton::clicked, this, &MainWindow::toggleAnalyticsExclusionClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &MainWindow::removeFolderClicked);
    connect(m_purgeBtn, &QPushButton::clicked, this, &MainWindow::showPurgeDialog);
    // Le libellé du bouton d'exclusion suit la sélection : « Exclure » ou « Réintégrer ».
    connect(m_foldersTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const QJsonObject f = selectedFolder();
        const bool excluded = f.value(QStringLiteral("analytics_excluded")).toInt() == 1;
        m_analyticsExclBtn->setText(excluded ? QStringLiteral("Réintégrer aux analyses")
                                             : QStringLiteral("Exclure des analyses"));
    });

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
    m_indexLabel->setWordWrap(true);
    iLayout->addWidget(m_indexLabel);
    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    m_progress->setTextVisible(true);
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
            "Exemple : <b>C:\\Users\\&lt;vous&gt;\\Pictures</b> → <b>/mnt/photos_&lt;poste&gt;</b>"));
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
        table->setItem(row, 1, new QTableWidgetItem(predictedMountpoint()));
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

QString MainWindow::canonicalSourceSlug() {
    // ASUS-DEV -> asus-dev ; jamais une IP. Meme convention que le serveur.
    QString s = detectPcName().trimmed().toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]")), QStringLiteral("-"));
    while (s.startsWith(QLatin1Char('-')) || s.startsWith(QLatin1Char('.')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('-')) || s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s.isEmpty() ? QStringLiteral("pc") : s;
}

QString MainWindow::predictedMountpoint() {
    return QStringLiteral("/mnt/photos_%1").arg(canonicalSourceSlug());
}

void MainWindow::showNetworkAccessDialog() {
    if (m_pathMappings.isEmpty()) {
        const auto answer = QMessageBox::question(this, QStringLiteral("Assistant d'accès réseau"),
            QStringLiteral("Aucun mappage de chemins n'est configuré.\n\n"
                           "L'assistant part d'un mappage (dossier local ↔ chemin vu par "
                           "morfPhoto) pour préparer le partage et les commandes.\n\n"
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
        "Cet assistant prépare l'accès de morfPhoto à vos photos, <b>en lecture seule</b> "
        "(morfPhoto ne déplace ni ne modifie jamais vos fichiers). morfPhoto peut tourner "
        "sur un serveur Linux (Raspberry Pi ou autre), sur ce PC, ou sur un autre PC "
        "Windows : <b>choisissez la situation</b>, les étapes s'adaptent."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* detected = new QLabel(QStringLiteral(
        "Détecté automatiquement : PC <b>%1</b> · adresse <b>%2</b> · compte Windows <b>%3</b>")
        .arg(pc, ip.isEmpty() ? QStringLiteral("(introuvable)") : ip,
             user.isEmpty() ? QStringLiteral("(introuvable)") : user));
    detected->setWordWrap(true);
    detected->setStyleSheet(QStringLiteral("color:#99a1ad;"));
    layout->addWidget(detected);

    // Où tourne morfPhoto ? Le choix commande toute la suite : partage + montage
    // réseau (serveur Linux), rien du tout (même machine), ou partage + racine UNC
    // (autre PC Windows). Les index correspondent aux cas testés dans refresh().
    auto* topoRow = new QHBoxLayout;
    topoRow->addWidget(new QLabel(QStringLiteral("morfPhoto tourne sur :")));
    auto* topoCombo = new QComboBox;
    topoCombo->addItem(QStringLiteral("Un serveur Linux (Raspberry Pi ou autre) — photos partagées depuis ce PC"));
    topoCombo->addItem(QStringLiteral("Ce PC Windows — morfPhoto et les photos sur la même machine"));
    topoCombo->addItem(QStringLiteral("Un autre PC Windows — photos partagées depuis ce PC"));
    topoRow->addWidget(topoCombo, 1);
    layout->addLayout(topoRow);

    // Choix du mappage à préparer (dossier local ↔ chemin vu par morfPhoto).
    auto* mapRow = new QHBoxLayout;
    mapRow->addWidget(new QLabel(QStringLiteral("Dossier à partager :")));
    auto* mapCombo = new QComboBox;
    for (const auto& [loc, srv] : m_pathMappings)
        mapCombo->addItem(QStringLiteral("%1  →  %2").arg(QDir::toNativeSeparators(loc), srv));
    mapRow->addWidget(mapCombo, 1);
    layout->addLayout(mapRow);

    // Étape 1 : partage Windows (seulement quand morfPhoto est sur une AUTRE machine).
    auto* step1Label = new QLabel(QStringLiteral("<b>Étape 1 — sur ce PC (Windows) : partager le dossier</b>"));
    layout->addWidget(step1Label);
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

    // Étape 2 : ce qu'il faut faire sur la machine morfPhoto (montage réseau,
    // racine UNC, ou simple déclaration locale — selon la topologie choisie).
    auto* step2Label = new QLabel;
    layout->addWidget(step2Label);
    auto* serverCmd = new QPlainTextEdit;
    serverCmd->setReadOnly(true);
    serverCmd->setFont(QFont(QStringLiteral("Consolas"), 9));
    layout->addWidget(serverCmd, 1);
    auto* copyServerBtn = new QPushButton(QStringLiteral("Copier"));
    layout->addWidget(copyServerBtn);

    // Identifiants du montage, éditables avant l'envoi. L'identifiant SMB validé
    // est le nom d'utilisateur Windows de la machine, y compris si la session est
    // liée à un compte Microsoft (ce n'est PAS l'e-mail). Le mot de passe, lui,
    // change : session locale, ou mot de passe Microsoft. Jamais le PIN.
    auto* credBox = new QWidget;
    auto* credLay = new QVBoxLayout(credBox);
    credLay->setContentsMargins(0, 0, 0, 0);
    credLay->addWidget(new QLabel(QStringLiteral(
        "<b>Identifiants Windows pour l'accès réseau</b>")));
    auto* localRadio = new QRadioButton(QStringLiteral("Compte Windows local"));
    auto* msRadio = new QRadioButton(QStringLiteral("Compte Windows lié à un compte Microsoft"));
    localRadio->setChecked(true);
    auto* typeGroup = new QButtonGroup(&dlg);
    typeGroup->addButton(localRadio);
    typeGroup->addButton(msRadio);
    credLay->addWidget(localRadio);
    credLay->addWidget(msRadio);
    auto* userRow = new QHBoxLayout;
    userRow->addWidget(new QLabel(QStringLiteral("Nom d'utilisateur :")));
    auto* userEdit = new QLineEdit(user);
    userEdit->setPlaceholderText(QStringLiteral(
        "nom d'utilisateur Windows de cette machine (ex. Fred)"));
    userRow->addWidget(userEdit, 1);
    credLay->addLayout(userRow);
    auto* pwdRow = new QHBoxLayout;
    pwdRow->addWidget(new QLabel(QStringLiteral("Mot de passe :")));
    auto* pwdEdit = new QLineEdit;
    pwdEdit->setEchoMode(QLineEdit::Password);
    pwdRow->addWidget(pwdEdit, 1);
    credLay->addLayout(pwdRow);
    auto* pwdHint = new QLabel;
    pwdHint->setWordWrap(true);
    pwdHint->setStyleSheet(QStringLiteral("color:#e09000;"));
    credLay->addWidget(pwdHint);
    const auto refreshPwdHint = [pwdEdit, pwdHint, localRadio]() {
        if (localRadio->isChecked()) {
            pwdEdit->setPlaceholderText(QStringLiteral(
                "mot de passe de session Windows (pas le PIN)"));
            pwdHint->setText(QStringLiteral(
                "<b>Compte local :</b> utilisez votre mot de passe de session Windows.<br>"
                "<b>Compte Microsoft :</b> utilisez le mot de passe de votre compte Microsoft.<br>"
                "Dans les deux cas, l'identifiant est le <b>nom d'utilisateur Windows</b> "
                "de cette machine, pas l'adresse e-mail, et jamais le PIN Windows Hello, "
                "l'empreinte, la reconnaissance faciale ou une passkey."));
        } else {
            pwdEdit->setPlaceholderText(QStringLiteral(
                "mot de passe du compte Microsoft associé (pas le PIN)"));
            pwdHint->setText(QStringLiteral(
                "La session est liée à Microsoft : l'identifiant reste le "
                "<b>nom Windows de cette machine</b> (pas l'e-mail). Le mot de passe "
                "est celui du <b>compte Microsoft</b>, pas le PIN Windows Hello."));
        }
    };
    connect(localRadio, &QRadioButton::toggled, &dlg, [refreshPwdHint](bool) { refreshPwdHint(); });
    refreshPwdHint();
    layout->addWidget(credBox);

    // Un-clic (serveur Linux) : au lieu de coller les commandes de l'étape 2, envoyer
    // la config directement à morfPhoto (comme « Envoyer la config » de SiteWatch).
    // morfPhoto monte alors le partage en lecture seule via son helper privilégié.
    // Multi-machine : chaque poste pousse SA source, sans écraser les autres.
    auto* pushBtn = new QPushButton(QStringLiteral("Envoyer la config au serveur morfPhoto (recommandé)"));
    pushBtn->setStyleSheet(QStringLiteral("font-weight:bold; padding:6px;"));
    layout->addWidget(pushBtn);
    auto* pushHint = new QLabel(QStringLiteral(
        "Évite le terminal : le serveur monte le partage tout seul et l'ajoute à ses racines. "
        "L'étape 1 (partage de ce dossier) reste nécessaire. Le mot de passe n'est utilisé "
        "qu'une fois pour le montage et n'est jamais conservé par le service."));
    pushHint->setWordWrap(true);
    pushHint->setStyleSheet(QStringLiteral("color:#99a1ad;"));
    layout->addWidget(pushHint);

    // Note mot de passe : utile seulement pour un montage SMB depuis Linux (fichier
    // d'identifiants). Masquée dans les autres cas.
    auto* note = new QLabel(QStringLiteral(
        "<span style='color:#e09000;'>Le PIN Windows Hello, l'empreinte, la "
        "reconnaissance faciale ou une passkey ne sont <b>pas</b> le mot de passe "
        "SMB. Compte local : mot de passe de session. Compte Microsoft : mot de "
        "passe du compte Microsoft. Identifiant dans les deux cas : le nom "
        "Windows de cette machine (détecté : « %1 »), pas l'adresse e-mail. "
        "Un mot de passe vide est refusé. Il n'est pas nécessaire de créer un "
        "compte technique dédié.</span>")
        .arg(user.isEmpty() ? QStringLiteral("Windows") : user));
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* stepsLabel = new QLabel(QStringLiteral("<b>État de la configuration</b>"));
    layout->addWidget(stepsLabel);
    auto* stepsView = new QPlainTextEdit;
    stepsView->setReadOnly(true);
    stepsView->setMaximumHeight(180);
    stepsView->setFont(QFont(QStringLiteral("Consolas"), 9));
    stepsView->setPlaceholderText(QStringLiteral(
        "Les étapes s'afficheront ici après l'envoi de la configuration."));
    layout->addWidget(stepsView);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    // (Re)génère les blocs selon la topologie ET le mappage sélectionnés. Trois cas :
    //   0 = serveur Linux : partage SMB ici + montage cifs là-bas ;
    //   1 = ce PC : rien à partager, juste déclarer le dossier local dans roots ;
    //   2 = autre PC Windows : partage SMB ici + racine UNC là-bas (aucun montage).
    const auto refresh = [&, this]() {
        const int i = mapCombo->currentIndex();
        if (i < 0 || i >= m_pathMappings.size())
            return;
        const int topo = topoCombo->currentIndex();
        const bool needShare = (topo == 0 || topo == 2);   // photos partagées depuis ce PC
        const bool linuxMount = (topo == 0);               // montage cifs + fichier d'identifiants

        const QString localRoot  = m_pathMappings[i].first;
        const QString share = shareNameFor(localRoot);
        const QString ipShown   = ip.isEmpty() ? QStringLiteral("IP_DU_PC") : ip;
        const QString pcShown   = pc.isEmpty() ? QStringLiteral("NOM_DU_PC") : pc;
        const QString userShown = user.isEmpty() ? QStringLiteral("VOTRE_COMPTE") : user;

        const QString credFile = QStringLiteral("/etc/morfsystem/smb-photos-%1.cred")
                                     .arg(canonicalSourceSlug());
        const QString mountPath = predictedMountpoint();
        // Chemin que morfPhoto verra (à mettre dans "roots") selon la topologie :
        //   Linux  -> /mnt/photos_<hostname> (jamais /mnt/photos générique) ;
        //   ce PC  -> le dossier local lui-même (slashs avant, identité) ;
        //   autre PC Windows -> une racine UNC vers le partage de ce PC.
        QString serverRoot;
        if (topo == 0)      serverRoot = mountPath;
        else if (topo == 1) serverRoot = QDir::fromNativeSeparators(localRoot);
        else                serverRoot = QStringLiteral("//%1/%2").arg(pcShown, share);

        // Étape 1 (partage) : visible seulement quand morfPhoto est ailleurs.
        step1Label->setVisible(needShare);
        winCmd->setVisible(needShare);
        createBtn->setVisible(needShare);
        copyWinBtn->setVisible(needShare);
        if (needShare)
            winCmd->setPlainText(QStringLiteral(
                "net share %1=\"%2\" /GRANT:%3,READ /REMARK:\"morfPhoto - lecture seule\"")
                .arg(share, QDir::toNativeSeparators(localRoot), userShown));

        // Note mot de passe : seulement pour le montage Linux (fichier d'identifiants).
        note->setVisible(linuxMount);

        // Bouton un-clic : uniquement pour le serveur Linux (topo 0), le seul cas où
        // morfPhoto sait monter lui-même. Les deux autres topologies restent manuelles.
        pushBtn->setVisible(topo == 0);
        pushHint->setVisible(topo == 0);
        credBox->setVisible(topo == 0);
        stepsLabel->setVisible(topo == 0);
        stepsView->setVisible(topo == 0);

        if (topo == 0) {
            // Serveur Linux : installer cifs-utils au besoin, monter en lecture seule,
            // rendre le montage permanent, puis déclarer la racine dans morfphoto.json.
            // uid/gid 1000 = utilisateur du service morfPhoto (à vérifier avec `id`).
            step2Label->setText(QStringLiteral("<b>Étape 2 — sur le serveur Linux (à coller dans un terminal)</b>"));
            serverCmd->setPlainText(QStringLiteral(
                "# Client SMB (si absent) : sudo apt install -y cifs-utils\n"
                "sudo mkdir -p %1\n"
                "sudo tee %5 >/dev/null <<'EOF'\n"
                "username=%2\n"
                "password=VOTRE_MOT_DE_PASSE_WINDOWS\n"
                "EOF\n"
                "sudo chmod 600 %5\n"
                "sudo chown root:root %5\n"
                "sudo mount -t cifs //%3/%4 %1 -o credentials=%5,ro,uid=1000,gid=1000,iocharset=utf8,vers=3.0\n"
                "findmnt -t cifs %1 && ls %1\n"
                "\n"
                "# Montage permanent (au redémarrage), seulement après validation :\n"
                "echo '//%3/%4 %1 cifs credentials=%5,ro,uid=1000,gid=1000,iocharset=utf8,vers=3.0,nofail,x-systemd.automount 0 0' | sudo tee -a /etc/fstab\n"
                "sudo systemctl daemon-reload\n"
                "\n"
                "# Enfin : ajouter \"%1\" au champ \"roots\" de morfphoto.json (sans retirer les racines existantes), valider le JSON, puis redémarrer morfPhoto.")
                .arg(serverRoot, userShown, ipShown, share, credFile));
        } else if (topo == 1) {
            // Ce PC : morfPhoto et les photos sur la même machine. Aucun partage,
            // aucun montage : il suffit de déclarer le dossier local dans roots.
            step2Label->setText(QStringLiteral("<b>Sur cette machine — configuration de morfPhoto</b>"));
            serverCmd->setPlainText(QStringLiteral(
                "# morfPhoto et les photos sont sur cette machine : aucun partage, aucun montage.\n"
                "# Dans morfphoto.json (à côté du binaire morfPhoto), déclarez le dossier local\n"
                "# (en slashs avant) dans \"roots\" :\n"
                "\n"
                "    \"roots\": [ \"%1\" ]\n"
                "\n"
                "# exiftool.exe doit être présent dans le PATH.\n"
                "# Dans PhotoHub, aucun mappage n'est nécessaire (le chemin local EST le chemin serveur).")
                .arg(serverRoot));
        } else {
            // Autre PC Windows : partage ici, puis racine UNC là-bas (pas de montage).
            step2Label->setText(QStringLiteral("<b>Étape 2 — sur l'autre PC Windows — configuration de morfPhoto</b>"));
            serverCmd->setPlainText(QStringLiteral(
                "# morfPhoto tourne sur un autre PC Windows : aucun montage à faire.\n"
                "# Dans son morfphoto.json (à côté du binaire morfPhoto), déclarez la racine UNC :\n"
                "\n"
                "    \"roots\": [ \"%1\" ]\n"
                "\n"
                "# Le compte qui EXÉCUTE le service morfPhoto doit avoir accès au partage \\\\%2\\%3.\n"
                "# exiftool.exe doit être présent dans le PATH de cette machine.")
                .arg(serverRoot, pcShown, share));
        }
    };
    connect(topoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [refresh](int){ refresh(); });
    connect(mapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [refresh](int){ refresh(); });
    refresh();

    connect(copyWinBtn, &QPushButton::clicked, &dlg, [winCmd]() {
        QGuiApplication::clipboard()->setText(winCmd->toPlainText());
    });
    connect(copyServerBtn, &QPushButton::clicked, &dlg, [serverCmd]() {
        QGuiApplication::clipboard()->setText(serverCmd->toPlainText());
    });

    // Un-clic : pousser la source vers morfPhoto (topologie serveur Linux). Remplace
    // le copier-coller des commandes de l'étape 2.
    connect(pushBtn, &QPushButton::clicked, &dlg, [&, this]() {
        if (!m_client->hasBase()) {
            QMessageBox::information(&dlg, QStringLiteral("Assistant d'accès réseau"),
                QStringLiteral("Aucun serveur morfPhoto sélectionné. Choisissez-en un dans la "
                               "barre de PhotoHub, puis réessayez."));
            return;
        }
        const int i = mapCombo->currentIndex();
        if (i < 0 || i >= m_pathMappings.size())
            return;
        if (ip.isEmpty()) {
            QMessageBox::warning(&dlg, QStringLiteral("Assistant d'accès réseau"),
                QStringLiteral("Adresse IP de ce PC introuvable : impossible de composer le partage."));
            return;
        }
        const QString localRoot = m_pathMappings[i].first;
        const QString share     = shareNameFor(localRoot);
        const QString account   = userEdit->text().trimmed();
        const QString pwd       = pwdEdit->text();
        if (account.isEmpty() || pwd.isEmpty()) {
            QMessageBox::warning(&dlg, QStringLiteral("Assistant d'accès réseau"),
                QStringLiteral("Renseignez le nom d'utilisateur Windows ET le mot de passe "
                               "avant d'envoyer la config.\n\n"
                               "L'identifiant est le nom de session Windows de cette machine "
                               "(pas l'e-mail Microsoft).\n"
                               "Compte local : mot de passe de session.\n"
                               "Compte Microsoft : mot de passe du compte Microsoft.\n"
                               "Jamais le PIN Windows Hello."));
            return;
        }
        pushBtn->setEnabled(false);
        stepsView->setPlainText(QStringLiteral("Vérification du serveur morfPhoto…"));
        const QString hostname = pc;
        m_client->checkSourcesReady(
            [this, i, stepsView, pushBtn, ip, share, account, pwd, hostname](bool ready, const QJsonObject& readyReport) {
                if (!ready) {
                    stepsView->setPlainText(formatSourceReport(readyReport));
                    pushBtn->setEnabled(true);
                    QMessageBox::warning(pushBtn->window(), QStringLiteral("PhotoHub"),
                        readyReport.value(QStringLiteral("detail")).toString(
                            QStringLiteral("morfPhoto n'est pas pret a monter une source "
                                           "(helper privilegie). Reinstaller le paquet "
                                           "morfPhoto sur le serveur.")));
                    return;
                }
                stepsView->setPlainText(QStringLiteral("Envoi de la configuration…"));
                m_client->pushSource(ip, share, account, pwd, hostname,
            [this, i, stepsView, pushBtn](bool ok, const QJsonObject& report) {
                stepsView->setPlainText(formatSourceReport(report));
                const QString mountpoint = report.value(QStringLiteral("mountpoint")).toString();
                if (!ok) {
                    pushBtn->setEnabled(true);
                    return;
                }
                if (!mountpoint.isEmpty() && i >= 0 && i < m_pathMappings.size()) {
                    m_pathMappings[i].second = mountpoint;
                    saveMappings();
                }
                const bool waitRestart = report.value(QStringLiteral("restart_needed")).toBool();
                if (waitRestart)
                    stepsView->appendPlainText(QStringLiteral("\nAttente du redémarrage de morfPhoto…"));
                m_client->confirmSourceRoot(mountpoint, waitRestart,
                    [stepsView, pushBtn](bool confirmed, const QJsonObject& extra) {
                        QString extraText;
                        extraText += QStringLiteral("\n%1  %2")
                            .arg(stepLabel(QStringLiteral("service_active")), -38)
                            .arg(extra.value(QStringLiteral("service_active")).toBool()
                                     ? QStringLiteral("✓") : QStringLiteral("✗"));
                        extraText += QStringLiteral("\n%1  %2")
                            .arg(stepLabel(QStringLiteral("root_in_api")), -38)
                            .arg(extra.value(QStringLiteral("root_in_api")).toBool()
                                     ? QStringLiteral("✓") : QStringLiteral("✗"));
                        if (!confirmed) {
                            extraText += QStringLiteral(
                                "\n\nmorfPhoto a redémarré mais la racine est absente de l'API, "
                                "ou le service n'est pas opérationnel.");
                        }
                        stepsView->appendPlainText(extraText);
                        pushBtn->setEnabled(true);
                    });
            });
            });
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

    // La qualification des contextes s'appuie sur le morfPhoto sélectionné.
    m_contextsBtn->setEnabled(!found.isEmpty());
}

void MainWindow::onSourceSelected(int index) {
    if (index < 0) return;
    const QString url = m_sourceCombo->itemData(index).toString();
    m_client->setBaseUrl(url);
    m_connLabel->setText(QStringLiteral("Connecté à %1").arg(m_sourceCombo->itemText(index)));
    m_contextsBtn->setEnabled(true);
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
        m_foldersTable->setItem(i, 2, new QTableWidgetItem(folderSupport(f)));
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
    // Pendant une passe, interroger plus souvent : un gros dossier ne doit pas
    // laisser la barre figée 3 secondes d'affilée.
    m_refresh->setInterval(indexing ? 400 : 3000);
    if (indexing) {
        const QJsonObject prog = status.value(QStringLiteral("progress")).toObject();
        const int foldersTotal = prog.value(QStringLiteral("folders_total")).toInt();
        const int foldersDone  = prog.value(QStringLiteral("folders_done")).toInt();
        const qint64 filesSeen = static_cast<qint64>(
            prog.value(QStringLiteral("files_seen")).toDouble());
        const QJsonValue pctVal = prog.value(QStringLiteral("percent"));
        // Une seule ligne : perception d'avancement. Le bilan (connus, ajoutés…)
        // n'a de sens qu'une fois la passe close, dans le bloc ci-dessous.
        const int folderNo = foldersTotal <= 0
            ? 0
            : qBound(1, foldersDone < foldersTotal ? foldersDone + 1 : foldersTotal,
                     foldersTotal);
        QString txt = QStringLiteral("Indexation…");
        if (folderNo > 0)
            txt += QStringLiteral(" dossier %1/%2").arg(folderNo).arg(foldersTotal);
        txt += QStringLiteral(" · %1 fichiers examinés")
                   .arg(QLocale().toString(filesSeen));
        m_indexLabel->setText(txt);

        m_progress->setTextVisible(true);
        if (pctVal.isDouble()) {
            m_progress->setRange(0, 100);
            m_progress->setFormat(QStringLiteral("%p%"));
            m_progress->setValue(qBound(0, qRound(pctVal.toDouble()), 100));
        } else {
            m_progress->setRange(0, 0);
            m_progress->setFormat(QString());
        }
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
    const int seen      = run.value(QStringLiteral("files_seen")).toInt();
    const int created   = run.value(QStringLiteral("files_new")).toInt();
    const int updated   = run.value(QStringLiteral("files_updated")).toInt();
    const int missing   = run.value(QStringLiteral("files_missing")).toInt();
    const int errors    = run.value(QStringLiteral("errors_count")).toInt();
    const int folders   = run.value(QStringLiteral("folders_total")).toInt();
    const int unchanged = qMax(0, seen - created - updated);
    QString summary = QStringLiteral(
        "Indexation terminée\n"
        "%1 fichiers examinés\n"
        "%2 déjà connus\n"
        "%3 ajoutés\n"
        "%4 mis à jour\n"
        "%5 disparus\n"
        "%6 erreur%7")
                          .arg(QLocale().toString(seen))
                          .arg(QLocale().toString(unchanged))
                          .arg(QLocale().toString(created))
                          .arg(QLocale().toString(updated))
                          .arg(QLocale().toString(missing))
                          .arg(QLocale().toString(errors))
                          .arg(errors > 1 ? QStringLiteral("s") : QString());
    if (folders > 0)
        summary += QStringLiteral("\n%1 dossiers parcourus").arg(folders);
    summary += autoTxt;
    m_indexLabel->setText(summary);
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

QJsonObject MainWindow::selectedFolder() const {
    const int row = m_foldersTable->currentRow();
    if (row < 0 || row >= m_folders.size())
        return {};
    return m_folders[row].toObject();
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

    // Étape 1 : sélection d'UN OU PLUSIEURS dossiers locaux. Un CD contient souvent
    // plusieurs dossiers (par année, par événement) : les ajouter un par un serait
    // pénible. Le sélecteur natif Windows ne sait pas choisir plusieurs dossiers ;
    // on passe donc par le sélecteur Qt (non natif) et on active la sélection
    // multiple sur sa vue interne (Ctrl/Maj pour cocher plusieurs entrées d'un même
    // dossier parent, ex. les dossiers d'un CD).
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QFileDialog fileDlg(this, QStringLiteral("Sélectionner un ou plusieurs dossiers (Ctrl/Maj pour plusieurs)"),
                        startDir);
    fileDlg.setFileMode(QFileDialog::Directory);
    fileDlg.setOption(QFileDialog::ShowDirsOnly, true);
    fileDlg.setOption(QFileDialog::DontUseNativeDialog, true);   // requis pour la multi-sélection
    if (auto* lv = fileDlg.findChild<QListView*>(QStringLiteral("listView")))
        lv->setSelectionMode(QAbstractItemView::ExtendedSelection);
    if (auto* tv = fileDlg.findChild<QTreeView*>())
        tv->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QStringList localDirs;
    if (fileDlg.exec() == QDialog::Accepted)
        localDirs = fileDlg.selectedFiles();
    localDirs.removeAll(QString());
    if (localDirs.isEmpty())
        return;

    // Étape 2 : dialogue de confirmation. Une ligne par dossier : chemin local (fixe)
    // et chemin serveur (éditable, pré-rempli par le mappage). L'utilisateur corrige
    // au besoin le chemin envoyé à morfPhoto.
    QDialog dlg(this);
    dlg.setWindowTitle(localDirs.size() > 1 ? QStringLiteral("Confirmer les dossiers à ajouter")
                                            : QStringLiteral("Confirmer le dossier à ajouter"));
    dlg.resize(680, 440);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    layout->addWidget(new QLabel(QStringLiteral(
        "%1 dossier(s) sélectionné(s). Le <b>chemin serveur</b> (colonne de droite) est "
        "ce qui sera envoyé à morfPhoto ; corrigez-le si un mappage manque.")
        .arg(localDirs.size())));

    auto* table = new QTableWidget(localDirs.size(), 2);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("Dossier local"), QStringLiteral("Chemin serveur morfPhoto (éditable)")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    bool anyUnmapped = false;
    for (int i = 0; i < localDirs.size(); ++i) {
        const QString local  = localDirs[i];
        const QString server = applyPathMapping(local);
        if (server == QDir::fromNativeSeparators(local))
            anyUnmapped = true;
        auto* locItem = new QTableWidgetItem(QDir::toNativeSeparators(local));
        locItem->setFlags(locItem->flags() & ~Qt::ItemIsEditable);   // informatif
        table->setItem(i, 0, locItem);
        table->setItem(i, 1, new QTableWidgetItem(server));
    }
    layout->addWidget(table, 1);

    if (anyUnmapped) {
        auto* warn = new QLabel(
            QStringLiteral("<span style='color:#e09000;'>⚠ Un ou plusieurs dossiers n'ont "
                           "pas de mappage : vérifiez leur chemin serveur ci-dessus. "
                           "(<i>Fichier &gt; Mappage de chemins…</i>)</span>"));
        warn->setWordWrap(true);
        layout->addWidget(warn);
    }

    auto* rootsInfo = new QLabel(
        QStringLiteral("Racines autorisées : %1").arg(m_allowedRoots.join(QStringLiteral("  ·  "))));
    rootsInfo->setStyleSheet(QStringLiteral("color:#99a1ad;"));
    rootsInfo->setWordWrap(true);
    layout->addWidget(rootsInfo);

    // --- Support amovible (CD/DVD, disque d'archive), appliqué à TOUS les dossiers ---
    // Cas d'usage principal : plusieurs dossiers d'un même CD partagent le même volume.
    // morfPhoto NE marque JAMAIS disparues les photos d'un dossier amovible support absent.
    auto* removableChk = new QCheckBox(
        QStringLiteral("Support amovible (CD/DVD, disque d'archive) — appliqué à tous les dossiers ci-dessus"));
    layout->addWidget(removableChk);
    auto* removableHint = new QLabel(
        QStringLiteral("Ne jamais marquer ses photos disparues quand le support est absent : "
                       "l'archive reste dans la base et dans les analyses, support éjecté."));
    removableHint->setWordWrap(true);
    removableHint->setStyleSheet(QStringLiteral("color:#99a1ad;"));
    layout->addWidget(removableHint);

    auto* volRow = new QHBoxLayout;
    volRow->addWidget(new QLabel(QStringLiteral("Nom du support :")));
    auto* volEdit = new QLineEdit;
    volEdit->setPlaceholderText(QStringLiteral("ex. PHOTOS-2015 (pour reconnaître le disque)"));
    volEdit->setEnabled(false);   // pertinent seulement pour un support amovible
    volRow->addWidget(volEdit, 1);
    layout->addLayout(volRow);
    connect(removableChk, &QCheckBox::toggled, volEdit, &QLineEdit::setEnabled);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    btns->button(QDialogButtonBox::Ok)->setText(
        localDirs.size() > 1 ? QStringLiteral("Ajouter les %1 dossiers").arg(localDirs.size())
                             : QStringLiteral("Ajouter"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;

    // morfPhoto est l'autorité finale : il valide le périmètre, l'existence de chaque
    // dossier et les droits. Un dossier en échec remonte via onActionResult sans
    // empêcher les autres. Le support amovible et le nom de volume valent pour tous.
    const bool removable = removableChk->isChecked();
    const QString vol = removable ? volEdit->text().trimmed() : QString();
    int sent = 0;
    for (int i = 0; i < table->rowCount(); ++i) {
        const QString server = table->item(i, 1) ? table->item(i, 1)->text().trimmed() : QString();
        if (server.isEmpty())
            continue;
        m_client->addFolder(server, removable, vol);
        ++sent;
    }
    if (sent > 0)
        statusBar()->showMessage(QStringLiteral("%1 dossier(s) envoyé(s) à morfPhoto.").arg(sent), 4000);
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
// editMediaClicked
//
// Régler APRÈS COUP le caractère amovible d'une sélection et le nom de son volume :
// un dossier ordinaire déclaré trop vite peut devenir « archive CD », ou l'inverse.
// -----------------------------------------------------------------------------
void MainWindow::editMediaClicked() {
    const QJsonObject f = selectedFolder();
    if (f.isEmpty()) { statusBar()->showMessage(QStringLiteral("Sélectionner un dossier."), 3000); return; }
    const int id = f.value(QStringLiteral("id")).toInt();
    const bool removableNow = f.value(QStringLiteral("removable")).toInt() == 1;
    const QString volNow = f.value(QStringLiteral("volume_label")).toString();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Support du dossier – PhotoHub"));
    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    auto* info = new QLabel(
        QStringLiteral("Un support amovible (CD/DVD, disque d'archive) : morfPhoto ne "
                       "marque jamais ses photos disparues quand le support est absent. "
                       "Elles restent dans la base et dans les analyses, support éjecté "
                       "et même après un redémarrage."));
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color:#99a1ad;"));
    layout->addWidget(info);

    layout->addWidget(new QLabel(QDir::toNativeSeparators(f.value(QStringLiteral("path")).toString())));

    auto* removableChk = new QCheckBox(QStringLiteral("Support amovible"));
    removableChk->setChecked(removableNow);
    layout->addWidget(removableChk);

    auto* volRow = new QHBoxLayout;
    volRow->addWidget(new QLabel(QStringLiteral("Nom du support :")));
    auto* volEdit = new QLineEdit(volNow);
    volEdit->setPlaceholderText(QStringLiteral("ex. PHOTOS-2015"));
    volEdit->setEnabled(removableNow);
    volRow->addWidget(volEdit, 1);
    layout->addLayout(volRow);
    connect(removableChk, &QCheckBox::toggled, volEdit, &QLineEdit::setEnabled);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;
    const bool removable = removableChk->isChecked();
    m_client->setFolderMedia(id, removable, removable ? volEdit->text().trimmed() : QString());
}

// -----------------------------------------------------------------------------
// toggleAnalyticsExclusionClicked
//
// Sortir (ou réintégrer) une sélection des analyses SANS effacer ses données. Geste
// réversible et non destructif : les photos restent indexées, seuls les agrégats de
// morfAnalytics les ignorent. À distinguer nettement de la suppression.
// -----------------------------------------------------------------------------
void MainWindow::toggleAnalyticsExclusionClicked() {
    const QJsonObject f = selectedFolder();
    if (f.isEmpty()) { statusBar()->showMessage(QStringLiteral("Sélectionner un dossier."), 3000); return; }
    const int id = f.value(QStringLiteral("id")).toInt();
    const bool excluded = f.value(QStringLiteral("analytics_excluded")).toInt() == 1;
    m_client->setFolderAnalyticsExcluded(id, !excluded);
}

// -----------------------------------------------------------------------------
// showPurgeDialog
//
// Suppression DÉFINITIVE de données côté morfPhoto (irréversible), sélective ou
// totale. Distincte du retrait doux (qui conserve l'historique) : ici les lignes
// sont vraiment effacées. On récupère d'abord les années et boîtiers connus pour
// proposer des choix, puis on confronte l'utilisateur à une confirmation ferme.
// -----------------------------------------------------------------------------
void MainWindow::showPurgeDialog() {
    if (!m_client->hasBase()) {
        statusBar()->showMessage(QStringLiteral("Aucun morfPhoto sélectionné."), 4000);
        return;
    }
    const QJsonObject sel = selectedFolder();
    // Récupérer années puis boîtiers avant d'ouvrir le dialogue (choix pré-remplis).
    m_client->fetchYears([this, sel](const QJsonArray& years) {
        m_client->fetchCameras([this, sel, years](const QJsonArray& cameras) {
            buildPurgeDialog(sel, years, cameras);
        });
    });
}

void MainWindow::buildPurgeDialog(const QJsonObject& sel, const QJsonArray& years,
                                  const QJsonArray& cameras) {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Supprimer des données – PhotoHub"));
    dlg.resize(560, 380);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    auto* warn = new QLabel(QStringLiteral(
        "<span style='color:#e0534e;'><b>Suppression définitive.</b></span> Contrairement "
        "au retrait d'un dossier (réversible, historique conservé), cette action <b>efface "
        "réellement</b> les photos choisies de la base de morfPhoto. Les fichiers d'origine "
        "sur le disque ou le CD ne sont pas touchés ; seules les données indexées disparaissent."));
    warn->setWordWrap(true);
    layout->addWidget(warn);

    // Choix de la portée.
    auto* group = new QButtonGroup(&dlg);
    auto* rbFolder = new QRadioButton;
    if (!sel.isEmpty())
        rbFolder->setText(QStringLiteral("Le dossier sélectionné : %1")
                              .arg(sel.value(QStringLiteral("path")).toString()));
    else
        rbFolder->setText(QStringLiteral("Le dossier sélectionné (aucun sélectionné)"));
    rbFolder->setEnabled(!sel.isEmpty());
    auto* rbYear   = new QRadioButton(QStringLiteral("Une année de prise de vue :"));
    auto* rbCamera = new QRadioButton(QStringLiteral("Un boîtier :"));
    auto* rbAll    = new QRadioButton(QStringLiteral("Toutes les données (remise à zéro complète)"));
    group->addButton(rbFolder); group->addButton(rbYear);
    group->addButton(rbCamera); group->addButton(rbAll);

    // Année : liste déroulante des années connues.
    auto* yearCombo = new QComboBox;
    for (const QJsonValue& v : years) {
        const QJsonObject o = v.toObject();
        const int y = o.value(QStringLiteral("year")).toInt();
        const int c = o.value(QStringLiteral("count")).toInt();
        yearCombo->addItem(QStringLiteral("%1  (%2 photos)").arg(y).arg(c), y);
    }
    yearCombo->setEnabled(false);

    // Boîtier : liste déroulante des boîtiers connus.
    auto* cameraCombo = new QComboBox;
    for (const QJsonValue& v : cameras) {
        const QJsonObject o = v.toObject();
        const QString cam = o.value(QStringLiteral("camera")).toString();
        const int c = o.value(QStringLiteral("count")).toInt();
        cameraCombo->addItem(QStringLiteral("%1  (%2 photos)").arg(cam).arg(c), cam);
    }
    cameraCombo->setEnabled(false);

    layout->addWidget(rbFolder);
    auto* yearRow = new QHBoxLayout;
    yearRow->addWidget(rbYear);
    yearRow->addWidget(yearCombo, 1);
    layout->addLayout(yearRow);
    auto* camRow = new QHBoxLayout;
    camRow->addWidget(rbCamera);
    camRow->addWidget(cameraCombo, 1);
    layout->addLayout(camRow);
    layout->addWidget(rbAll);

    // Confirmation ferme pour la remise à zéro totale : saisir SUPPRIMER.
    auto* confirmRow = new QHBoxLayout;
    auto* confirmLabel = new QLabel(QStringLiteral("Pour tout supprimer, saisir SUPPRIMER :"));
    auto* confirmEdit = new QLineEdit;
    confirmEdit->setEnabled(false);
    confirmRow->addWidget(confirmLabel);
    confirmRow->addWidget(confirmEdit, 1);
    layout->addLayout(confirmRow);

    layout->addStretch(1);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto* okBtn = btns->button(QDialogButtonBox::Ok);
    okBtn->setText(QStringLiteral("Supprimer définitivement"));
    okBtn->setEnabled(false);
    layout->addWidget(btns);

    // Active les contrôles liés à la portée choisie et la garde de validité de l'OK.
    const auto refresh = [&]() {
        yearCombo->setEnabled(rbYear->isChecked());
        cameraCombo->setEnabled(rbCamera->isChecked());
        confirmEdit->setEnabled(rbAll->isChecked());
        bool ready = false;
        if (rbFolder->isChecked() && !sel.isEmpty()) ready = true;
        else if (rbYear->isChecked() && yearCombo->count() > 0) ready = true;
        else if (rbCamera->isChecked() && cameraCombo->count() > 0) ready = true;
        else if (rbAll->isChecked()) ready = (confirmEdit->text() == QLatin1String("SUPPRIMER"));
        okBtn->setEnabled(ready);
    };
    connect(rbFolder, &QRadioButton::toggled, &dlg, [refresh]() { refresh(); });
    connect(rbYear,   &QRadioButton::toggled, &dlg, [refresh]() { refresh(); });
    connect(rbCamera, &QRadioButton::toggled, &dlg, [refresh]() { refresh(); });
    connect(rbAll,    &QRadioButton::toggled, &dlg, [refresh]() { refresh(); });
    connect(confirmEdit, &QLineEdit::textChanged, &dlg, [refresh]() { refresh(); });
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    // Dernier garde-fou explicite avant le geste irréversible.
    QString what;
    QString scope;
    QVariant value;
    if (rbFolder->isChecked()) {
        scope = QStringLiteral("folder");
        value = sel.value(QStringLiteral("id")).toInt();
        what = QStringLiteral("le dossier « %1 »").arg(sel.value(QStringLiteral("path")).toString());
    } else if (rbYear->isChecked()) {
        scope = QStringLiteral("year");
        value = yearCombo->currentData().toInt();
        what = QStringLiteral("l'année %1").arg(yearCombo->currentData().toInt());
    } else if (rbCamera->isChecked()) {
        scope = QStringLiteral("camera");
        value = cameraCombo->currentData().toString();
        what = QStringLiteral("le boîtier « %1 »").arg(cameraCombo->currentData().toString());
    } else {
        scope = QStringLiteral("all");
        what = QStringLiteral("TOUTES les données photo");
    }
    const auto answer = QMessageBox::warning(this, QStringLiteral("Suppression définitive"),
        QStringLiteral("Supprimer définitivement %1 de morfPhoto ?\n\n"
                       "Cette action est irréversible.").arg(what),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        m_client->purge(scope, value);
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

void MainWindow::showContextDialog() {
    if (!m_client->hasBase()) {
        QMessageBox::information(this, QStringLiteral("Contextes photographiques"),
            QStringLiteral("Aucun morfPhoto sélectionné. Choisissez-en un dans la barre de "
                           "PhotoHub, puis réessayez."));
        return;
    }
    // Écran dédié (client de GET /contexts et PUT /context). morfPhoto reste l'unique
    // écrivain du `.morfphoto.json` ; PhotoHub ne fait que déclarer le contexte.
    ContextDialog dlg(m_client, this);
    dlg.exec();
}

} // namespace photohub
