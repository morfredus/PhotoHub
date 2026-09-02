/*
 * PhotoHub
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QComboBox;
class QTableWidget;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QScrollArea;
class QGridLayout;

namespace photohub {

class MorfPhotoClient;

// -----------------------------------------------------------------------------
// ContextDialog : qualification progressive des journées photographiques
// (contrat morfphoto-context/2).
//
// PhotoHub reste un client : cet écran LIT la liste des répertoires via
// GET /api/v1/contexts et ÉCRIT un contexte via PUT /api/v1/context. morfPhoto
// reste l'unique écrivain du `.morfphoto.json`.
//
// Deux dimensions INDÉPENDANTES présentées séparément : `Contexte` (7 valeurs) et
// `Sujet` (6 valeurs), jamais une seule liste mélangée. Trois états distincts et
// jamais fusionnés : NON QUALIFIÉ (aucun `.morfphoto.json`), QUALIFIÉ (dont
// `INCONNU`, valeur volontaire), INVALIDE (fichier à réparer).
// -----------------------------------------------------------------------------
class ContextDialog : public QDialog {
    Q_OBJECT
public:
    explicit ContextDialog(MorfPhotoClient* client, QWidget* parent = nullptr);

private:
    void buildUi();
    void reload();                                   // recharge selon le filtre courant
    void onRowSelected();                            // remplit l'éditeur depuis la ligne
    void populateEditor(const QJsonObject& row);
    void save();                                     // PUT /api/v1/context
    void goRelative(int delta);                      // Précédent / Suivant
    void loadPreview(const QString& directory);      // vignettes du dossier (via morfPhoto)
    void clearPreview();
    void fillTable(const QJsonArray& items);
    void refreshRowCells(int row);
    void setMessage(const QString& msg, bool error = false);
    QString filterStatus() const;                    // "" | qualified | unqualified | invalid

    MorfPhotoClient* m_client;

    QComboBox*      m_filterCombo = nullptr;
    QTableWidget*   m_table       = nullptr;
    QComboBox*      m_ctxCombo    = nullptr;
    QComboBox*      m_subjCombo   = nullptr;
    QLineEdit*      m_motif       = nullptr;
    QPlainTextEdit* m_desc        = nullptr;
    QLabel*         m_editHeader  = nullptr;
    QPushButton*    m_saveBtn     = nullptr;
    QPushButton*    m_prevBtn     = nullptr;
    QPushButton*    m_nextBtn     = nullptr;
    QLabel*         m_msg         = nullptr;

    // Aperçu : vignettes du dossier courant, servies par morfPhoto (client pur).
    QScrollArea*    m_previewArea    = nullptr;
    QWidget*        m_previewContent = nullptr;
    QGridLayout*    m_previewGrid    = nullptr;
    int             m_previewGen     = 0;   // garde de sequence (reponses async perimees)

    QJsonArray      m_rows;   // aligné sur les lignes du tableau
};

} // namespace photohub
