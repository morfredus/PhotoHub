/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ContextDialog.h"
#include "MorfPhotoClient.h"

#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QPixmap>
#include <QImage>
#include <QFileInfo>
#include <QJsonDocument>

namespace photohub {

namespace {

// L'objet JSON complet d'une journée, sérialisé sur la cellule de date. Rôle
// dédié (au-delà de Qt::UserRole qui porte déjà le répertoire) : survit au tri
// par en-tête, qui casserait tout index de ligne parallèle.
constexpr int kRowObjectRole = Qt::UserRole + 1;

// Vocabulaires GELÉS du contrat morfphoto-context/2 (mêmes valeurs, même ordre que
// morfPhoto). Un placeholder en tête laisse une journée non qualifiée sans choix forcé.
const QStringList kContexts = {
    QStringLiteral("LIBRE"), QStringLiteral("DECOUVERTE"), QStringLiteral("EVENEMENT"),
    QStringLiteral("SPECTACLE"), QStringLiteral("MISSION"), QStringLiteral("SPECIALISEE"),
    QStringLiteral("INCONNU")};
const QStringList kSubjects = {
    QStringLiteral("GENERAL"), QStringLiteral("PERSONNES"), QStringLiteral("ANIMAUX"),
    QStringLiteral("PAYSAGE"), QStringLiteral("ARCHITECTURE"), QStringLiteral("DETAIL")};

// Libellé lisible d'un statut d'API.
QString statusLabel(const QString& status) {
    if (status == QLatin1String("qualified"))   return QStringLiteral("Qualifié");
    if (status == QLatin1String("invalid"))     return QStringLiteral("Invalide");
    return QStringLiteral("Non qualifié");   // unqualified : aucun .morfphoto.json valide
}

QString orDash(const QJsonValue& v) {
    return (v.isNull() || v.toString().isEmpty()) ? QStringLiteral("—") : v.toString();
}

} // namespace

ContextDialog::ContextDialog(MorfPhotoClient* client, QWidget* parent)
    : QDialog(parent), m_client(client) {
    setWindowTitle(QStringLiteral("Contextes photographiques – PhotoHub"));
    resize(940, 620);
    buildUi();
    reload();
}

void ContextDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(8);

    auto* intro = new QLabel(QStringLiteral(
        "Qualifiez vos journées photographiques. Le contexte est enregistré dans un fichier "
        "<code>.morfphoto.json</code> à côté des photos (morfPhoto l'écrit ; vos fichiers ne "
        "sont jamais modifiés). Vous pouvez en qualifier quelques-unes puis reprendre plus tard."));
    intro->setWordWrap(true);
    root->addWidget(intro);

