#include "common/clipboard_repository.h"

#include <algorithm>
#include <QDateTime>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace pastetry {
namespace {

constexpr int kMaxSearchLimit = 500;
constexpr int kRegexBoundedRowLimit = 4000;

const QString kSummaryColumns = QStringLiteral(
    "e.id, e.created_at_ms, e.preview, e.source_app, e.pinned, "
    "(SELECT COUNT(*) FROM entry_formats ef WHERE ef.entry_id = e.id) AS format_count, "
    "(SELECT ef2.blob_hash FROM entry_formats ef2 "
    "WHERE ef2.entry_id = e.id AND ef2.mime_type LIKE 'image/%' LIMIT 1) "
    "AS image_blob_hash ");

const QString kPinnedThenRecencyOrder = QStringLiteral(
    "e.pinned DESC, "
    "CASE WHEN e.pinned = 1 THEN e.pin_order ELSE 2147483647 END ASC, "
    "e.created_at_ms DESC");

enum class QueryTokenType {
    Word,
    Quoted,
    LParen,
    RParen,
};

struct QueryToken {
    QueryTokenType type = QueryTokenType::Word;
    QString text;
    int position = 0;
};

struct SqlExpr {
    QString sql;
    QVariantList bindValues;
};

bool isOperatorWord(const QueryToken &token, const QString &op) {
    return token.type == QueryTokenType::Word &&
           token.text.compare(op, Qt::CaseInsensitive) == 0;
}

bool canStartPrimary(const QueryToken &token) {
    if (token.type == QueryTokenType::LParen || token.type == QueryTokenType::Quoted) {
        return true;
    }
    if (token.type == QueryTokenType::Word) {
        return !isOperatorWord(token, QStringLiteral("AND")) &&
               !isOperatorWord(token, QStringLiteral("OR"));
    }
    return false;
}

QStringList extractSearchTerms(const QString &text) {
    static const QRegularExpression tokenRe(QStringLiteral("([\\p{L}\\p{N}_-]+)"));

    QStringList terms;
    const auto matches = tokenRe.globalMatch(text);
    for (auto it = matches; it.hasNext();) {
        const auto match = it.next();
        const QString token = match.captured(1).trimmed();
        if (!token.isEmpty()) {
            terms.push_back(token);
        }
    }
    return terms;
}

QString buildPrefixFtsQuery(const QString &text) {
    const QStringList terms = extractSearchTerms(text);
    if (terms.isEmpty()) {
        return {};
    }

    QStringList parts;
    parts.reserve(terms.size());
    for (const QString &term : terms) {
        parts.push_back(term + QStringLiteral("*"));
    }
    return parts.join(QChar(' '));
}

QString escapeLike(const QString &value) {
    QString escaped = value;
    escaped.replace('\\', QStringLiteral("\\\\"));
    escaped.replace('%', QStringLiteral("\\%"));
    escaped.replace('_', QStringLiteral("\\_"));
    return escaped;
}

bool parsePinnedValue(const QString &text, bool *value) {
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true") ||
        normalized == QStringLiteral("yes") || normalized == QStringLiteral("on")) {
        *value = true;
        return true;
    }
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false") ||
        normalized == QStringLiteral("no") || normalized == QStringLiteral("off")) {
        *value = false;
        return true;
    }
    return false;
}

bool parseDateFilterValue(const QString &text, bool isBefore, qint64 *valueMs,
                          QString *error) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Date value cannot be empty");
        }
        return false;
    }

    const bool dateOnly = !trimmed.contains(QChar('T')) && !trimmed.contains(QChar(' '));
    const QDate date = QDate::fromString(trimmed, Qt::ISODate);
    if (dateOnly && date.isValid()) {
        const QTime time = isBefore ? QTime(23, 59, 59, 999) : QTime(0, 0, 0, 0);
        const QDateTime dt(date, time, Qt::LocalTime);
        *valueMs = dt.toMSecsSinceEpoch();
        return true;
    }

    QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(trimmed, Qt::ISODate);
    }
    if (!dt.isValid()) {
        if (error) {
            *error = QStringLiteral("Invalid date/time '%1' (use ISO-8601)")
                         .arg(trimmed);
        }
        return false;
    }

    *valueMs = dt.toMSecsSinceEpoch();
    return true;
}

EntrySummary readSummary(const QSqlQuery &query) {
    EntrySummary summary;
    summary.id = query.value(0).toLongLong();
    summary.createdAtMs = query.value(1).toLongLong();
    summary.preview = query.value(2).toString();
    summary.sourceApp = query.value(3).toString();
    summary.pinned = query.value(4).toInt() == 1;
    summary.formatCount = query.value(5).toInt();
    summary.imageBlobHash = query.value(6).toString();
    return summary;
}

SearchResult runPagedSummaryQuery(const QSqlDatabase &db, const QString &sql,
                                  const QVariantList &bindValues, int safeCursor,
                                  int safeLimit, QString *error) {
    SearchResult result;

    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant &value : bindValues) {
        query.addBindValue(value);
    }
    query.addBindValue(safeLimit + 1);
    query.addBindValue(safeCursor);

    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return result;
    }

    while (query.next()) {
        result.entries.push_back(readSummary(query));
    }

    if (result.entries.size() > safeLimit) {
        result.entries.removeLast();
        result.nextCursor = safeCursor + safeLimit;
    }

    return result;
}

