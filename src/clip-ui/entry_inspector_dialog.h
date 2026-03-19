#pragma once

#include "common/ipc_client.h"

#include <QByteArray>
#include <QDialog>
#include <QPixmap>
#include <QVector>

class QLabel;
class QPlainTextEdit;
class QTableWidget;

namespace pastetry {

class EntryInspectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit EntryInspectorDialog(IpcClient client, QWidget *parent = nullptr);
    void inspectEntry(qint64 entryId);

private:
    struct FormatSnapshot {
        QString mimeType;
        qint64 byteSize = 0;
        QString blobHash;
        bool payloadFetched = false;
        QByteArray payload;
        bool payloadTruncated = false;
        qint64 payloadOriginalSize = 0;
    };

    void refresh();
    void loadEntryDetail();
    void clearEntry();
    void clearFormatDetails();
    void updateSelectedFormatDetails();
    bool ensureFormatPayloadLoaded(int index, QString *error);
    QByteArray formatPayloadRequest(const FormatSnapshot &snapshot, QString *error,
                                    bool *truncated, qint64 *originalSize) const;
    QPixmap imagePreviewRequest(const QString &blobHash, QString *error) const;

    IpcClient m_client;
    qint64 m_entryId = -1;

    QLabel *m_summaryLabel = nullptr;
    QPlainTextEdit *m_entryPreview = nullptr;
    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_detailView = nullptr;
    QLabel *m_imagePreviewLabel = nullptr;

    QVector<FormatSnapshot> m_formats;
};

}  // namespace pastetry
