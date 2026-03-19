#include "common/clipboard_repository.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace pastetry;

class RepositoryTests : public QObject {
    Q_OBJECT

private slots:
    void insertAndReadBack();
    void searchPaginationAtScale();
    void regexSearchValidationAndResults();
    void regexStrictModeFindsOlderMatches();
    void regexStrictModePagination();
    void advancedSearchFiltersAndErrors();
    void preservesFormatInsertionOrder();
    void persistsCapturePolicy();
    void clearHistoryKeepsPinnedWhenRequested();
    void pinnedOrderMoveAndRepin();
    void resolvesRecentNonPinnedSlots();
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

    SearchRequest request;
    request.mode = SearchMode::Plain;
    request.query = "token1";
    request.cursor = 0;
    request.limit = 200;

    SearchResult page = repo.searchEntries(request, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!page.entries.isEmpty());
    QVERIFY(page.nextCursor >= 0);

    request.cursor = page.nextCursor;
    SearchResult next = repo.searchEntries(request, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!next.entries.isEmpty());

    for (const auto &entry : page.entries) {
        QVERIFY(entry.preview.contains("token1"));
    }
}

void RepositoryTests::regexSearchValidationAndResults() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-regex");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturedEntry entry;
    entry.sourceApp = "regex";
    entry.preview = "hello world from pastetry";
    entry.formats = {CapturedFormat{"text/plain", entry.preview.toUtf8()}};
    QVERIFY2(repo.insertEntry(entry, &error) > 0, qPrintable(error));

    SearchRequest validRequest;
    validRequest.mode = SearchMode::Regex;
    validRequest.query = "hello\\s+world";
    validRequest.limit = 50;

    SearchResult valid = repo.searchEntries(validRequest, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(valid.queryValid);
    QCOMPARE(valid.entries.size(), 1);
    QVERIFY(valid.entries.at(0).preview.contains("hello world"));

    SearchRequest invalidRequest = validRequest;
    invalidRequest.query = "([a-z";
    SearchResult invalid = repo.searchEntries(invalidRequest, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!invalid.queryValid);
    QVERIFY(!invalid.queryError.isEmpty());
    QVERIFY(invalid.entries.isEmpty());
}