QVector<EntrySummary> loadSummariesByIds(const QSqlDatabase &db, const QVector<qint64> &ids,
                                         QString *error) {
    QVector<EntrySummary> summaries;
    if (ids.isEmpty()) {
        return summaries;
    }

    QStringList placeholders;
    placeholders.reserve(ids.size());
    for (int i = 0; i < ids.size(); ++i) {
        placeholders.push_back(QStringLiteral("?"));
    }

    const QString sql = QStringLiteral(
                            "SELECT %1 "
                            "FROM entries e "
                            "WHERE e.id IN (%2)")
                            .arg(kSummaryColumns, placeholders.join(','));

    QSqlQuery query(db);
    query.prepare(sql);
    for (qint64 id : ids) {
        query.addBindValue(id);
    }

    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }

    QHash<qint64, EntrySummary> byId;
    while (query.next()) {
        EntrySummary summary = readSummary(query);
        byId.insert(summary.id, std::move(summary));
    }

    summaries.reserve(ids.size());
    for (qint64 id : ids) {
        const auto it = byId.find(id);
        if (it != byId.end()) {
            summaries.push_back(it.value());
        }
    }
    return summaries;
}

QVector<qint64> loadPinnedIdsOrdered(const QSqlDatabase &db, QString *error) {
    QVector<qint64> ids;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "SELECT id FROM entries WHERE pinned = 1 ORDER BY pin_order ASC, created_at_ms DESC, id DESC"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }
    return ids;
}

bool tokenizeAdvancedQuery(const QString &queryText, QVector<QueryToken> *tokens,
                           QString *error) {
    tokens->clear();

    int i = 0;
    while (i < queryText.size()) {
        while (i < queryText.size() && queryText.at(i).isSpace()) {
            ++i;
        }
        if (i >= queryText.size()) {
            break;
        }

        const QChar ch = queryText.at(i);
        if (ch == QChar('(')) {
            tokens->push_back({QueryTokenType::LParen, QStringLiteral("("), i});
            ++i;
            continue;
        }
        if (ch == QChar(')')) {
            tokens->push_back({QueryTokenType::RParen, QStringLiteral(")"), i});
            ++i;
            continue;
        }

        if (ch == QChar('"')) {
            const int start = i;
            ++i;
            QString text;
            bool escaped = false;
            bool closed = false;
            while (i < queryText.size()) {
                const QChar current = queryText.at(i);
                if (escaped) {
                    text.append(current);
                    escaped = false;
                } else if (current == QChar('\\')) {
                    escaped = true;
                } else if (current == QChar('"')) {
                    closed = true;
                    ++i;
                    break;
                } else {
                    text.append(current);
                }
                ++i;
            }

            if (!closed) {
                if (error) {
                    *error = QStringLiteral("Unterminated quoted string");
                }
                return false;
            }

            tokens->push_back({QueryTokenType::Quoted, text, start});
            continue;
        }

        const int start = i;
        while (i < queryText.size()) {
            const QChar c = queryText.at(i);
            if (c.isSpace() || c == QChar('(') || c == QChar(')') || c == QChar('"')) {
                break;
            }
            ++i;
        }
        tokens->push_back({QueryTokenType::Word, queryText.mid(start, i - start), start});
    }

    return true;
}

SqlExpr combineExpr(const QString &op, SqlExpr left, SqlExpr right) {
    SqlExpr combined;
    combined.sql = QStringLiteral("(%1 %2 %3)").arg(left.sql, op, right.sql);
    combined.bindValues = std::move(left.bindValues);
    combined.bindValues.append(right.bindValues);
    return combined;
}

class AdvancedQueryParser {
public:
    explicit AdvancedQueryParser(QVector<QueryToken> tokens)
        : m_tokens(std::move(tokens)) {}

    bool parse(SqlExpr *out, QString *error) {
        if (m_tokens.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Advanced query is empty");
            }
            return false;
        }

        if (!parseOr(out, error)) {
            return false;
        }
        if (!atEnd()) {
            if (error) {
                *error = QStringLiteral("Unexpected token '%1'")
                             .arg(peek()->text);
            }
            return false;
        }
        return true;
    }

