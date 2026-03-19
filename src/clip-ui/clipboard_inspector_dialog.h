#pragma once

#include <QClipboard>
#include <QByteArray>
#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

class QComboBox;
class QLabel;
class QMimeData;
class QPlainTextEdit;
class QTableWidget;

namespace pastetry {

class ClipboardInspectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClipboardInspectorDialog(QWidget *parent = nullptr);
    void refresh();

private:
    struct FormatSnapshot {
        QString mimeType;
        QByteArray payload;
        QString kind;
        QString textPreview;
        QStringList urlPreview;
        QImage imagePreview;
        bool textTruncated = false;
    };

    QClipboard::Mode currentMode() const;
    void updateSummaryLabel(const QMimeData *mimeData, QClipboard::Mode mode);
    void clearDetails();
    void updateSelectedDetails();
    void appendSnapshotRow(int row, const FormatSnapshot &snapshot);
    FormatSnapshot buildSnapshot(const QString &mimeType,
                                const QMimeData *mimeData) const;

    QClipboard *m_clipboard = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_detailView = nullptr;
    QLabel *m_imagePreviewLabel = nullptr;

    QVector<FormatSnapshot> m_snapshots;
};

}  // namespace pastetry
