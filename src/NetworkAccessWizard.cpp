/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "NetworkAccessWizard.h"
#include "MorfPhotoClient.h"

#include <QWizardPage>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QProcess>
#include <QPointer>
#include <QHash>
#include <QJsonArray>
#include <QHostAddress>

namespace photohub {

// =============================================================================
// Rapport du helper : libelles et conseils
// =============================================================================

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
        {QStringLiteral("share_writable"),          QStringLiteral("Partage accessible en écriture")},
        {QStringLiteral("fstab_configured"),        QStringLiteral("Montage persistant configuré")},
        {QStringLiteral("root_added"),              QStringLiteral("Root ajoutée à morfPhoto")},
        {QStringLiteral("json_valid"),              QStringLiteral("Configuration morfPhoto valide")},
        {QStringLiteral("service_restarted"),       QStringLiteral("morfPhoto redémarré")},
        {QStringLiteral("service_active"),          QStringLiteral("Service opérationnel")},
        {QStringLiteral("root_in_api"),             QStringLiteral("Root confirmée par l'API")},
        {QStringLiteral("helper_startable"),        QStringLiteral("Helper privilégié démarrable")},
        {QStringLiteral("real_root"),               QStringLiteral("Identité root réelle")},
    };
    return labels.value(id, id);
}

