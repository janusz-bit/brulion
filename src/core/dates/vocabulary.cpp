#include "dates/vocabulary_p.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::dates::detail {

namespace {

struct WeekdayForms {
    int day;
    QStringList singular; ///< all grammatical cases
    QStringList plural;
    QStringList abbreviations;
};

QHash<QString, WeekdayWord> buildWeekdayWords()
{
    const QList<WeekdayForms> forms = {
        {1, {u"poniedzialek"_s, u"poniedzialku"_s, u"monday"_s}, {u"poniedzialki"_s, u"poniedzialkow"_s, u"mondays"_s},
         {u"pon"_s, u"pn"_s, u"mon"_s}},
        {2, {u"wtorek"_s, u"wtorku"_s, u"tuesday"_s}, {u"wtorki"_s, u"wtorkow"_s, u"tuesdays"_s},
         {u"wt"_s, u"tue"_s, u"tues"_s}},
        {3, {u"sroda"_s, u"srode"_s, u"srody"_s, u"wednesday"_s}, {u"srod"_s, u"wednesdays"_s}, {u"sr"_s, u"wed"_s}},
        {4, {u"czwartek"_s, u"czwartku"_s, u"thursday"_s}, {u"czwartki"_s, u"czwartkow"_s, u"thursdays"_s},
         {u"czw"_s, u"thu"_s, u"thur"_s, u"thurs"_s}},
        {5, {u"piatek"_s, u"piatku"_s, u"friday"_s}, {u"piatki"_s, u"piatkow"_s, u"fridays"_s}, {u"pt"_s, u"fri"_s}},
        {6, {u"sobota"_s, u"sobote"_s, u"soboty"_s, u"saturday"_s}, {u"sobot"_s, u"saturdays"_s},
         {u"sob"_s, u"sat"_s}},
        {7, {u"niedziela"_s, u"niedziele"_s, u"niedzieli"_s, u"sunday"_s}, {u"niedziel"_s, u"sundays"_s},
         {u"ndz"_s, u"nd"_s, u"niedz"_s, u"sun"_s}},
    };
    QHash<QString, WeekdayWord> map;
    for (const WeekdayForms &f : forms) {
        for (const QString &w : f.singular)
            map.insert(w, {f.day, false, false});
        for (const QString &w : f.plural)
            map.insert(w, {f.day, false, true});
        for (const QString &w : f.abbreviations)
            map.insert(w, {f.day, true, false});
    }
    return map;
}

struct MonthForms {
    int month;
    QStringList polish;
    QStringList english;
};

QHash<QString, MonthWord> buildMonthWords()
{
    const QList<MonthForms> forms = {
        {1, {u"styczen"_s, u"stycznia"_s, u"styczniu"_s, u"sty"_s}, {u"january"_s, u"jan"_s}},
        {2, {u"luty"_s, u"lutego"_s, u"lutym"_s, u"lut"_s}, {u"february"_s, u"feb"_s}},
        {3, {u"marzec"_s, u"marca"_s, u"marcu"_s}, {u"march"_s, u"mar"_s}},
        {4, {u"kwiecien"_s, u"kwietnia"_s, u"kwietniu"_s, u"kwi"_s}, {u"april"_s, u"apr"_s}},
        {5, {u"maj"_s, u"maja"_s, u"maju"_s}, {u"may"_s}},
        {6, {u"czerwiec"_s, u"czerwca"_s, u"czerwcu"_s, u"cze"_s}, {u"june"_s, u"jun"_s}},
        {7, {u"lipiec"_s, u"lipca"_s, u"lipcu"_s, u"lip"_s}, {u"july"_s, u"jul"_s}},
        {8, {u"sierpien"_s, u"sierpnia"_s, u"sierpniu"_s, u"sie"_s}, {u"august"_s, u"aug"_s}},
        {9, {u"wrzesien"_s, u"wrzesnia"_s, u"wrzesniu"_s, u"wrz"_s}, {u"september"_s, u"sept"_s, u"sep"_s}},
        {10, {u"pazdziernik"_s, u"pazdziernika"_s, u"pazdzierniku"_s, u"paz"_s}, {u"october"_s, u"oct"_s}},
        {11, {u"listopad"_s, u"listopada"_s, u"listopadzie"_s, u"lis"_s}, {u"november"_s, u"nov"_s}},
        {12, {u"grudzien"_s, u"grudnia"_s, u"grudniu"_s, u"gru"_s}, {u"december"_s, u"dec"_s}},
    };
    const QStringList abbreviations = {u"sty"_s, u"lut"_s, u"mar"_s, u"kwi"_s, u"cze"_s, u"lip"_s, u"sie"_s,
                                       u"wrz"_s, u"paz"_s, u"lis"_s, u"gru"_s, u"jan"_s, u"feb"_s, u"apr"_s,
                                       u"jun"_s, u"jul"_s, u"aug"_s, u"sep"_s, u"sept"_s, u"oct"_s, u"nov"_s,
                                       u"dec"_s};
    QHash<QString, MonthWord> map;
    for (const MonthForms &f : forms) {
        for (const QString &w : f.polish)
            map.insert(w, {f.month, false, abbreviations.contains(w)});
        for (const QString &w : f.english)
            map.insert(w, {f.month, true, abbreviations.contains(w)});
    }
    return map;
}

QHash<QString, int> buildNumberWords()
{
    return {
        {u"jeden"_s, 1},       {u"jedna"_s, 1},        {u"jedno"_s, 1},         {u"jednego"_s, 1},
        {u"jednej"_s, 1},      {u"dwa"_s, 2},          {u"dwie"_s, 2},          {u"dwoch"_s, 2},
        {u"trzy"_s, 3},        {u"cztery"_s, 4},       {u"piec"_s, 5},          {u"szesc"_s, 6},
        {u"siedem"_s, 7},      {u"osiem"_s, 8},        {u"dziewiec"_s, 9},      {u"dziesiec"_s, 10},
        {u"jedenascie"_s, 11}, {u"dwanascie"_s, 12},   {u"pietnascie"_s, 15},   {u"dwadziescia"_s, 20},
        {u"trzydziesci"_s, 30}, {u"czterdziesci"_s, 40}, {u"one"_s, 1},          {u"two"_s, 2},
        {u"three"_s, 3},       {u"four"_s, 4},         {u"five"_s, 5},          {u"six"_s, 6},
        {u"seven"_s, 7},       {u"eight"_s, 8},        {u"nine"_s, 9},          {u"ten"_s, 10},
        {u"eleven"_s, 11},     {u"twelve"_s, 12},      {u"fifteen"_s, 15},      {u"twenty"_s, 20},
        {u"thirty"_s, 30},     {u"forty"_s, 40},
    };
}

QHash<QString, int> buildOrdinalWords()
{
    return {
        {u"drugi"_s, 2},    {u"druga"_s, 2},    {u"drugie"_s, 2},    {u"drugiego"_s, 2},
        {u"trzeci"_s, 3},   {u"trzecia"_s, 3},  {u"trzecie"_s, 3},   {u"trzeciego"_s, 3},
        {u"czwarty"_s, 4},  {u"czwarta"_s, 4},  {u"czwarte"_s, 4},   {u"czwartego"_s, 4},
        {u"piaty"_s, 5},    {u"piata"_s, 5},    {u"piate"_s, 5},     {u"piatego"_s, 5},
        {u"szosty"_s, 6},   {u"szosta"_s, 6},   {u"szoste"_s, 6},    {u"szostego"_s, 6},
        {u"other"_s, 2},    {u"second"_s, 2},   {u"third"_s, 3},     {u"fourth"_s, 4},
        {u"fifth"_s, 5},    {u"sixth"_s, 6},
    };
}

} // namespace