private:
    bool parseOr(SqlExpr *out, QString *error) {
        if (!parseAnd(out, error)) {
            return false;
        }
        while (matchKeyword(QStringLiteral("OR"))) {
            SqlExpr rhs;
            if (!parseAnd(&rhs, error)) {
                return false;
            }
            *out = combineExpr(QStringLiteral("OR"), std::move(*out), std::move(rhs));
        }
        return true;
    }

    bool parseAnd(SqlExpr *out, QString *error) {
        if (!parseUnary(out, error)) {
            return false;
        }

        while (!atEnd()) {
            if (matchKeyword(QStringLiteral("AND"))) {
                SqlExpr rhs;
                if (!parseUnary(&rhs, error)) {
                    return false;
                }
                *out = combineExpr(QStringLiteral("AND"), std::move(*out), std::move(rhs));
                continue;
            }

            const QueryToken *token = peek();
            if (!token || token->type == QueryTokenType::RParen ||
                isOperatorWord(*token, QStringLiteral("OR"))) {
                break;
            }
            if (!canStartPrimary(*token)) {
                break;
            }

            SqlExpr rhs;
            if (!parseUnary(&rhs, error)) {
                return false;
            }
            *out = combineExpr(QStringLiteral("AND"), std::move(*out), std::move(rhs));
        }
        return true;
    }

    bool parseUnary(SqlExpr *out, QString *error) {
        if (matchKeyword(QStringLiteral("NOT"))) {
            SqlExpr inner;
            if (!parseUnary(&inner, error)) {
                return false;
            }
            out->sql = QStringLiteral("(NOT %1)").arg(inner.sql);
            out->bindValues = std::move(inner.bindValues);
            return true;
        }
        return parsePrimary(out, error);
    }

    bool parsePrimary(SqlExpr *out, QString *error) {
        const QueryToken *token = peek();
        if (!token) {
            if (error) {
                *error = QStringLiteral("Unexpected end of advanced query");
            }
            return false;
        }

        if (token->type == QueryTokenType::LParen) {
            ++m_index;
            if (!parseOr(out, error)) {
                return false;
            }
            if (atEnd() || peek()->type != QueryTokenType::RParen) {
                if (error) {
                    *error = QStringLiteral("Expected ')'");
                }
                return false;
            }
            ++m_index;
            return true;
        }

        return parseTerm(out, error);
    }

    bool parseTerm(SqlExpr *out, QString *error) {
        if (atEnd()) {
            if (error) {
                *error = QStringLiteral("Expected term");
            }
            return false;
        }

        const QueryToken token = m_tokens.at(m_index++);
        if (token.type == QueryTokenType::RParen) {
            if (error) {
                *error = QStringLiteral("Unexpected ')'");
            }
            return false;
        }

        if (token.type == QueryTokenType::Word &&
            (isOperatorWord(token, QStringLiteral("AND")) ||
             isOperatorWord(token, QStringLiteral("OR")) ||
             isOperatorWord(token, QStringLiteral("NOT")))) {
            if (error) {
                *error = QStringLiteral("Unexpected operator '%1'").arg(token.text);
            }
            return false;
        }

        if (token.type == QueryTokenType::Word) {
            const int colon = token.text.indexOf(QChar(':'));
            if (colon > 0) {
                const QString field = token.text.left(colon).trimmed().toLower();
                QString value = token.text.mid(colon + 1);
                if (value.isEmpty()) {
                    if (atEnd()) {
                        if (error) {
                            *error = QStringLiteral("Field '%1' requires a value")
                                         .arg(field);
                        }
                        return false;
                    }
                    const QueryToken nextToken = m_tokens.at(m_index++);
                    if (nextToken.type == QueryTokenType::LParen ||
                        nextToken.type == QueryTokenType::RParen ||
                        (nextToken.type == QueryTokenType::Word &&
                         (isOperatorWord(nextToken, QStringLiteral("AND")) ||
                          isOperatorWord(nextToken, QStringLiteral("OR")) ||
                          isOperatorWord(nextToken, QStringLiteral("NOT"))))) {
                        if (error) {
                            *error = QStringLiteral("Field '%1' requires a value")
                                         .arg(field);
                        }
                        return false;
                    }
                    value = nextToken.text;
                }
                return compileField(field, value, out, error);
            }
        }

        return compileText(token.text, out, error);
    }

    bool compileText(const QString &text, SqlExpr *out, QString *error) {
        const QString ftsQuery = buildPrefixFtsQuery(text);
        if (ftsQuery.isEmpty()) {
            if (error) {
                *error = QStringLiteral("No searchable text in '%1'").arg(text);
            }
            return false;
        }

        out->sql = QStringLiteral(
            "e.id IN (SELECT rowid FROM entries_fts WHERE entries_fts MATCH ?)");
        out->bindValues = {ftsQuery};
        return true;
    }

    bool compileField(const QString &field, const QString &rawValue, SqlExpr *out,
                      QString *error) {
        const QString value = rawValue.trimmed();
        if (value.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Field '%1' requires a value").arg(field);
            }
            return false;
        }

        if (field == QStringLiteral("app")) {
            out->sql = QStringLiteral("LOWER(e.source_app) LIKE ? ESCAPE '\\'");
            out->bindValues = {QStringLiteral("%") + escapeLike(value.toLower()) +
                               QStringLiteral("%")};
            return true;
        }

        if (field == QStringLiteral("window")) {
            out->sql = QStringLiteral("LOWER(e.source_window) LIKE ? ESCAPE '\\'");
            out->bindValues = {QStringLiteral("%") + escapeLike(value.toLower()) +
                               QStringLiteral("%")};
            return true;
        }

        if (field == QStringLiteral("mime")) {
            out->sql = QStringLiteral(
                "EXISTS (SELECT 1 FROM entry_formats ef "
                "WHERE ef.entry_id = e.id AND LOWER(ef.mime_type) LIKE ? ESCAPE '\\')");
            out->bindValues = {QStringLiteral("%") + escapeLike(value.toLower()) +
                               QStringLiteral("%")};
            return true;
        }

        if (field == QStringLiteral("pinned")) {
            bool pinned = false;
            if (!parsePinnedValue(value, &pinned)) {
                if (error) {
                    *error = QStringLiteral("Invalid pinned value '%1' (use true/false)")
                                 .arg(value);
                }
                return false;
            }
            out->sql = QStringLiteral("e.pinned = ?");
            out->bindValues = {pinned ? 1 : 0};
            return true;
        }

        if (field == QStringLiteral("after")) {
            qint64 valueMs = 0;
            if (!parseDateFilterValue(value, false, &valueMs, error)) {
                return false;
            }
            out->sql = QStringLiteral("e.created_at_ms >= ?");
            out->bindValues = {valueMs};
            return true;
        }

        if (field == QStringLiteral("before")) {
            qint64 valueMs = 0;
            if (!parseDateFilterValue(value, true, &valueMs, error)) {
                return false;
            }
            out->sql = QStringLiteral("e.created_at_ms <= ?");
            out->bindValues = {valueMs};
            return true;
        }

        if (error) {
            *error = QStringLiteral("Unknown advanced field '%1'").arg(field);
        }
        return false;
    }

    bool matchKeyword(const QString &word) {
        const QueryToken *token = peek();
        if (!token || !isOperatorWord(*token, word)) {
            return false;
        }
        ++m_index;
        return true;
    }

    bool atEnd() const {
        return m_index >= m_tokens.size();
    }

    const QueryToken *peek() const {
        if (atEnd()) {
            return nullptr;
        }
        return &m_tokens.at(m_index);
    }

    QVector<QueryToken> m_tokens;
    int m_index = 0;
};