QString formatSourceReport(const QJsonObject& report) {
    QStringList lines;
    for (const QJsonValue& v : report.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject s = v.toObject();
        const QString id = s.value(QStringLiteral("id")).toString();
        const bool ok = s.value(QStringLiteral("ok")).toBool();
        QString line = QStringLiteral("%1  %2")
            .arg(stepLabel(id), -38)
            .arg(ok ? QStringLiteral("✓") : QStringLiteral("✗"));
        const QString detail = s.value(QStringLiteral("detail")).toString().trimmed();
        if (!ok && !detail.isEmpty())
            line += QStringLiteral("\n  %1").arg(detail);
        else if (ok && (id == QLatin1String("hostname_normalized")
                        || id == QLatin1String("mountpoint_created")))
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

QString networkErrorAdvice(const QString& code) {
    if (code == QLatin1String("auth_failed"))
        return QStringLiteral(
            "Le mot de passe a été refusé par Windows. Saisissez à nouveau le mot de passe "
            "de session de ce PC (compte Microsoft : le mot de passe Microsoft). Ni le PIN "
            "Windows Hello, ni l'e-mail, ni une passkey.");
    if (code == QLatin1String("account_locked"))
        return QStringLiteral(
            "Le compte Windows est verrouillé ou son mot de passe a expiré. Déverrouillez-le "
            "(ou changez le mot de passe dans Windows), puis réessayez.");
    if (code == QLatin1String("share_not_found"))
        return QStringLiteral(
            "Le serveur joint bien ce PC mais ne trouve pas le partage. Revenez à l'étape "
            "précédente pour le recréer.");
    if (code == QLatin1String("host_unreachable"))
        return QStringLiteral(
            "Le serveur n'atteint pas ce PC. Vérifiez que les deux sont sur le même réseau, "
            "que ce PC est allumé, et que le pare-feu Windows autorise le « Partage de "
            "fichiers et d'imprimantes » (réseau privé). Si l'adresse de ce PC a changé, "
            "relancez l'assistant.");
    if (code == QLatin1String("permission_denied") || code == QLatin1String("share_not_writable"))
        return QStringLiteral(
            "Le compte est reconnu mais n'a pas les droits voulus sur le dossier. Revenez à "
            "l'étape précédente pour recréer le partage avec les bons droits, ou décochez "
            "« Source qualifiable » pour un accès en lecture seule.");
    if (code == QLatin1String("cifs_utils_missing"))
        return QStringLiteral(
            "Le paquet cifs-utils manque sur le serveur. Sur le serveur : "
            "sudo apt install -y cifs-utils, puis réessayez.");
    return {};
}

// =============================================================================
// Etat partage entre les pages
// =============================================================================

namespace {

struct State {
    NetworkAccessContext ctx;
    QStringList roots;          // dossiers locaux proposes (mappages + ajouts)
    QString root;               // dossier choisi
    bool    writable = true;    // source qualifiable
    QString mountpoint;         // renseigne apres le montage
    QString usedHost;           // nom ou IP avec lequel le serveur a monte le partage

    QString share() const { return ctx.shareNameFor ? ctx.shareNameFor(root) : QString(); }
};

// Page « action » : lance une verification a l'arrivee, n'autorise « Suivant »
// qu'une fois celle-ci reussie, propose « Réessayer » sinon. Pas de moc necessaire.
class ActionPage : public QWizardPage {
public:
    ActionPage(const QString& title, const QString& subTitle) {
        setTitle(title);
        setSubTitle(subTitle);
        m_lay = new QVBoxLayout(this);
        m_log = new QPlainTextEdit;
        m_log->setReadOnly(true);
        m_log->setFont(QFont(QStringLiteral("Consolas"), 9));
        m_lay->addWidget(m_log, 1);
        m_msg = new QLabel;
        m_msg->setWordWrap(true);
        m_lay->addWidget(m_msg);
        m_retry = new QPushButton(QStringLiteral("Réessayer"));
        m_retry->hide();
        m_lay->addWidget(m_retry, 0, Qt::AlignLeft);
        connect(m_retry, &QPushButton::clicked, this, [this]() { start(); });
    }
    void initializePage() override { start(); }
    bool isComplete() const override { return m_done; }

protected:
    virtual void run() = 0;
    void log(const QString& s) { m_log->appendPlainText(s); }
    void finish(bool ok, const QString& msg = {}) {
        m_done = ok;
        m_retry->setVisible(!ok);
        m_msg->setStyleSheet(ok ? QStringLiteral("color:#2e9d57;") : QStringLiteral("color:#d9534f;"));
        m_msg->setText(msg);
        emit completeChanged();
    }
    void start() {
        m_done = false;
        m_retry->hide();
        m_msg->clear();
        m_log->clear();
        emit completeChanged();
        run();
    }

    QVBoxLayout*    m_lay;
    QPlainTextEdit* m_log;
    QLabel*         m_msg;
    QPushButton*    m_retry;
    bool            m_done = false;
};

// ---------------------------------------------------------------------------
// 1. Dossier a partager
// ---------------------------------------------------------------------------
class FolderPage : public QWizardPage {
public:
    explicit FolderPage(State* s) : m_s(s) {
        setTitle(QStringLiteral("Quel dossier de photos partager ?"));
        setSubTitle(QStringLiteral(
            "PhotoHub prépare tout : le partage sur ce PC, puis l'accès depuis le serveur "
            "morfPhoto. Vous n'avez qu'à répondre aux questions."));
        auto* lay = new QVBoxLayout(this);

        const NetworkAccessContext& c = s->ctx;
        auto* detected = new QLabel(QStringLiteral(
            "Détecté : PC <b>%1</b> · adresse <b>%2</b> · compte Windows <b>%3</b>")
            .arg(c.pcName, c.lanIp.isEmpty() ? QStringLiteral("(introuvable)") : c.lanIp,
                 c.winUser.isEmpty() ? QStringLiteral("(introuvable)") : c.winUser));
        detected->setWordWrap(true);
        lay->addWidget(detected);

        auto* row = new QHBoxLayout;
        m_combo = new QComboBox;
        row->addWidget(m_combo, 1);
        auto* browse = new QPushButton(QStringLiteral("Parcourir…"));
        row->addWidget(browse);
        lay->addLayout(row);

        m_writable = new QCheckBox(QStringLiteral("Source qualifiable (recommandé)"));
        m_writable->setChecked(true);
        lay->addWidget(m_writable);
        auto* hint = new QLabel(QStringLiteral(
            "Permet à morfPhoto d'écrire le petit fichier de contexte "
            "<code>.morfphoto.json</code> à côté de vos photos (jamais les photos "
            "elles-mêmes). Décochez pour une archive à garder strictement en lecture seule."));
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#99a1ad;"));
        lay->addWidget(hint);
        auto* tip = new QLabel(QStringLiteral(
            "<b>Conseil :</b> dans l'interface de votre box, réservez une <b>adresse fixe</b> "
            "(réservation DHCP) à ce PC et au serveur morfPhoto. Les photos restent ainsi "
            "accessibles après un redémarrage de la box ou d'un poste."));
        tip->setWordWrap(true);
        tip->setStyleSheet(QStringLiteral("color:#99a1ad;"));
        lay->addWidget(tip);
        lay->addStretch(1);

        for (const auto& m : c.mappings)
            addRoot(m.first);
        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString d = QFileDialog::getExistingDirectory(
                this, QStringLiteral("Dossier de photos à partager"));
            if (!d.isEmpty())
                addRoot(d);
        });
        connect(m_combo, &QComboBox::currentIndexChanged, this, [this](int) { emit completeChanged(); });
    }

    bool isComplete() const override { return m_combo->currentIndex() >= 0; }
    bool validatePage() override {
        m_s->root = m_combo->currentData().toString();
        m_s->writable = m_writable->isChecked();
        return true;
    }

private:
    void addRoot(const QString& local) {
        const QString key = QDir::fromNativeSeparators(local);
        int i = m_combo->findData(key);
        if (i < 0) {
            m_combo->addItem(QDir::toNativeSeparators(local), key);
            i = m_combo->count() - 1;
        }
        m_combo->setCurrentIndex(i);
    }
    State* m_s;
    QComboBox* m_combo;
    QCheckBox* m_writable;
};

// ---------------------------------------------------------------------------
// 2. Le serveur morfPhoto est-il pret ?
// ---------------------------------------------------------------------------
class ServerPage : public ActionPage {
public:
    explicit ServerPage(State* s)
        : ActionPage(QStringLiteral("Vérification du serveur morfPhoto"),
                     QStringLiteral("PhotoHub contacte le serveur pour s'assurer qu'il peut "
                                    "monter un partage.")),
          m_s(s) {}

protected:
    void run() override {
        MorfPhotoClient* c = m_s->ctx.client;
        if (!c || !c->hasBase()) {
            finish(false, QStringLiteral(
                "Aucun serveur morfPhoto sélectionné. Choisissez-en un dans la barre de "
                "PhotoHub, fermez l'assistant, puis relancez-le."));
            return;
        }
        log(QStringLiteral("Contact de %1 …").arg(c->baseUrl()));
        QPointer<ServerPage> self(this);
        c->checkSourcesReady([self](bool ok, const QJsonObject& rep) {
            if (!self)
                return;
            const QString steps = formatSourceReport(rep);
            if (!steps.isEmpty())
                self->log(steps);
            if (ok)
                self->finish(true, QStringLiteral("Le serveur est prêt."));
            else
                self->finish(false, rep.value(QStringLiteral("detail")).toString(
                    QStringLiteral("Le serveur morfPhoto ne peut pas monter de partage "
                                   "(helper privilégié). Réinstallez le paquet morfPhoto.")));
        });
    }

private:
    State* m_s;
};

// ---------------------------------------------------------------------------
// 3. Partage Windows : creation elevee, avec retour visible
// ---------------------------------------------------------------------------
class SharePage : public ActionPage {
public:
    explicit SharePage(State* s)
        : ActionPage(QStringLiteral("Partage du dossier sur ce PC"),
                     QStringLiteral("Windows va demander une autorisation (fenêtre bleue). "
                                    "Acceptez-la : la progression s'affiche ci-dessous.")),
          m_s(s) {}

protected:
    void run() override {
#ifndef Q_OS_WIN
        log(QStringLiteral("Création automatique disponible sous Windows uniquement."));
        finish(true, QStringLiteral("Créez le partage « %1 » à la main, puis continuez.").arg(m_s->share()));
#else
        const QString share = m_s->share();
        const QString path  = QDir::toNativeSeparators(m_s->root);
        const QString user  = m_s->ctx.winUser;
        if (user.isEmpty()) {
            finish(false, QStringLiteral("Compte Windows introuvable : impossible de créer le partage."));
            return;
        }
        const auto psq = [](QString s) { return s.replace(QLatin1Char('\''), QStringLiteral("''")); };
        const QString logPath = QDir(QDir::tempPath()).filePath(QStringLiteral("photohub-share.log"));
        const QString scriptPath = QDir(QDir::tempPath()).filePath(QStringLiteral("photohub-share.ps1"));
        QFile::remove(logPath);
        m_logPath = logPath;
        m_offset  = 0;
        m_result.clear();

        // Script ASCII uniquement (Windows PowerShell 5.1 lit mal l'UTF-8 sans BOM) :
        // il ecrit des CODES de progression, que PhotoHub traduit en francais.
        const bool w = m_s->writable;
        const QString script = QStringLiteral(
            "$ErrorActionPreference='Stop'\n"
            "$n='%1'; $p='%2'; $u='%3'; $log='%4'\n"
            "function L($m){ Add-Content -Path $log -Value $m -Encoding UTF8 }\n"
            "try {\n"
            "  if (Get-SmbShare -Name $n -ErrorAction SilentlyContinue) {\n"
            "    L 'STEP:exists'\n"
            "    Grant-SmbShareAccess -Name $n -AccountName $u -AccessRight %5 -Force | Out-Null\n"
            "  } else {\n"
            "    New-SmbShare -Name $n -Path $p %6 $u | Out-Null\n"
            "    L 'STEP:created'\n"
            "  }\n"
            "%7"
            "  if (-not (Get-SmbShare -Name $n -ErrorAction SilentlyContinue)) { throw 'partage introuvable apres creation' }\n"
            "  L 'OK'\n"
            "} catch { L ('ERR:' + $_.Exception.Message) }\n")
            .arg(psq(share), psq(path), psq(user), psq(QDir::toNativeSeparators(logPath)),
                 w ? QStringLiteral("Change") : QStringLiteral("Read"),
                 w ? QStringLiteral("-ChangeAccess") : QStringLiteral("-ReadAccess"),
                 w ? QStringLiteral("  icacls $p /grant \"$($u):(OI)(CI)M\" | Out-Null\n  L 'STEP:ntfs'\n")
                   : QString());
        QFile f(scriptPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            finish(false, QStringLiteral("Impossible d'écrire le script temporaire."));
            return;
        }
        f.write(script.toUtf8());
        f.close();

        log(QStringLiteral("Création du partage « %1 » (%2)…")
                .arg(share, w ? QStringLiteral("lecture/écriture") : QStringLiteral("lecture seule")));
        log(QStringLiteral("En attente de votre autorisation Windows…"));

        // -Wait : le PowerShell appelant ne rend la main qu'a la fin du script eleve.
        auto* p = new QProcess(this);
        auto* timer = new QTimer(p);
        timer->setInterval(250);
        QPointer<SharePage> self(this);
        connect(timer, &QTimer::timeout, this, [self]() { if (self) self->pollLog(); });
        connect(p, &QProcess::finished, this, [self, p, timer](int code, QProcess::ExitStatus) {
            timer->stop();
            const QString err = QString::fromLocal8Bit(p->readAllStandardError()).trimmed();
            p->deleteLater();
            if (!self)
                return;
            self->pollLog();
            if (self->m_result == QLatin1String("OK"))
                self->finish(true, QStringLiteral("Le partage est prêt."));
            else if (self->m_result.startsWith(QLatin1String("ERR:")))
                self->finish(false, QStringLiteral("Windows a refusé de créer le partage : %1")
                                        .arg(self->m_result.mid(4)));
            else
                self->finish(false, code != 0
                    ? QStringLiteral("L'autorisation Windows a été refusée ou annulée. Cliquez sur "
                                     "« Réessayer » puis acceptez la fenêtre bleue.")
                    : QStringLiteral("Aucun résultat reçu du script de partage. %1").arg(err));
        });
        p->start(QStringLiteral("powershell"), {
            QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            QStringLiteral("Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden "
                           "-ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','\"%1\"'")
                .arg(QDir::toNativeSeparators(scriptPath))});
        timer->start();
#endif
    }

private:
    // Lit les nouvelles lignes du journal ecrit par le script eleve et les affiche.
    void pollLog() {
        QFile f(m_logPath);
        if (!f.open(QIODevice::ReadOnly))
            return;
        f.seek(m_offset);
        const QByteArray chunk = f.readAll();
        // On ne consomme que les lignes completes (le script peut etre en train d'ecrire).
        const int end = chunk.lastIndexOf('\n');
        if (end < 0)
            return;
        m_offset += end + 1;
        for (QString line : QString::fromUtf8(chunk.left(end)).split(QLatin1Char('\n'))) {
            line = line.trimmed();
            if (line.startsWith(QChar(0xFEFF)))
                line.remove(0, 1);
            if (line.isEmpty())
                continue;
            if (line == QLatin1String("STEP:created"))
                log(QStringLiteral("✓ Partage créé"));
            else if (line == QLatin1String("STEP:exists"))
                log(QStringLiteral("✓ Le partage existait déjà : droits mis à jour"));
            else if (line == QLatin1String("STEP:ntfs"))
                log(QStringLiteral("✓ Droits d'écriture accordés sur le dossier"));
            else if (line == QLatin1String("OK"))
                log(QStringLiteral("✓ Partage vérifié")), m_result = line;
            else if (line.startsWith(QLatin1String("ERR:")))
                log(QStringLiteral("✗ %1").arg(line.mid(4))), m_result = line;
        }
    }

    State*  m_s;
    QString m_logPath;
    qint64  m_offset = 0;
    QString m_result;
};

// ---------------------------------------------------------------------------
// 4. Identifiants + montage cote serveur (la boucle qui s'arrete tant que ca echoue)
// ---------------------------------------------------------------------------
class MountPage : public QWizardPage {
public:
    explicit MountPage(State* s) : m_s(s) {
        setTitle(QStringLiteral("Connexion du serveur à ce PC"));
        setSubTitle(QStringLiteral(
            "Le serveur va se connecter au partage avec le compte Windows de ce PC. "
            "Si Windows refuse, l'assistant vous dit pourquoi et vous redonne la main."));
        auto* lay = new QVBoxLayout(this);

        auto* ur = new QHBoxLayout;
        ur->addWidget(new QLabel(QStringLiteral("Nom d'utilisateur Windows :")));
        m_user = new QLineEdit(s->ctx.winUser);
        ur->addWidget(m_user, 1);
        lay->addLayout(ur);
        auto* pr = new QHBoxLayout;
        pr->addWidget(new QLabel(QStringLiteral("Mot de passe :")));
        m_pwd = new QLineEdit;
        m_pwd->setEchoMode(QLineEdit::Password);
        m_pwd->setPlaceholderText(QStringLiteral("mot de passe de session (pas le PIN)"));
        pr->addWidget(m_pwd, 1);
        lay->addLayout(pr);
        auto* hint = new QLabel(QStringLiteral(
            "Le mot de passe est celui qui ouvre votre session Windows (compte Microsoft : "
            "son mot de passe). Ce n'est ni le PIN, ni l'empreinte, ni l'e-mail. Il sert "
            "une seule fois au montage et n'est pas conservé par le service."));
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#99a1ad;"));
        lay->addWidget(hint);

        m_go = new QPushButton(QStringLiteral("Connecter le serveur"));
        m_go->setStyleSheet(QStringLiteral("font-weight:bold; padding:6px;"));
        lay->addWidget(m_go, 0, Qt::AlignLeft);

        m_msg = new QLabel;
        m_msg->setWordWrap(true);
        lay->addWidget(m_msg);
        m_log = new QPlainTextEdit;
        m_log->setReadOnly(true);
        m_log->setFont(QFont(QStringLiteral("Consolas"), 9));
        lay->addWidget(m_log, 1);

        connect(m_go, &QPushButton::clicked, this, [this]() { connectServer(); });
        connect(m_pwd, &QLineEdit::returnPressed, this, [this]() { connectServer(); });
    }

    void initializePage() override {
        m_done = false;
        m_msg->clear();
        m_log->clear();
        m_go->setEnabled(true);
        emit completeChanged();
    }
    bool isComplete() const override { return m_done; }

private:
    void fail(const QString& text, bool clearPwd) {
        m_msg->setStyleSheet(QStringLiteral("color:#d9534f;"));
        m_msg->setText(text);
        m_go->setEnabled(true);
        if (clearPwd) {
            m_pwd->clear();
            m_pwd->setFocus();
        }
    }

    void connectServer() {
        if (!m_go->isEnabled())
            return;
        const QString account = m_user->text().trimmed();
        const QString pwd     = m_pwd->text();
        if (account.isEmpty() || pwd.isEmpty()) {
            fail(QStringLiteral("Renseignez le nom d'utilisateur ET le mot de passe Windows "
                                "(un mot de passe vide est refusé)."), false);
            return;
        }
        if (m_s->ctx.lanIp.isEmpty()) {
            fail(QStringLiteral("Adresse IP de ce PC introuvable : vérifiez la connexion au "
                                "réseau local."), false);
            return;
        }
        m_go->setEnabled(false);
        m_msg->clear();
        m_log->setPlainText(QStringLiteral("Connexion du serveur à ce PC…"));

        // Le NOM du PC d'abord : il survit à un changement d'adresse (DHCP). L'adresse IP
        // ne sert qu'en repli, si le serveur ne sait pas résoudre ce nom.
        QStringList hosts;
        if (!m_s->ctx.pcName.isEmpty())
            hosts << m_s->ctx.pcName;
        hosts << m_s->ctx.lanIp;
        pushTo(hosts, 0, account, pwd);
    }

    void pushTo(const QStringList& hosts, int idx, const QString& account, const QString& pwd) {
        const QString host = hosts.at(idx);
        MorfPhotoClient* c = m_s->ctx.client;
        QPointer<MountPage> self(this);
        c->pushSource(host, m_s->share(), account, pwd, m_s->ctx.pcName, m_s->writable,
            [self, c, hosts, idx, account, pwd, host](bool ok, const QJsonObject& rep) {
                if (!self)
                    return;
                self->m_log->setPlainText(formatSourceReport(rep));
                if (!ok) {
                    const QString code = rep.value(QStringLiteral("code")).toString();
                    // Nom non résolu ou serveur qui ne joint pas ce nom : retenter par l'IP.
                    const bool nameProblem = code == QLatin1String("host_unreachable")
                                             || code == QLatin1String("cifs_mount_failed");
                    if (nameProblem && idx + 1 < hosts.size()) {
                        self->m_log->appendPlainText(QStringLiteral(
                            "\nLe serveur n'a pas pu joindre « %1 » : nouvel essai avec l'adresse IP…")
                                .arg(host));
                        self->pushTo(hosts, idx + 1, account, pwd);
                        return;
                    }
                    const QString advice = networkErrorAdvice(code);
                    const bool pwdProblem = code == QLatin1String("auth_failed")
                                            || code == QLatin1String("account_locked");
                    self->fail(advice.isEmpty()
                                   ? rep.value(QStringLiteral("detail")).toString(
                                         QStringLiteral("Le montage a échoué."))
                                   : advice,
                               pwdProblem);
                    return;
                }
                const QString mountpoint = rep.value(QStringLiteral("mountpoint")).toString();
                const bool waitRestart = rep.value(QStringLiteral("restart_needed")).toBool();
                self->m_s->mountpoint = mountpoint;
                self->m_s->usedHost = host;
                if (waitRestart)
                    self->m_log->appendPlainText(QStringLiteral("\nAttente du redémarrage de morfPhoto…"));
                c->confirmSourceRoot(mountpoint, waitRestart,
                    [self](bool confirmed, const QJsonObject& extra) {
                        if (!self)
                            return;
                        const auto mark = [&](const char* id, const char* key) {
                            self->m_log->appendPlainText(QStringLiteral("%1  %2")
                                .arg(stepLabel(QLatin1String(id)), -38)
                                .arg(extra.value(QLatin1String(key)).toBool()
                                         ? QStringLiteral("✓") : QStringLiteral("✗")));
                        };
                        self->m_log->appendPlainText(QString());
                        mark("service_active", "service_active");
                        mark("root_in_api", "root_in_api");
                        if (!confirmed) {
                            self->fail(QStringLiteral(
                                "Le partage est monté, mais morfPhoto ne l'a pas pris en compte "
                                "(service pas encore opérationnel ou racine absente). Cliquez "
                                "sur « Connecter le serveur » pour réessayer."), false);
                            return;
                        }
                        if (self->m_s->ctx.onMounted)
                            self->m_s->ctx.onMounted(self->m_s->root, self->m_s->mountpoint);
                        self->m_done = true;
                        self->m_msg->setStyleSheet(QStringLiteral("color:#2e9d57;"));
                        self->m_msg->setText(QStringLiteral("Le serveur voit maintenant vos photos."));
                        self->m_pwd->clear();
                        self->emit completeChanged();
                        self->wizard()->next();
                    });
            });
    }

    State*          m_s;
    QLineEdit*      m_user;
    QLineEdit*      m_pwd;
    QPushButton*    m_go;
    QLabel*         m_msg;
    QPlainTextEdit* m_log;
    bool            m_done = false;
};

// ---------------------------------------------------------------------------
// 5. Fin
// ---------------------------------------------------------------------------
class DonePage : public QWizardPage {
public:
    explicit DonePage(State* s) : m_s(s) {
        setTitle(QStringLiteral("C'est prêt"));
        auto* lay = new QVBoxLayout(this);
        m_label = new QLabel;
        m_label->setWordWrap(true);
        lay->addWidget(m_label);
        lay->addStretch(1);
    }
    void initializePage() override {
        m_label->setText(QStringLiteral(
            "Le dossier <b>%1</b> est partagé et le serveur morfPhoto y accède (<code>%2</code>), "
            "y compris après un redémarrage.<br><br>"
            "Il reste à l'ajouter à l'indexation depuis la fenêtre principale de PhotoHub "
            "(« Ajouter un dossier »), puis à lancer l'indexation.<br><br>%3")
            .arg(QDir::toNativeSeparators(m_s->root).toHtmlEscaped(),
                 m_s->mountpoint.toHtmlEscaped(),
                 QHostAddress(m_s->usedHost).isNull()
                     ? QStringLiteral("Le serveur retrouve ce PC par son nom (<b>%1</b>) : un "
                                      "changement d'adresse ne casse rien.").arg(m_s->usedHost.toHtmlEscaped())
                     : QStringLiteral("Le serveur n'a pas su résoudre le nom de ce PC : il utilise "
                                      "son adresse (<b>%1</b>). Si elle change, PhotoHub le signalera "
                                      "et proposera de reconfigurer. Une adresse fixe dans la box "
                                      "évite ce cas.").arg(m_s->usedHost.toHtmlEscaped())));
    }

private:
    State*  m_s;
    QLabel* m_label;
};

} // namespace