void RepositoryTests::regexStrictModeFindsOlderMatches() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-regex-strict");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturedEntry oldEntry;
    oldEntry.sourceApp = "generator";
    oldEntry.preview = "needle_old_record";
    oldEntry.formats = {CapturedFormat{"text/plain", oldEntry.preview.toUtf8()}};
    QVERIFY2(repo.insertEntry(oldEntry, &error) > 0, qPrintable(error));

    for (int i = 0; i < 5200; ++i) {
        CapturedEntry entry;
        entry.sourceApp = "generator";
        entry.preview = QString("recent entry %1").arg(i);
        entry.formats = {CapturedFormat{"text/plain", entry.preview.toUtf8()}};
        QVERIFY2(repo.insertEntry(entry, &error) > 0, qPrintable(error));
    }

    SearchRequest bounded;
    bounded.mode = SearchMode::Regex;
    bounded.query = "needle_old_record";
    bounded.limit = 20;
    bounded.regexStrict = false;

    SearchResult boundedResult = repo.searchEntries(bounded, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(boundedResult.queryValid);
    QVERIFY(boundedResult.entries.isEmpty());

    SearchRequest strict = bounded;
    strict.regexStrict = true;
    SearchResult strictResult = repo.searchEntries(strict, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(strictResult.queryValid);
    QCOMPARE(strictResult.entries.size(), 1);
    QCOMPARE(strictResult.entries.first().preview, QString("needle_old_record"));
}

void RepositoryTests::regexStrictModePagination() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-regex-strict-pagination");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    for (int i = 0; i < 5200; ++i) {
        CapturedEntry entry;
        entry.sourceApp = "generator";
        entry.preview = QString("recent entry %1").arg(i);
        if (i % 2 == 0) {
            entry.preview += QStringLiteral(" token_match");
        }
        entry.formats = {CapturedFormat{"text/plain", entry.preview.toUtf8()}};
        QVERIFY2(repo.insertEntry(entry, &error) > 0, qPrintable(error));
    }

    SearchRequest strict;
    strict.mode = SearchMode::Regex;
    strict.query = "token_match$";
    strict.limit = 25;
    strict.regexStrict = true;

    SearchResult firstPage = repo.searchEntries(strict, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(firstPage.queryValid);
    QCOMPARE(firstPage.entries.size(), 25);
    QVERIFY(firstPage.nextCursor >= 0);

    strict.cursor = firstPage.nextCursor;
    SearchResult secondPage = repo.searchEntries(strict, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(secondPage.queryValid);
    QCOMPARE(secondPage.entries.size(), 25);
    QVERIFY(secondPage.nextCursor >= 0);

    for (const EntrySummary &entry : firstPage.entries) {
        QVERIFY(entry.preview.endsWith("token_match"));
    }
    for (const EntrySummary &entry : secondPage.entries) {
        QVERIFY(entry.preview.endsWith("token_match"));
    }
}

void RepositoryTests::advancedSearchFiltersAndErrors() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-advanced");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturedEntry codeEntry;
    codeEntry.sourceApp = "Code";
    codeEntry.sourceWindow = "Editor";
    codeEntry.preview = "alpha beta";
    codeEntry.formats = {
        CapturedFormat{"text/plain", QByteArray("alpha beta")},
        CapturedFormat{"text/html", QByteArray("<b>alpha beta</b>")},
    };
    const qint64 codeId = repo.insertEntry(codeEntry, &error);
    QVERIFY2(codeId > 0, qPrintable(error));

    CapturedEntry termEntry;
    termEntry.sourceApp = "Terminal";
    termEntry.sourceWindow = "Term";
    termEntry.preview = "gamma delta";
    termEntry.formats = {
        CapturedFormat{"text/plain", QByteArray("gamma delta")},
    };
    const qint64 terminalId = repo.insertEntry(termEntry, &error);
    QVERIFY2(terminalId > 0, qPrintable(error));
    QVERIFY2(repo.setPinned(terminalId, true, &error), qPrintable(error));

    SearchRequest filterRequest;
    filterRequest.mode = SearchMode::Advanced;
    filterRequest.query = "app:code AND mime:html AND beta";
    filterRequest.limit = 20;
    SearchResult filtered = repo.searchEntries(filterRequest, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(filtered.queryValid);
    QCOMPARE(filtered.entries.size(), 1);
    QCOMPARE(filtered.entries.first().id, codeId);

    SearchRequest pinnedRequest;
    pinnedRequest.mode = SearchMode::Advanced;
    pinnedRequest.query = "pinned:true AND app:terminal";
    pinnedRequest.limit = 20;
    SearchResult pinnedResult = repo.searchEntries(pinnedRequest, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(pinnedResult.queryValid);
    QCOMPARE(pinnedResult.entries.size(), 1);
    QCOMPARE(pinnedResult.entries.first().id, terminalId);

    SearchRequest invalidRequest;
    invalidRequest.mode = SearchMode::Advanced;
    invalidRequest.query = "app:";
    invalidRequest.limit = 20;
    SearchResult invalid = repo.searchEntries(invalidRequest, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!invalid.queryValid);
    QVERIFY(!invalid.queryError.isEmpty());
}

void RepositoryTests::preservesFormatInsertionOrder() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-format-order");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturedEntry entry;
    entry.sourceApp = "order-test";
    entry.preview = "ordered formats";
    entry.formats = {
        CapturedFormat{"application/x-custom", QByteArray("A")},
        CapturedFormat{"text/plain", QByteArray("B")},
        CapturedFormat{"text/html", QByteArray("<b>C</b>")},
    };

    const qint64 id = repo.insertEntry(entry, &error);
    QVERIFY2(id > 0, qPrintable(error));

    const EntryDetail detail = repo.getEntryDetail(id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(detail.formats.size(), 3);
    QCOMPARE(detail.formats.at(0).mimeType, QString("application/x-custom"));
    QCOMPARE(detail.formats.at(1).mimeType, QString("text/plain"));
    QCOMPARE(detail.formats.at(2).mimeType, QString("text/html"));
    QCOMPARE(detail.formats.at(0).formatOrder, 0);
    QCOMPARE(detail.formats.at(1).formatOrder, 1);
    QCOMPARE(detail.formats.at(2).formatOrder, 2);
}

void RepositoryTests::persistsCapturePolicy() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-policy");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturePolicy policy;
    policy.profile = CaptureProfile::Broad;
    policy.customAllowlistPatterns = {
        QStringLiteral("application/x-special"),
        QStringLiteral("image/*"),
    };
    policy.maxFormatBytes = 5 * 1024 * 1024;
    policy.maxEntryBytes = 25 * 1024 * 1024;
    QVERIFY2(repo.saveCapturePolicy(policy, &error), qPrintable(error));

    CapturePolicy loaded;
    QVERIFY2(repo.loadCapturePolicy(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded.profile, CaptureProfile::Broad);
    QCOMPARE(loaded.customAllowlistPatterns.size(), 2);
    QCOMPARE(loaded.customAllowlistPatterns.at(0), QString("application/x-special"));
    QCOMPARE(loaded.customAllowlistPatterns.at(1), QString("image/*"));
    QCOMPARE(loaded.maxFormatBytes, 5 * 1024 * 1024);
    QCOMPARE(loaded.maxEntryBytes, 25 * 1024 * 1024);

    CapturePolicy emptyAllowlistPolicy = loaded;
    emptyAllowlistPolicy.customAllowlistPatterns.clear();
    QVERIFY2(repo.saveCapturePolicy(emptyAllowlistPolicy, &error), qPrintable(error));

    CapturePolicy loadedEmptyAllowlist;
    QVERIFY2(repo.loadCapturePolicy(&loadedEmptyAllowlist, &error), qPrintable(error));
    QVERIFY(loadedEmptyAllowlist.customAllowlistPatterns.isEmpty());
    QCOMPARE(loadedEmptyAllowlist.profile, emptyAllowlistPolicy.profile);
}

void RepositoryTests::clearHistoryKeepsPinnedWhenRequested() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-clear-history");

    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturedEntry keepEntry;
    keepEntry.sourceApp = QStringLiteral("clear-test");
    keepEntry.preview = QStringLiteral("keep-me");
    keepEntry.formats = {CapturedFormat{"text/plain", QByteArray("keep-me")}};
    const qint64 keepId = repo.insertEntry(keepEntry, &error);
    QVERIFY2(keepId > 0, qPrintable(error));
    QVERIFY2(repo.setPinned(keepId, true, &error), qPrintable(error));

    CapturedEntry dropA;
    dropA.sourceApp = QStringLiteral("clear-test");
    dropA.preview = QStringLiteral("drop-a");
    dropA.formats = {CapturedFormat{"text/plain", QByteArray("drop-a")}};
    const qint64 dropAId = repo.insertEntry(dropA, &error);
    QVERIFY2(dropAId > 0, qPrintable(error));

    CapturedEntry dropB;
    dropB.sourceApp = QStringLiteral("clear-test");
    dropB.preview = QStringLiteral("drop-b");
    dropB.formats = {CapturedFormat{"text/plain", QByteArray("drop-a")}};
    const qint64 dropBId = repo.insertEntry(dropB, &error);
    QVERIFY2(dropBId > 0, qPrintable(error));

    QVERIFY2(repo.clearHistory(true, &error), qPrintable(error));

    SearchRequest afterKeepPinned;
    afterKeepPinned.mode = SearchMode::Plain;
    afterKeepPinned.query = QString();
    afterKeepPinned.limit = 20;
    SearchResult keepPinnedResult = repo.searchEntries(afterKeepPinned, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(keepPinnedResult.entries.size(), 1);
    QCOMPARE(keepPinnedResult.entries.first().id, keepId);
    QVERIFY(keepPinnedResult.entries.first().pinned);

    EntryDetail missingDrop = repo.getEntryDetail(dropAId, &error);
    QCOMPARE(missingDrop.id, qint64(0));
    QVERIFY(!error.isEmpty());

    error.clear();
    missingDrop = repo.getEntryDetail(dropBId, &error);
    QCOMPARE(missingDrop.id, qint64(0));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY2(repo.clearHistory(false, &error), qPrintable(error));
    SearchResult emptyResult = repo.searchEntries(afterKeepPinned, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(emptyResult.entries.isEmpty());
}

void RepositoryTests::pinnedOrderMoveAndRepin() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-pin-order");
    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturedEntry one;
    one.sourceApp = QStringLiteral("pin-order");
    one.preview = QStringLiteral("one");
    one.formats = {CapturedFormat{"text/plain", QByteArray("one")}};
    const qint64 id1 = repo.insertEntry(one, &error);
    QVERIFY2(id1 > 0, qPrintable(error));

    CapturedEntry two;
    two.sourceApp = QStringLiteral("pin-order");
    two.preview = QStringLiteral("two");
    two.formats = {CapturedFormat{"text/plain", QByteArray("two")}};
    const qint64 id2 = repo.insertEntry(two, &error);
    QVERIFY2(id2 > 0, qPrintable(error));

    CapturedEntry three;
    three.sourceApp = QStringLiteral("pin-order");
    three.preview = QStringLiteral("three");
    three.formats = {CapturedFormat{"text/plain", QByteArray("three")}};
    const qint64 id3 = repo.insertEntry(three, &error);
    QVERIFY2(id3 > 0, qPrintable(error));

    QVERIFY2(repo.setPinned(id1, true, &error), qPrintable(error));
    QVERIFY2(repo.setPinned(id2, true, &error), qPrintable(error));
    QVERIFY2(repo.setPinned(id3, true, &error), qPrintable(error));

    QCOMPARE(repo.resolveSlotEntry(true, 1, &error), id3);
    QCOMPARE(repo.resolveSlotEntry(true, 2, &error), id2);
    QCOMPARE(repo.resolveSlotEntry(true, 3, &error), id1);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QVERIFY2(repo.movePinnedEntry(id1, 0, &error), qPrintable(error));
    QCOMPARE(repo.resolveSlotEntry(true, 1, &error), id1);
    QCOMPARE(repo.resolveSlotEntry(true, 2, &error), id3);
    QCOMPARE(repo.resolveSlotEntry(true, 3, &error), id2);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QVERIFY2(repo.setPinned(id3, false, &error), qPrintable(error));
    QCOMPARE(repo.resolveSlotEntry(true, 1, &error), id1);
    QCOMPARE(repo.resolveSlotEntry(true, 2, &error), id2);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QVERIFY2(repo.setPinned(id3, true, &error), qPrintable(error));
    QCOMPARE(repo.resolveSlotEntry(true, 1, &error), id3);
    QCOMPARE(repo.resolveSlotEntry(true, 2, &error), id1);
    QCOMPARE(repo.resolveSlotEntry(true, 3, &error), id2);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void RepositoryTests::resolvesRecentNonPinnedSlots() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipboardRepository repo(dir.filePath("history.sqlite3"), dir.filePath("blobs"),
                             "test-recent-slots");
    QString error;
    QVERIFY2(repo.open(&error), qPrintable(error));
    QVERIFY2(repo.initialize(&error), qPrintable(error));

    CapturedEntry npA;
    npA.sourceApp = QStringLiteral("slots");
    npA.preview = QStringLiteral("np-a");
    npA.formats = {CapturedFormat{"text/plain", QByteArray("np-a")}};
    const qint64 nonPinnedA = repo.insertEntry(npA, &error);
    QVERIFY2(nonPinnedA > 0, qPrintable(error));

    CapturedEntry pB;
    pB.sourceApp = QStringLiteral("slots");
    pB.preview = QStringLiteral("p-b");
    pB.formats = {CapturedFormat{"text/plain", QByteArray("p-b")}};
    const qint64 pinnedB = repo.insertEntry(pB, &error);
    QVERIFY2(pinnedB > 0, qPrintable(error));
    QVERIFY2(repo.setPinned(pinnedB, true, &error), qPrintable(error));

    CapturedEntry npC;
    npC.sourceApp = QStringLiteral("slots");
    npC.preview = QStringLiteral("np-c");
    npC.formats = {CapturedFormat{"text/plain", QByteArray("np-c")}};
    const qint64 nonPinnedC = repo.insertEntry(npC, &error);
    QVERIFY2(nonPinnedC > 0, qPrintable(error));

    CapturedEntry npD;
    npD.sourceApp = QStringLiteral("slots");
    npD.preview = QStringLiteral("np-d");
    npD.formats = {CapturedFormat{"text/plain", QByteArray("np-d")}};
    const qint64 nonPinnedD = repo.insertEntry(npD, &error);
    QVERIFY2(nonPinnedD > 0, qPrintable(error));

    QCOMPARE(repo.resolveSlotEntry(false, 1, &error), nonPinnedD);
    QCOMPARE(repo.resolveSlotEntry(false, 2, &error), nonPinnedC);
    QCOMPARE(repo.resolveSlotEntry(false, 3, &error), nonPinnedA);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(repo.resolveSlotEntry(false, 4, &error), -1);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

QTEST_APPLESS_MAIN(RepositoryTests)

#include "repository_tests.moc"