SearchResult runRegexSearch(const QSqlDatabase &db, const SearchRequest &request, int safeCursor,
                            int safeLimit, QString *error) {
    SearchResult result;
    const QString pattern = request.query.trimmed();
    if (pattern.isEmpty()) {
        return runPagedSummaryQuery(
            db,
            QStringLiteral("SELECT %1 FROM entries e "
                           "ORDER BY %2 "
                           "LIMIT ? OFFSET ?")
                .arg(kSummaryColumns, kPinnedThenRecencyOrder),
            {}, safeCursor, safeLimit, error);
    }

    QRegularExpression regex(pattern);
    if (!regex.isValid()) {
        result.queryValid = false;
        result.queryError = QStringLiteral("Regex error at offset %1: %2")
                                .arg(regex.patternErrorOffset())
                                .arg(regex.errorString());
        return result;
    }

    QVector<qint64> pageIds;
    pageIds.reserve(safeLimit + 1);

    const int boundedLimit = request.regexStrict ? -1 : kRegexBoundedRowLimit;
    const int targetMatchCount = safeCursor + safeLimit + 1;
    int matchCount = 0;
    QSqlQuery scan(db);
    QString sql = QStringLiteral("SELECT e.id, e.preview "
                                 "FROM entries e "
                                 "ORDER BY %1")
                      .arg(kPinnedThenRecencyOrder);
    if (boundedLimit >= 0) {
        sql += QStringLiteral(" LIMIT ?");
    }
    scan.prepare(sql);
    if (boundedLimit >= 0) {
        scan.addBindValue(boundedLimit);
    }

    if (!scan.exec()) {
        if (error) {
            *error = scan.lastError().text();
        }
        return result;
    }

    while (scan.next()) {
        const qint64 id = scan.value(0).toLongLong();
        const QString preview = scan.value(1).toString();

        if (!regex.match(preview).hasMatch()) {
            continue;
        }

        if (matchCount >= safeCursor && pageIds.size() < safeLimit + 1) {
            pageIds.push_back(id);
        }
        ++matchCount;
        if (matchCount >= targetMatchCount) {
            break;
        }
    }

    if (pageIds.size() > safeLimit) {
        pageIds.removeLast();
        result.nextCursor = safeCursor + safeLimit;
    }

    result.entries = loadSummariesByIds(db, pageIds, error);
    return result;
}

SearchResult runPlainSearch(const QSqlDatabase &db, const SearchRequest &request, int safeCursor,
                            int safeLimit, QString *error) {
    const QString trimmed = request.query.trimmed();
    if (trimmed.isEmpty()) {
        return runPagedSummaryQuery(
            db,
            QStringLiteral("SELECT %1 FROM entries e "
                           "ORDER BY %2 "
                           "LIMIT ? OFFSET ?")
                .arg(kSummaryColumns, kPinnedThenRecencyOrder),
            {}, safeCursor, safeLimit, error);
    }

    const QString ftsQuery = buildPrefixFtsQuery(trimmed);
    SearchResult result;
    if (ftsQuery.isEmpty()) {
        result.queryValid = false;
        result.queryError = QStringLiteral("Query has no searchable terms");
        return result;
    }

    return runPagedSummaryQuery(
        db,
        QStringLiteral("SELECT %1 "
                       "FROM entries_fts f "
                       "JOIN entries e ON e.id = f.rowid "
                       "WHERE entries_fts MATCH ? "
                       "ORDER BY e.pinned DESC, "
                       "CASE WHEN e.pinned = 1 THEN e.pin_order ELSE 2147483647 END ASC, "
                       "bm25(entries_fts), e.created_at_ms DESC "
                       "LIMIT ? OFFSET ?")
            .arg(kSummaryColumns),
        {ftsQuery}, safeCursor, safeLimit, error);
}

