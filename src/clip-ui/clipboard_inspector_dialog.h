#pragma once

#include "clip-ui/ipc_async_runner.h"

#include <QCborMap>
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
class QWidget;

namespace pastetry {

class ClipboardInspectorDialog : public QDialog {
    Q_OBJECT

public:
    enum class SourceMode {
        Clipboard = 0,
        StoredEntry = 1,
    };

    explicit ClipboardInspectorDialog(IpcAsyncRunner *ipcRunner, QWidget *parent = nullptr);

    void inspectClipboard();
    void inspectEntry(qint64 entryId);
    void refresh();

private:
    struct FormatSnapshot {
        QString mimeType;
        qint64 byteSize = 0;
        QString blobHash;

        bool payloadFetched = false;
        bool payloadLoading = false;
        QByteArray payload;
        bool payloadTruncated = false;
        qint64 payloadOriginalSize = 0;

        QString kind;
        QString textPreview;
        QStringList urlPreview;
        QImage imagePreview;
        bool imageLoading = false;
        bool textTruncated = false;
    };

    SourceMode currentSourceMode() const;
    void setSourceMode(SourceMode mode);

    QClipboard::Mode currentClipboardMode() const;

    void refreshClipboard();
    void refreshStoredEntry();

    void clearFormats();
    void clearDetails();
    void appendSnapshotRow(int row, const FormatSnapshot &snapshot);

    void updateSummaryLabelClipboard(const QMimeData *mimeData,
                                     QClipboard::Mode mode);
    void updateSummaryLabelStoredEntry(const QCborMap &detail,
                                       int formatCount);

    FormatSnapshot buildSnapshotFromClipboard(const QString &mimeType,
                                              const QMimeData *mimeData) const;
    void populateSnapshotFromPayload(FormatSnapshot *snapshot) const;

    void requestStoredPayload(int index);
    void requestStoredImagePreview(int index);

    void updateSelectedDetails();

    IpcAsyncRunner *m_ipcRunner = nullptr;
    QClipboard *m_clipboard = nullptr;

    qint64 m_entryId = -1;

    QComboBox *m_sourceModeCombo = nullptr;
    QWidget *m_clipboardModeContainer = nullptr;
    QComboBox *m_clipboardModeCombo = nullptr;

    QLabel *m_summaryLabel = nullptr;
    QWidget *m_entryPreviewContainer = nullptr;
    QPlainTextEdit *m_entryPreview = nullptr;

    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_detailView = nullptr;
    QLabel *m_imagePreviewLabel = nullptr;

    QVector<FormatSnapshot> m_snapshots;
    bool m_entryDetailInFlight = false;
    qint64 m_entryDetailRequestId = 0;
};

}  // namespace pastetry