// =============================================================================
// Assistant
// =============================================================================

NetworkAccessWizard::NetworkAccessWizard(const NetworkAccessContext& ctx, QWidget* parent)
    : QWizard(parent) {
    setWindowTitle(QStringLiteral("Assistant d'accès réseau – PhotoHub"));
    setWizardStyle(QWizard::ModernStyle);
    resize(720, 520);
    setOption(QWizard::NoCancelButtonOnLastPage, true);
    setOption(QWizard::HaveCustomButton1, true);
    setButtonText(QWizard::CustomButton1, QStringLiteral("Mode manuel…"));
    setButtonText(QWizard::NextButton, QStringLiteral("Suivant >"));
    setButtonText(QWizard::BackButton, QStringLiteral("< Précédent"));
    setButtonText(QWizard::CancelButton, QStringLiteral("Annuler"));
    setButtonText(QWizard::FinishButton, QStringLiteral("Terminer"));
    connect(this, &QWizard::customButtonClicked, this, [this](int) {
        m_manual = true;
        reject();
    });

    // L'etat vit autant que l'assistant (parente a this via un QObject jetable).
    auto* state = new State{ctx};
    connect(this, &QObject::destroyed, [state]() { delete state; });
    for (const auto& m : ctx.mappings)
        state->roots << m.first;

    addPage(new FolderPage(state));
    addPage(new ServerPage(state));
    addPage(new SharePage(state));
    addPage(new MountPage(state));
    addPage(new DonePage(state));
}

} // namespace photohub