SearchResult runAdvancedSearch(const QSqlDatabase &db, const SearchRequest &request,
                               int safeCursor, int safeLimit, QString *error) {
    const QString trimmed = request.query.trimmed();
    if (trimmed.isEmpty()) {
        return runPagedSummaryQuery(
            db,
            QStringLiteral("SELECT %1 FROM entries e "
                           "ORDER BY %2 "
                           "LIMIT ? OFFSET ?")
                .arg(kSummaryColumns, kPinnedThenRecencyOrder),
            {}, safeCursor, safeLimit, error);
    }

    QVector<QueryToken> tokens;
    SearchResult result;
    QString parseError;
    if (!tokenizeAdvancedQuery(trimmed, &tokens, &parseError)) {
        result.queryValid = false;
        result.queryError = parseError;
        return result;
    }

    AdvancedQueryParser parser(std::move(tokens));
    SqlExpr expr;
    if (!parser.parse(&expr, &parseError)) {
        result.queryValid = false;
        result.queryError = parseError;
        return result;
    }

    return runPagedSummaryQuery(
        db,
        QStringLiteral("SELECT %1 "
                       "FROM entries e "
                       "WHERE %2 "
                       "ORDER BY %3 "
                       "LIMIT ? OFFSET ?")
            .arg(kSummaryColumns, expr.sql, kPinnedThenRecencyOrder),
        expr.bindValues, safeCursor, safeLimit, error);
}

}  // namespace

ClipboardRepository::ClipboardRepository(QString dbPath, QString blobDir,
                                         QString connectionName)
    : m_dbPath(std::move(dbPath)),
      m_connectionName(std::move(connectionName)),
      m_blobStore(std::move(blobDir)) {}

ClipboardRepository::~ClipboardRepository() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    const QString name = m_connectionName;
    m_db = {};
    QSqlDatabase::removeDatabase(name);
}

