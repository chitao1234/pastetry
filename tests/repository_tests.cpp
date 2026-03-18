#include "common/clipboard_repository.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace pastetry;

class RepositoryTests : public QObject {
    Q_OBJECT

private slots:
    void insertAndReadBack();
    void searchPaginationAtScale();
};

void RepositoryTests::insertAndReadBack() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-insert");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturedEntry entry;
    entry.preview = "hello world";
    entry.sourceApp = "unit-test";
    entry.formats = {
        CapturedFormat{"text/plain", QByteArray("hello world")},
        CapturedFormat{"text/html", QByteArray("<b>hello world</b>")},
    };

    const qint64 id = repo.insertEntry(entry, &error);
    QVERIFY2(id > 0, qPrintable(error));

    const EntryDetail detail = repo.getEntryDetail(id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(detail.id, id);
    QCOMPARE(detail.preview, QString("hello world"));
    QCOMPARE(detail.formats.size(), 2);

    const QByteArray blob = repo.loadBlob(detail.formats.first().blobHash, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!blob.isEmpty());
}

void RepositoryTests::searchPaginationAtScale() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-scale");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    constexpr int kTotal = 12000;
    for (int i = 0; i < kTotal; ++i) {
        CapturedEntry entry;
        entry.sourceApp = "generator";
        entry.preview = QString("entry %1 token%2").arg(i).arg(i % 5);
        entry.formats = {
            CapturedFormat{"text/plain", entry.preview.toUtf8()},
        };

        const qint64 id = repo.insertEntry(entry, &error);
        QVERIFY2(id > 0, qPrintable(error));
    }

    SearchResult page = repo.searchEntries("token1", 0, 200, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!page.entries.isEmpty());
    QVERIFY(page.nextCursor >= 0);

    SearchResult next = repo.searchEntries("token1", page.nextCursor, 200, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!next.entries.isEmpty());

    for (const auto &entry : page.entries) {
        QVERIFY(entry.preview.contains("token1"));
    }
}

QTEST_APPLESS_MAIN(RepositoryTests)

#include "repository_tests.moc"
