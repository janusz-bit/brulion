#include "log/slowscope.h"

using namespace Qt::StringLiterals;

namespace brulion::logging {

SlowScope::SlowScope(const QLoggingCategory &category, const char *what, int thresholdMs)
    : m_category(category), m_what(what), m_threshold(thresholdMs)
{
    m_timer.start();
}

SlowScope::~SlowScope()
{
    const qint64 ms = m_timer.elapsed();
    const bool slow = ms >= m_threshold;
    if (slow ? !m_category.isInfoEnabled() : !m_category.isDebugEnabled())
        return;
    QString text = u"%1 took %2"_s.arg(QString::fromUtf8(m_what), formatMs(ms));
    if (!m_detail.isEmpty())
        text += u" ("_s + m_detail + u')';
    if (slow)
        QMessageLogger(nullptr, 0, nullptr, m_category.categoryName()).info().noquote() << text;
    else
        QMessageLogger(nullptr, 0, nullptr, m_category.categoryName()).debug().noquote() << text;
}

QString formatBytes(qint64 bytes)
{
    const qint64 b = qMax<qint64>(0, bytes);
    if (b < 1024)
        return u"%1 B"_s.arg(b);
    const double kb = double(b) / 1024.0;
    if (kb < 1024.0)
        return u"%1 KB"_s.arg(kb, 0, 'f', 1);
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return u"%1 MB"_s.arg(mb, 0, 'f', 1);
    return u"%1 GB"_s.arg(mb / 1024.0, 0, 'f', 1);
}

QString formatMs(qint64 ms)
{
    const qint64 m = qMax<qint64>(0, ms);
    if (m < 1000)
        return u"%1 ms"_s.arg(m);
    if (m < 60'000)
        return u"%1 s"_s.arg(double(m) / 1000.0, 0, 'f', 1);
    const qint64 s = m / 1000;
    if (s < 3600)
        return u"%1 min %2 s"_s.arg(s / 60).arg(s % 60, 2, 10, QChar(u'0'));
    return u"%1 h %2 min"_s.arg(s / 3600).arg((s % 3600) / 60, 2, 10, QChar(u'0'));
}

} // namespace brulion::logging