    // --- Filtre + tableau ---
    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(QStringLiteral("Afficher :")));
    m_filterCombo = new QComboBox;
    m_filterCombo->addItem(QStringLiteral("Toutes les journées"), QString());
    m_filterCombo->addItem(QStringLiteral("Non qualifiées"),       QStringLiteral("unqualified"));
    m_filterCombo->addItem(QStringLiteral("Qualifiées"),           QStringLiteral("qualified"));
    m_filterCombo->addItem(QStringLiteral("Invalides (à réparer)"), QStringLiteral("invalid"));
    filterRow->addWidget(m_filterCombo);
    filterRow->addStretch(1);
    // Réindexer sans quitter l'écran : après un ajout de dossiers, une passe
    // incrémentale puis un rafraîchissement automatique de la liste (voir reindex()).
    m_reindexBtn = new QPushButton(QStringLiteral("Réindexer (incrémental)"));
    m_reindexBtn->setToolTip(QStringLiteral(
        "Demande à morfPhoto une indexation incrémentale, puis recharge la liste "
        "une fois la passe terminée. Utile après avoir ajouté des journées."));
    filterRow->addWidget(m_reindexBtn);
    m_reloadBtn = new QPushButton(QStringLiteral("Rafraîchir"));
    filterRow->addWidget(m_reloadBtn);
    root->addLayout(filterRow);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { reload(); });
    connect(m_reloadBtn, &QPushButton::clicked, this, [this]() { reload(); });
    connect(m_reindexBtn, &QPushButton::clicked, this, &ContextDialog::reindex);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Date"), QStringLiteral("Photos"),
        QStringLiteral("Contexte"), QStringLiteral("Sujet"), QStringLiteral("Statut")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Tri par clic sur n'importe quel en-tête (Date/nom de dossier, nombre de photos,
    // contexte, sujet, statut). Le remplissage désactive ce tri le temps d'insérer les
    // lignes puis le réarme (voir fillTable) : sinon chaque insertion réordonnerait.
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    // Liste à gauche, aperçu du dossier à droite (les vignettes viennent de morfPhoto).
    auto* midRow = new QHBoxLayout;
    midRow->addWidget(m_table, 2);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ContextDialog::onRowSelected);

    auto* previewBox = new QGroupBox(QStringLiteral("Aperçu du dossier"));
    auto* previewLay = new QVBoxLayout(previewBox);
    previewLay->setContentsMargins(6, 6, 6, 6);
    m_previewArea = new QScrollArea;
    m_previewArea->setWidgetResizable(true);
    m_previewContent = new QWidget;
    m_previewGrid = new QGridLayout(m_previewContent);
    m_previewGrid->setContentsMargins(2, 2, 2, 2);
    m_previewGrid->setSpacing(6);
    m_previewArea->setWidget(m_previewContent);
    previewLay->addWidget(m_previewArea);
    midRow->addWidget(previewBox, 1);
    root->addLayout(midRow, 1);

    // --- Éditeur (deux dimensions séparées) ---
    auto* editBox = new QGroupBox(QStringLiteral("Qualifier la journée sélectionnée"));
    auto* editLay = new QVBoxLayout(editBox);
    m_editHeader = new QLabel(QStringLiteral("Sélectionnez une journée dans la liste."));
    m_editHeader->setStyleSheet(QStringLiteral("font-weight:bold;"));
    m_editHeader->setWordWrap(true);
    editLay->addWidget(m_editHeader);

    auto* form = new QFormLayout;
    m_ctxCombo = new QComboBox;
    m_ctxCombo->addItem(QStringLiteral("— à choisir —"), QString());
    for (const QString& c : kContexts)
        m_ctxCombo->addItem(c, c);
    m_subjCombo = new QComboBox;
    m_subjCombo->addItem(QStringLiteral("— à choisir —"), QString());
    for (const QString& s : kSubjects)
        m_subjCombo->addItem(s, s);
    m_motif = new QLineEdit;
    m_motif->setPlaceholderText(QStringLiteral("motif court (ex. Zoo, Cigognes, Demande en mariage)"));
    m_desc = new QPlainTextEdit;
    m_desc->setPlaceholderText(QStringLiteral("description libre (facultatif)"));
    m_desc->setMaximumHeight(70);
    form->addRow(QStringLiteral("Contexte :"), m_ctxCombo);
    form->addRow(QStringLiteral("Sujet :"), m_subjCombo);
    form->addRow(QStringLiteral("Motif :"), m_motif);
    form->addRow(QStringLiteral("Description :"), m_desc);
    editLay->addLayout(form);

    // Aide : distinguer LIBRE de DECOUVERTE, et INCONNU de « non qualifié ».
    auto* help = new QLabel(QStringLiteral(
        "<span style='color:#99a1ad;'>"
        "<b>Contexte</b> = conditions de la séance. <b>Sujet</b> = sujet <b>dominant</b> "
        "(quelques autres sujets dans le lot sont normaux). Les deux sont indépendants "
        "(ex. une visite au zoo = <i>LIBRE + ANIMAUX</i> ; une sortie cigognes préparée = "
        "<i>SPECIALISEE + ANIMAUX</i>).<br>"
        "<b>LIBRE</b> : aucune intention particulière. <b>DECOUVERTE</b> : sortie "
        "volontairement consacrée à explorer un lieu. <b>INCONNU</b> : vous qualifiez la "
        "journée mais laissez volontairement le contexte indéterminé (différent d'une journée "
        "jamais examinée).</span>"));
    help->setWordWrap(true);
    editLay->addWidget(help);

    m_msg = new QLabel;
    m_msg->setWordWrap(true);
    editLay->addWidget(m_msg);

    auto* btns = new QHBoxLayout;
    m_prevBtn = new QPushButton(QStringLiteral("◀ Précédent"));
    m_nextBtn = new QPushButton(QStringLiteral("Suivant ▶"));
    m_saveBtn = new QPushButton(QStringLiteral("Enregistrer"));
    m_saveBtn->setDefault(true);
    btns->addWidget(m_prevBtn);
    btns->addWidget(m_nextBtn);
    btns->addStretch(1);
    btns->addWidget(m_saveBtn);
    editLay->addLayout(btns);
    root->addWidget(editBox);

    connect(m_prevBtn, &QPushButton::clicked, this, [this]() { goRelative(-1); });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() { goRelative(1); });
    connect(m_saveBtn, &QPushButton::clicked, this, &ContextDialog::save);

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(closeBox);

    // Éditeur désactivé tant qu'aucune ligne n'est choisie.
    m_ctxCombo->setEnabled(false);
    m_subjCombo->setEnabled(false);
    m_motif->setEnabled(false);
    m_desc->setEnabled(false);
    m_saveBtn->setEnabled(false);
}

