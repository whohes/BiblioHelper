#include "book_groups.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace bh::core {

QString bookKey(const FundSheet &sheet, const FundRecord &record)
{
    const int author = sheet.columnIndex(ColumnRole::AuthorTitle);
    if (author < 0 || author >= record.values.size())
        return {};
    QString title = displayText(record.values.at(author)).toLower();
    title.replace(QChar(0x0451), QChar(0x0435));  // ё → е
    for (QChar &ch : title) {
        if (!ch.isLetterOrNumber())
            ch = QLatin1Char(' ');
    }
    title = title.simplified();
    if (title.isEmpty())
        return {};
    const int year = sheet.columnIndex(ColumnRole::Year);
    const QString yearText = year >= 0 && year < record.values.size() ? displayText(record.values.at(year)).trimmed()
                                                                       : QString();
    return title + QLatin1Char('|') + yearText;
}

QVector<QVector<int>> suggestBookGroups(const FundSheet &sheet)
{
    QHash<QString, QVector<int>> byKey;
    QVector<QString> order;
    for (int r = 0; r < sheet.records.size(); ++r) {
        const QString key = bookKey(sheet, sheet.records.at(r));
        if (key.isEmpty())
            continue;
        auto it = byKey.find(key);
        if (it == byKey.end()) {
            byKey.insert(key, {r});
            order.push_back(key);
        } else {
            it->push_back(r);
        }
    }
    QVector<QVector<int>> groups;
    for (const QString &key : qAsConst(order)) {
        const QVector<int> &rows = byKey[key];
        if (rows.size() < 2)
            continue;
        // Уже одна книга — предлагать нечего.
        const QString first = sheet.records.at(rows.first()).bookId;
        bool same = !first.isEmpty();
        for (int r : rows)
            same = same && sheet.records.at(r).bookId == first;
        if (!same)
            groups.push_back(rows);
    }
    return groups;
}

int countBooks(const FundSheet &sheet, const QVector<int> &records)
{
    int books = 0;
    QSet<QString> seen;
    for (int r : records) {
        if (r < 0 || r >= sheet.records.size())
            continue;
        const QString &book = sheet.records.at(r).bookId;
        if (book.isEmpty()) {
            ++books;
        } else if (!seen.contains(book)) {
            seen.insert(book);
            ++books;
        }
    }
    return books;
}

} // namespace bh::core