bool ClipboardRepository::open(QString *error) {
    if (QSqlDatabase::contains(m_connectionName)) {
        m_db = QSqlDatabase::database(m_connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        m_db.setDatabaseName(m_dbPath);
    }

    if (!m_db.open()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA synchronous=NORMAL");
    pragma.exec("PRAGMA temp_store=MEMORY");
    pragma.exec("PRAGMA mmap_size=268435456");

    return true;
}

bool ClipboardRepository::initialize(QString *error) {
    QSqlQuery query(m_db);

    const char *schemaSql[] = {
        "CREATE TABLE IF NOT EXISTS entries ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_at_ms INTEGER NOT NULL,"
        "source_app TEXT NOT NULL DEFAULT '',"
        "source_window TEXT NOT NULL DEFAULT '',"
        "preview TEXT NOT NULL DEFAULT '',"
        "pinned INTEGER NOT NULL DEFAULT 0,"
        "pin_order INTEGER NOT NULL DEFAULT 0"
        ")",
        "CREATE TABLE IF NOT EXISTS entry_formats ("
        "entry_id INTEGER NOT NULL,"
        "mime_type TEXT NOT NULL,"
        "byte_size INTEGER NOT NULL,"
        "blob_hash TEXT NOT NULL,"
        "format_order INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY (entry_id, mime_type),"
        "FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE"
        ")",
        "CREATE TABLE IF NOT EXISTS capture_policy ("
        "id INTEGER PRIMARY KEY CHECK(id = 1),"
        "profile TEXT NOT NULL,"
        "custom_allowlist TEXT NOT NULL DEFAULT '',"
        "max_format_bytes INTEGER NOT NULL,"
        "max_entry_bytes INTEGER NOT NULL"
        ")",
        "INSERT OR IGNORE INTO capture_policy(id, profile, custom_allowlist, "
        "max_format_bytes, max_entry_bytes) "
        "VALUES (1, 'balanced', '', 10485760, 33554432)",
        "CREATE INDEX IF NOT EXISTS idx_entries_created ON entries(created_at_ms DESC)",
        "CREATE INDEX IF NOT EXISTS idx_entries_pinned_order ON entries(pinned, pin_order ASC)",
        "CREATE INDEX IF NOT EXISTS idx_entry_formats_entry ON entry_formats(entry_id)",
        "CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts USING fts5(preview, source_app)",
        "CREATE TRIGGER IF NOT EXISTS entries_ai AFTER INSERT ON entries BEGIN "
        "INSERT INTO entries_fts(rowid, preview, source_app) "
        "VALUES (new.id, new.preview, new.source_app);"
        "END",
        "CREATE TRIGGER IF NOT EXISTS entries_ad AFTER DELETE ON entries BEGIN "
        "DELETE FROM entries_fts WHERE rowid = old.id;"
        "END",
        "CREATE TRIGGER IF NOT EXISTS entries_au AFTER UPDATE ON entries BEGIN "
        "UPDATE entries_fts SET preview = new.preview, source_app = new.source_app "
        "WHERE rowid = new.id;"
        "END",
    };

    for (const auto &sql : schemaSql) {
        if (!query.exec(sql)) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }

    QSqlQuery normalizePolicy(m_db);
    if (!normalizePolicy.exec(
            "UPDATE capture_policy SET custom_allowlist = '' "
            "WHERE custom_allowlist IS NULL")) {
        if (error) {
            *error = normalizePolicy.lastError().text();
        }
        return false;
    }

    QSqlQuery tableInfo(m_db);
    if (!tableInfo.exec("PRAGMA table_info(entry_formats)")) {
        if (error) {
            *error = tableInfo.lastError().text();
        }
        return false;
    }

    bool hasFormatOrder = false;
    while (tableInfo.next()) {
        if (tableInfo.value(1).toString() == QStringLiteral("format_order")) {
            hasFormatOrder = true;
            break;
        }
    }

    if (!hasFormatOrder) {
        QSqlQuery alter(m_db);
        if (!alter.exec(
                "ALTER TABLE entry_formats ADD COLUMN format_order INTEGER NOT NULL DEFAULT 0")) {
            if (error) {
                *error = alter.lastError().text();
            }
            return false;
        }
    }

    QSqlQuery entriesInfo(m_db);
    if (!entriesInfo.exec("PRAGMA table_info(entries)")) {
        if (error) {
            *error = entriesInfo.lastError().text();
        }
        return false;
    }
    bool hasPinOrder = false;
    while (entriesInfo.next()) {
        if (entriesInfo.value(1).toString() == QStringLiteral("pin_order")) {
            hasPinOrder = true;
            break;
        }
    }
    if (!hasPinOrder) {
        QSqlQuery alter(m_db);
        if (!alter.exec(
                "ALTER TABLE entries ADD COLUMN pin_order INTEGER NOT NULL DEFAULT 0")) {
            if (error) {
                *error = alter.lastError().text();
            }
            return false;
        }
    }

    QVector<qint64> pinnedIds = loadPinnedIdsOrdered(m_db, error);
    if (error && !error->isEmpty()) {
        return false;
    }

    if (!m_db.transaction()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    QSqlQuery clearUnpinned(m_db);
    if (!clearUnpinned.exec(QStringLiteral(
            "UPDATE entries SET pin_order = 0 WHERE pinned = 0 AND pin_order != 0"))) {
        m_db.rollback();
        if (error) {
            *error = clearUnpinned.lastError().text();
        }
        return false;
    }

    QSqlQuery assignOrder(m_db);
    assignOrder.prepare("UPDATE entries SET pin_order = ? WHERE id = ?");
    for (int i = 0; i < pinnedIds.size(); ++i) {
        assignOrder.bindValue(0, i + 1);
        assignOrder.bindValue(1, pinnedIds.at(i));
        if (!assignOrder.exec()) {
            m_db.rollback();
            if (error) {
                *error = assignOrder.lastError().text();
            }
            return false;
        }
    }

    if (!m_db.commit()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    return m_blobStore.ensureTables(m_db, error);
}

qint64 ClipboardRepository::insertEntry(const CapturedEntry &entry, QString *error) {
    if (entry.formats.isEmpty()) {
        if (error) {
            *error = "Entry has no formats";
        }
        return -1;
    }

    if (!m_db.transaction()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return -1;
    }

    QSqlQuery insertEntryQuery(m_db);
    insertEntryQuery.prepare(
        "INSERT INTO entries(created_at_ms, source_app, source_window, preview, pinned) "
        "VALUES (?, ?, ?, ?, 0)");
    insertEntryQuery.addBindValue(QDateTime::currentMSecsSinceEpoch());
    insertEntryQuery.addBindValue(entry.sourceApp.isNull() ? QStringLiteral("") : entry.sourceApp);
    insertEntryQuery.addBindValue(entry.sourceWindow.isNull() ? QStringLiteral("") : entry.sourceWindow);
    insertEntryQuery.addBindValue(entry.preview.isNull() ? QStringLiteral("") : entry.preview.left(512));

    if (!insertEntryQuery.exec()) {
        m_db.rollback();
        if (error) {
            *error = insertEntryQuery.lastError().text();
        }
        return -1;
    }

    const qint64 entryId = insertEntryQuery.lastInsertId().toLongLong();

    QSqlQuery insertFormat(m_db);
    insertFormat.prepare(
        "INSERT INTO entry_formats(entry_id, mime_type, byte_size, blob_hash, format_order) "
        "VALUES (?, ?, ?, ?, ?)");

    for (int order = 0; order < entry.formats.size(); ++order) {
        const auto &format = entry.formats.at(order);
        QString blobError;
        const QString hash = m_blobStore.putBlob(m_db, format.data, &blobError);
        if (hash.isEmpty()) {
            m_db.rollback();
            if (error) {
                *error = blobError;
            }
            return -1;
        }

        insertFormat.bindValue(0, entryId);
        insertFormat.bindValue(1, format.mimeType);
        insertFormat.bindValue(2, static_cast<qint64>(format.data.size()));
        insertFormat.bindValue(3, hash);
        insertFormat.bindValue(4, order);

        if (!insertFormat.exec()) {
            m_db.rollback();
            if (error) {
                *error = insertFormat.lastError().text();
            }
            return -1;
        }
    }

    if (!m_db.commit()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return -1;
    }

    return entryId;
}

SearchResult ClipboardRepository::searchEntries(const SearchRequest &request,
                                                QString *error) const {
    const int safeLimit = qBound(1, request.limit, kMaxSearchLimit);
    const int safeCursor = qMax(0, request.cursor);

    switch (request.mode) {
        case SearchMode::Regex:
            return runRegexSearch(m_db, request, safeCursor, safeLimit, error);
        case SearchMode::Advanced:
            return runAdvancedSearch(m_db, request, safeCursor, safeLimit, error);
        case SearchMode::Plain:
        default:
            return runPlainSearch(m_db, request, safeCursor, safeLimit, error);
    }
}

EntryDetail ClipboardRepository::getEntryDetail(qint64 entryId, QString *error) const {
    EntryDetail detail;

    QSqlQuery entryQuery(m_db);
    entryQuery.prepare(
        "SELECT id, created_at_ms, preview, source_app, source_window, pinned "
        "FROM entries WHERE id = ?");
    entryQuery.addBindValue(entryId);

    if (!entryQuery.exec()) {
        if (error) {
            *error = entryQuery.lastError().text();
        }
        return detail;
    }

    if (!entryQuery.next()) {
        if (error) {
            *error = "Entry not found";
        }
        return detail;
    }

    detail.id = entryQuery.value(0).toLongLong();
    detail.createdAtMs = entryQuery.value(1).toLongLong();
    detail.preview = entryQuery.value(2).toString();
    detail.sourceApp = entryQuery.value(3).toString();
    detail.sourceWindow = entryQuery.value(4).toString();
    detail.pinned = entryQuery.value(5).toInt() == 1;

    QSqlQuery formatQuery(m_db);
    formatQuery.prepare(
        "SELECT mime_type, byte_size, blob_hash, format_order "
        "FROM entry_formats WHERE entry_id = ? "
        "ORDER BY format_order ASC, mime_type ASC");
    formatQuery.addBindValue(entryId);
    if (!formatQuery.exec()) {
        if (error) {
            *error = formatQuery.lastError().text();
        }
        return {};
    }

    while (formatQuery.next()) {
        FormatDescriptor format;
        format.mimeType = formatQuery.value(0).toString();
        format.byteSize = formatQuery.value(1).toLongLong();
        format.blobHash = formatQuery.value(2).toString();
        format.formatOrder = formatQuery.value(3).toInt();
        detail.formats.push_back(format);
    }

    return detail;
}

QByteArray ClipboardRepository::loadBlob(const QString &hash, QString *error) const {
    return m_blobStore.loadBlob(hash, error);
}

bool ClipboardRepository::loadCapturePolicy(CapturePolicy *policy, QString *error) const {
    if (!policy) {
        if (error) {
            *error = QStringLiteral("policy output is required");
        }
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT profile, COALESCE(custom_allowlist, ''), max_format_bytes, max_entry_bytes "
        "FROM capture_policy WHERE id = 1");
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("capture_policy row missing");
        }
        return false;
    }

    CapturePolicy loaded;
    loaded.profile = captureProfileFromString(query.value(0).toString());
    loaded.customAllowlistPatterns =
        query.value(1).toString().split('\n', Qt::SkipEmptyParts);
    for (QString &pattern : loaded.customAllowlistPatterns) {
        pattern = pattern.trimmed();
    }
    loaded.maxFormatBytes = query.value(2).toLongLong();
    loaded.maxEntryBytes = query.value(3).toLongLong();

    *policy = loaded;
    return true;
}

bool ClipboardRepository::saveCapturePolicy(const CapturePolicy &policy, QString *error) {
    QStringList customLines;
    customLines.reserve(policy.customAllowlistPatterns.size());
    for (const QString &raw : policy.customAllowlistPatterns) {
        const QString trimmed = raw.trimmed();
        if (!trimmed.isEmpty()) {
            customLines.push_back(trimmed);
        }
    }

    const QString customAllowlistText =
        customLines.isEmpty() ? QStringLiteral("") : customLines.join('\n');

    QSqlQuery query(m_db);
    query.prepare(
        "UPDATE capture_policy SET profile = ?, custom_allowlist = COALESCE(?, ''), "
        "max_format_bytes = ?, max_entry_bytes = ? WHERE id = 1");
    query.addBindValue(captureProfileToString(policy.profile));
    query.addBindValue(customAllowlistText);
    query.addBindValue(policy.maxFormatBytes);
    query.addBindValue(policy.maxEntryBytes);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool ClipboardRepository::setPinned(qint64 entryId, bool pinned, QString *error) {
    QSqlQuery stateQuery(m_db);
    stateQuery.prepare("SELECT pinned, pin_order FROM entries WHERE id = ?");
    stateQuery.addBindValue(entryId);
    if (!stateQuery.exec()) {
        if (error) {
            *error = stateQuery.lastError().text();
        }
        return false;
    }
    if (!stateQuery.next()) {
        if (error) {
            *error = QStringLiteral("Entry not found");
        }
        return false;
    }

    const bool currentlyPinned = stateQuery.value(0).toInt() == 1;
    const int currentOrder = stateQuery.value(1).toInt();

    if (!m_db.transaction()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    if (pinned) {
        QSqlQuery shift(m_db);
        shift.prepare("UPDATE entries SET pin_order = pin_order + 1 WHERE pinned = 1 AND id != ?");
        shift.addBindValue(entryId);
        if (!shift.exec()) {
            m_db.rollback();
            if (error) {
                *error = shift.lastError().text();
            }
            return false;
        }

        QSqlQuery pinQuery(m_db);
        pinQuery.prepare("UPDATE entries SET pinned = 1, pin_order = 1 WHERE id = ?");
        pinQuery.addBindValue(entryId);
        if (!pinQuery.exec()) {
            m_db.rollback();
            if (error) {
                *error = pinQuery.lastError().text();
            }
            return false;
        }
    } else {
        QSqlQuery unpinQuery(m_db);
        unpinQuery.prepare("UPDATE entries SET pinned = 0, pin_order = 0 WHERE id = ?");
        unpinQuery.addBindValue(entryId);
        if (!unpinQuery.exec()) {
            m_db.rollback();
            if (error) {
                *error = unpinQuery.lastError().text();
            }
            return false;
        }

        if (currentlyPinned && currentOrder > 0) {
            QSqlQuery compact(m_db);
            compact.prepare(
                "UPDATE entries SET pin_order = pin_order - 1 "
                "WHERE pinned = 1 AND pin_order > ?");
            compact.addBindValue(currentOrder);
            if (!compact.exec()) {
                m_db.rollback();
                if (error) {
                    *error = compact.lastError().text();
                }
                return false;
            }
        }
    }

    if (!m_db.commit()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    return true;
}

bool ClipboardRepository::movePinnedEntry(qint64 entryId, int targetPinnedIndex, QString *error) {
    QVector<qint64> pinnedIds = loadPinnedIdsOrdered(m_db, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    if (pinnedIds.isEmpty()) {
        if (error) {
            *error = QStringLiteral("No pinned entries");
        }
        return false;
    }

    const int currentIndex = pinnedIds.indexOf(entryId);
    if (currentIndex < 0) {
        if (error) {
            *error = QStringLiteral("Entry is not pinned");
        }
        return false;
    }

    const int clampedTarget = qBound(0, targetPinnedIndex, pinnedIds.size() - 1);
    if (clampedTarget == currentIndex) {
        return true;
    }

    const qint64 movingId = pinnedIds.takeAt(currentIndex);
    pinnedIds.insert(clampedTarget, movingId);

    if (!m_db.transaction()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    QSqlQuery update(m_db);
    update.prepare("UPDATE entries SET pin_order = ? WHERE id = ?");
    for (int i = 0; i < pinnedIds.size(); ++i) {
        update.bindValue(0, i + 1);
        update.bindValue(1, pinnedIds.at(i));
        if (!update.exec()) {
            m_db.rollback();
            if (error) {
                *error = update.lastError().text();
            }
            return false;
        }
    }

    if (!m_db.commit()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }
    return true;
}

qint64 ClipboardRepository::resolveSlotEntry(bool pinnedGroup, int slotOneBased,
                                             QString *error) const {
    if (slotOneBased <= 0) {
        if (error) {
            *error = QStringLiteral("slot must be >= 1");
        }
        return -1;
    }

    QSqlQuery query(m_db);
    if (pinnedGroup) {
        query.prepare(
            "SELECT id FROM entries "
            "WHERE pinned = 1 "
            "ORDER BY pin_order ASC, created_at_ms DESC, id DESC "
            "LIMIT 1 OFFSET ?");
    } else {
        query.prepare(
            "SELECT id FROM entries "
            "WHERE pinned = 0 "
            "ORDER BY created_at_ms DESC, id DESC "
            "LIMIT 1 OFFSET ?");
    }
    query.addBindValue(slotOneBased - 1);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return -1;
    }
    if (!query.next()) {
        return -1;
    }
    return query.value(0).toLongLong();
}

bool ClipboardRepository::cleanupEntryBlobs(qint64 entryId, QString *error) {
    QSqlQuery query(m_db);
    query.prepare("SELECT blob_hash FROM entry_formats WHERE entry_id = ?");
    query.addBindValue(entryId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    QStringList blobHashes;
    while (query.next()) {
        blobHashes.push_back(query.value(0).toString());
    }

    for (const auto &hash : blobHashes) {
        if (!m_blobStore.releaseBlob(m_db, hash, error)) {
            return false;
        }
    }

    return true;
}

bool ClipboardRepository::deleteEntry(qint64 entryId, QString *error) {
    bool wasPinned = false;
    int removedPinOrder = 0;
    QSqlQuery beforeDelete(m_db);
    beforeDelete.prepare("SELECT pinned, pin_order FROM entries WHERE id = ?");
    beforeDelete.addBindValue(entryId);
    if (!beforeDelete.exec()) {
        if (error) {
            *error = beforeDelete.lastError().text();
        }
        return false;
    }
    if (beforeDelete.next()) {
        wasPinned = beforeDelete.value(0).toInt() == 1;
        removedPinOrder = beforeDelete.value(1).toInt();
    }

    if (!m_db.transaction()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    if (!cleanupEntryBlobs(entryId, error)) {
        m_db.rollback();
        return false;
    }

    QSqlQuery del(m_db);
    del.prepare("DELETE FROM entries WHERE id = ?");
    del.addBindValue(entryId);
    if (!del.exec()) {
        m_db.rollback();
        if (error) {
            *error = del.lastError().text();
        }
        return false;
    }

    if (wasPinned && removedPinOrder > 0) {
        QSqlQuery compact(m_db);
        compact.prepare(
            "UPDATE entries SET pin_order = pin_order - 1 "
            "WHERE pinned = 1 AND pin_order > ?");
        compact.addBindValue(removedPinOrder);
        if (!compact.exec()) {
            m_db.rollback();
            if (error) {
                *error = compact.lastError().text();
            }
            return false;
        }
    }

    if (!m_db.commit()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    return true;
}

bool ClipboardRepository::clearHistory(bool keepPinned, QString *error) {
    QSqlQuery query(m_db);
    if (!query.exec(keepPinned ? "SELECT id FROM entries WHERE pinned = 0"
                               : "SELECT id FROM entries")) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    QVector<qint64> ids;
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }

    for (const auto id : ids) {
        if (!deleteEntry(id, error)) {
            return false;
        }
    }

    return true;
}

}  // namespace pastetry