QString ContextDialog::filterStatus() const {
    return m_filterCombo ? m_filterCombo->currentData().toString() : QString();
}

void ContextDialog::reload() {
    setMessage(QStringLiteral("Chargement…"));
    m_client->fetchContexts(filterStatus(), [this](const QJsonArray& items) {
        fillTable(items);
        setMessage(QStringLiteral("%1 journée(s).").arg(items.size()));
    });
}

// Indexation incrémentale déclenchée depuis l'écran, puis rechargement automatique.
// Les contrôles sont verrouillés le temps de la passe (elle peut être longue sur une
// source SMB) ; le client attend la vraie fin de passe avant de rappeler.
void ContextDialog::reindex() {
    if (m_reindexBtn) m_reindexBtn->setEnabled(false);
    if (m_reloadBtn)  m_reloadBtn->setEnabled(false);
    if (m_filterCombo) m_filterCombo->setEnabled(false);
    setMessage(QStringLiteral("Indexation incrémentale en cours… (cela peut prendre un moment)"));
    m_client->reindexAndWait(QStringLiteral("incremental"), [this](bool ok, const QString& err) {
        if (m_reindexBtn) m_reindexBtn->setEnabled(true);
        if (m_reloadBtn)  m_reloadBtn->setEnabled(true);
        if (m_filterCombo) m_filterCombo->setEnabled(true);
        if (!ok) {
            setMessage(QStringLiteral("Indexation : %1").arg(err), true);
            return;
        }
        // Passe terminée : recharger la liste. reload() est asynchrone et réécrit le
        // message (« N journée(s). ») à l'arrivée ; les avertissements éventuels d'une
        // journée restent visibles dans l'info-bulle de sa colonne Statut.
        reload();
    });
}

void ContextDialog::fillTable(const QJsonArray& items) {
    // Tri désarmé pendant l'insertion : sinon chaque setItem réordonnerait la table
    // en pleine construction (lignes qui « sautent »). On le réarme à la fin.
    m_table->setSortingEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(items.size());
    for (int i = 0; i < items.size(); ++i) {
        const QJsonObject o = items.at(i).toObject();
        auto* dateItem = new QTableWidgetItem(o.value(QStringLiteral("date")).toString());
        m_table->setItem(i, 0, dateItem);
        // Nombre de photos porté comme ENTIER (pas comme texte) : le tri de la colonne
        // est alors numérique (125 > 2), pas lexicographique ("125" < "2").
        auto* photos = new QTableWidgetItem;
        photos->setData(Qt::DisplayRole, o.value(QStringLiteral("photo_count")).toInt());
        m_table->setItem(i, 1, photos);
        m_table->setItem(i, 2, new QTableWidgetItem(orDash(o.value(QStringLiteral("context")))));
        m_table->setItem(i, 3, new QTableWidgetItem(orDash(o.value(QStringLiteral("subject")))));
        auto* st = new QTableWidgetItem(statusLabel(o.value(QStringLiteral("status")).toString()));
        // Diagnostic (avertissements / erreur) en info-bulle, sans encombrer la ligne.
        const QString err = o.value(QStringLiteral("error")).toString();
        QStringList warns;
        for (const QJsonValue& w : o.value(QStringLiteral("warnings")).toArray())
            warns << w.toString();
        QString tip;
        if (!err.isEmpty()) tip = err;
        if (!warns.isEmpty()) tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + warns.join(QStringLiteral(", "));
        if (!tip.isEmpty()) st->setToolTip(tip);
        m_table->setItem(i, 4, st);
        // Répertoire (clé du PUT) et objet complet portés en données cachées sur la
        // cellule de date : lecture par rôle, robuste au tri (voir rowObject()).
        dateItem->setData(Qt::UserRole, o.value(QStringLiteral("directory")).toString());
        dateItem->setToolTip(o.value(QStringLiteral("directory")).toString());
        storeRowObject(i, o);
    }
    m_table->setSortingEnabled(true);
    // Tri par défaut : Date (donc nom de dossier daté) décroissant. Les journées
    // récentes remontent en tête, pour retrouver et qualifier tout de suite les
    // derniers dossiers ajoutés au lieu de les chercher au bas d'une longue liste.
    m_table->sortByColumn(0, Qt::DescendingOrder);
    if (items.size() > 0)
        m_table->selectRow(0);
    else
        populateEditor(QJsonObject{});
}