const QHash<QString, WeekdayWord> &weekdayWords()
{
    static const QHash<QString, WeekdayWord> words = buildWeekdayWords();
    return words;
}

const QHash<QString, MonthWord> &monthWords()
{
    static const QHash<QString, MonthWord> words = buildMonthWords();
    return words;
}

const QHash<QString, int> &numberWords()
{
    static const QHash<QString, int> words = buildNumberWords();
    return words;
}

const QHash<QString, int> &ordinalWords()
{
    static const QHash<QString, int> words = buildOrdinalWords();
    return words;
}

std::optional<WeekdayWord> lookupWeekday(QStringView folded)
{
    const auto it = weekdayWords().constFind(folded.toString());
    return it == weekdayWords().cend() ? std::nullopt : std::optional<WeekdayWord>(*it);
}

std::optional<MonthWord> lookupMonth(QStringView folded)
{
    const auto it = monthWords().constFind(folded.toString());
    return it == monthWords().cend() ? std::nullopt : std::optional<MonthWord>(*it);
}

QString phraseAlternation(const QStringList &phrases)
{
    // Escape each word on its own, then join the words of a phrase with `\s+`.
    QStringList unique = phrases;
    unique.removeDuplicates();
    std::sort(unique.begin(), unique.end(), [](const QString &a, const QString &b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    QStringList parts;
    parts.reserve(unique.size());
    for (const QString &phrase : std::as_const(unique)) {
        QStringList words = phrase.split(u' ', Qt::SkipEmptyParts);
        for (QString &w : words)
            w = QRegularExpression::escape(w);
        parts.append(words.join(uR"(\s+)"_s));
    }
    return parts.join(u'|');
}

QString collapseSpaces(QStringView s)
{
    QString out;
    out.reserve(s.size());
    bool inSpace = false;
    for (const QChar c : s) {
        if (c.isSpace()) {
            if (!inSpace)
                out += u' ';
            inSpace = true;
        } else {
            out += c;
            inSpace = false;
        }
    }
    return out;
}

} // namespace brulion::dates::detail