void ContextDialog::onRowSelected() {
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) {
        populateEditor(QJsonObject{});
        return;
    }
    populateEditor(rowObject(row));
}

// Objet JSON d'une ligne, relu depuis la cellule de date (sérialisé en JSON compact).
// La sérialisation garantit le stockage dans QVariant sans dépendre d'un métatype.
QJsonObject ContextDialog::rowObject(int row) const {
    const QTableWidgetItem* it = (row >= 0) ? m_table->item(row, 0) : nullptr;
    if (!it)
        return {};
    return QJsonDocument::fromJson(it->data(kRowObjectRole).toString().toUtf8()).object();
}

void ContextDialog::storeRowObject(int row, const QJsonObject& o) {
    QTableWidgetItem* it = (row >= 0) ? m_table->item(row, 0) : nullptr;
    if (!it)
        return;
    it->setData(kRowObjectRole,
                QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

// Ligne visuelle portant ce répertoire (le tri a pu la déplacer depuis le PUT), -1 sinon.
int ContextDialog::rowForDirectory(const QString& directory) const {
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QTableWidgetItem* it = m_table->item(r, 0);
        if (it && it->data(Qt::UserRole).toString() == directory)
            return r;
    }
    return -1;
}

void ContextDialog::populateEditor(const QJsonObject& row) {
    const bool has = !row.isEmpty();
    m_ctxCombo->setEnabled(has);
    m_subjCombo->setEnabled(has);
    m_motif->setEnabled(has);
    m_desc->setEnabled(has);
    m_saveBtn->setEnabled(has);
    if (!has) {
        m_editHeader->setText(QStringLiteral("Sélectionnez une journée dans la liste."));
        m_ctxCombo->setCurrentIndex(0);
        m_subjCombo->setCurrentIndex(0);
        m_motif->clear();
        m_desc->clear();
        clearPreview();
        return;
    }
    const QString date = row.value(QStringLiteral("date")).toString();
    const int n = row.value(QStringLiteral("photo_count")).toInt();
    m_editHeader->setText(QStringLiteral("%1  ·  %2 photo(s)").arg(date).arg(n));

    // Positionner les combos sur la valeur existante (ou le placeholder si non qualifiée).
    const auto selectValue = [](QComboBox* c, const QString& v) {
        const int idx = v.isEmpty() ? 0 : c->findData(v);
        c->setCurrentIndex(idx < 0 ? 0 : idx);
    };
    selectValue(m_ctxCombo, row.value(QStringLiteral("context")).toString());
    selectValue(m_subjCombo, row.value(QStringLiteral("subject")).toString());
    m_motif->setText(row.value(QStringLiteral("motif")).toString());
    m_desc->setPlainText(row.value(QStringLiteral("description")).toString());
    setMessage(QString());
    loadPreview(row.value(QStringLiteral("directory")).toString());
}

// Vide le panneau d'aperçu (supprime les vignettes de la grille).
void ContextDialog::clearPreview() {
    if (!m_previewGrid)
        return;
    QLayoutItem* it = nullptr;
    while ((it = m_previewGrid->takeAt(0)) != nullptr) {
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
}

// Demande à morfPhoto un échantillon de fichiers du dossier, puis une vignette JPEG
// pour chacun. Asynchrone : une garde de séquence (m_previewGen) ignore les réponses
// d'un dossier qu'on a déjà quitté. PhotoHub ne lit jamais les fichiers lui-même.
void ContextDialog::loadPreview(const QString& directory) {
    clearPreview();
    const int gen = ++m_previewGen;
    if (directory.isEmpty())
        return;
    m_client->fetchDirectorySample(directory, 6, [this, gen](const QStringList& paths) {
        if (gen != m_previewGen)
            return;   // dossier déjà quitté
        if (paths.isEmpty()) {
            auto* lbl = new QLabel(QStringLiteral("Aucun aperçu."));
            lbl->setStyleSheet(QStringLiteral("color:#99a1ad;"));
            m_previewGrid->addWidget(lbl, 0, 0);
            return;
        }
        int i = 0;
        for (const QString& p : paths) {
            const int pos = i++;
            // Emplacement réservé tout de suite (grille 2 colonnes), rempli à l'arrivée.
            auto* holder = new QLabel(QStringLiteral("…"));
            holder->setFixedSize(150, 112);
            holder->setAlignment(Qt::AlignCenter);
            holder->setStyleSheet(QStringLiteral("border:1px solid #2c3037;color:#99a1ad;"));
            holder->setToolTip(QFileInfo(p).fileName());
            m_previewGrid->addWidget(holder, pos / 2, pos % 2);
            m_client->fetchThumbnail(p, [this, gen, holder](const QByteArray& jpeg) {
                if (gen != m_previewGen)
                    return;
                if (jpeg.isEmpty()) {
                    holder->setText(QStringLiteral("(sans aperçu)"));
                    return;
                }
                QImage img;
                if (!img.loadFromData(jpeg)) {
                    holder->setText(QStringLiteral("(illisible)"));
                    return;
                }
                const QPixmap pm = QPixmap::fromImage(img).scaled(
                    150, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                holder->setText(QString());
                holder->setPixmap(pm);
            });
        }
    });
}

void ContextDialog::save() {
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0))
        return;
    // Le répertoire (et non l'index de ligne) est la clé stable : le tri peut déplacer
    // la ligne entre l'envoi et la réponse, on la retrouvera par son dossier.
    const QString directory = m_table->item(row, 0)->data(Qt::UserRole).toString();
    const QString context   = m_ctxCombo->currentData().toString();
    const QString subject   = m_subjCombo->currentData().toString();
    if (context.isEmpty() || subject.isEmpty()) {
        setMessage(QStringLiteral("Choisissez un contexte ET un sujet (les deux sont requis)."), true);
        return;
    }
    m_saveBtn->setEnabled(false);
    setMessage(QStringLiteral("Enregistrement…"));
    m_client->putContext(directory, context, subject, m_motif->text().trimmed(),
                         m_desc->toPlainText().trimmed(),
        [this, directory](bool ok, const QJsonObject& stored, const QString& err) {
            m_saveBtn->setEnabled(true);
            if (!ok) {
                setMessage(QStringLiteral("Échec : %1").arg(err), true);
                return;
            }
            // Fusionner le contexte stocké dans la ligne (garder date/label existants).
            const int r = rowForDirectory(directory);
            if (r >= 0) {
                QJsonObject merged = rowObject(r);
                for (const QString& k : {QStringLiteral("status"), QStringLiteral("context"),
                        QStringLiteral("subject"), QStringLiteral("motif"),
                        QStringLiteral("description"), QStringLiteral("warnings")})
                    merged[k] = stored.value(k);
                storeRowObject(r, merged);
                refreshRowCells(r);
            }
            setMessage(QStringLiteral("Enregistré."));
            // Qualification progressive : enchaîner sur la journée suivante. Repartir de
            // la ligne réellement enregistrée (un éventuel re-tri a pu la déplacer).
            const int r2 = rowForDirectory(directory);
            if (r2 >= 0)
                m_table->selectRow(r2);
            goRelative(1);
        });
}

void ContextDialog::refreshRowCells(int row) {
    if (row < 0 || !m_table->item(row, 2))
        return;
    const QJsonObject o = rowObject(row);
    // Désarmer le tri le temps de réécrire les cellules : modifier une cellule d'une
    // colonne triée réordonnerait la table sous le curseur. L'appelant re-sélectionne
    // ensuite la bonne ligne par répertoire.
    const bool wasSorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);
    m_table->item(row, 2)->setText(orDash(o.value(QStringLiteral("context"))));
    m_table->item(row, 3)->setText(orDash(o.value(QStringLiteral("subject"))));
    m_table->item(row, 4)->setText(statusLabel(o.value(QStringLiteral("status")).toString()));
    m_table->setSortingEnabled(wasSorting);
}

void ContextDialog::goRelative(int delta) {
    const int n = m_table->rowCount();
    if (n == 0)
        return;
    int row = m_table->currentRow();
    if (row < 0)
        row = 0;
    else
        row = qBound(0, row + delta, n - 1);
    m_table->selectRow(row);
}

void ContextDialog::setMessage(const QString& msg, bool error) {
    m_msg->setText(msg);
    m_msg->setStyleSheet(error ? QStringLiteral("color:#c0392b;") : QStringLiteral("color:#99a1ad;"));
}

} // namespace photohub
