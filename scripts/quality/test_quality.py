"""Tests of scripts/quality/quality.py — its pure parts (no git, lizard, compiler or ctest).

    python3 -I -m unittest discover -s scripts/quality -p 'test_*.py'
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import quality as q  # noqa: E402


def texts(tokens):
    return [t.text for t in tokens]


class TokenizeTest(unittest.TestCase):
    def test_skips_comments_strings_and_preprocessor_as_single_tokens(self):
        src = ('#include "a.h"\n'
               'int f(int a) { // x < y\n'
               '  /* a && b */ return a == 1 ? u"zażółć < gęślą"_s : R"(x == y)";\n'
               '}\n')
        toks = q.tokenize(src)
        kinds = {t.kind for t in toks}
        self.assertIn('pp', kinds)
        code = [t for t in toks if t.kind not in q.TRIVIA]
        self.assertEqual(texts(code), ['int', 'f', '(', 'int', 'a', ')', '{', 'return', 'a', '==', '1',
                                       '?', 'u"zażółć < gęślą"_s', ':', 'R"(x == y)"', ';', '}'])
        self.assertEqual([t.line for t in code if t.text == '=='], [3])

    def test_longest_operator_first(self):
        code = [t.text for t in q.tokenize('a <<= b; c->d; e <=> f; g >>= h; i && j || k;')
                if t.kind == 'op']
        self.assertEqual(code, ['<<=', ';', '->', ';', '<=>', ';', '>>=', ';', '&&', '||', ';'])

    def test_preprocessor_with_continuation(self):
        toks = q.tokenize('#define X(a) \\\n  (a + 1)\nint y = 2;\n')
        self.assertEqual(toks[0].kind, 'pp')
        self.assertIn('(a + 1)', toks[0].text)
        self.assertEqual([t.text for t in toks if t.kind not in q.TRIVIA], ['int', 'y', '=', '2', ';'])

    def test_hash_inside_line_is_not_preprocessor(self):
        toks = [t for t in q.tokenize('auto s = a # b;') if t.kind not in q.TRIVIA]
        self.assertEqual(texts(toks), ['auto', 's', '=', 'a', '#', 'b', ';'])

    def test_unterminated_input_does_not_hang(self):
        toks = q.tokenize('"never closed\n/* never closed')
        self.assertTrue(toks)  # every character ends up in some token
        self.assertEqual(''.join(t.text for t in toks), '"never closed\n/* never closed')


class ChangedLinesTest(unittest.TestCase):
    def test_parses_hunks_of_new_side(self):
        diff = ('diff --git a/src/a.cpp b/src/a.cpp\n'
                '--- a/src/a.cpp\n'
                '+++ b/src/a.cpp\n'
                '@@ -10,0 +11,3 @@ void f()\n'
                '+x\n+y\n+z\n'
                '@@ -40 +44 @@\n'
                '-old\n+new\n'
                'diff --git a/src/gone.cpp b/src/gone.cpp\n'
                '--- a/src/gone.cpp\n'
                '+++ /dev/null\n'
                '@@ -1,2 +0,0 @@\n'
                '-a\n-b\n'
                'diff --git a/src/b.h b/src/b.h\n'
                '--- a/src/b.h\n'
                '+++ b/src/b.h\n'
                '@@ -5,2 +5,0 @@\n'
                '-only deleted\n-lines\n')
        self.assertEqual(q.changed_lines(diff), {'src/a.cpp': {11, 12, 13, 44}, 'src/b.h': {5}})


class LcovTest(unittest.TestCase):
    def test_reads_line_hits_per_file(self):
        lcov = ('SF:/w/src/a.cpp\nDA:3,1\nDA:4,0\nDA:5,12\nend_of_record\n'
                'SF:/w/src/b.cpp\nDA:1,0\nend_of_record\n')
        self.assertEqual(q.parse_lcov(lcov), {'/w/src/a.cpp': {3: 1, 4: 0, 5: 12}, '/w/src/b.cpp': {1: 0}})

    def test_same_file_twice_adds_hits(self):
        lcov = 'SF:/a\nDA:1,0\nDA:2,1\nend_of_record\nSF:/a\nDA:1,3\nend_of_record\n'
        self.assertEqual(q.parse_lcov(lcov), {'/a': {1: 3, 2: 1}})


class CrapTest(unittest.TestCase):
    def test_formula(self):
        self.assertEqual(q.crap(1, 1.0), 1)
        self.assertEqual(q.crap(5, 0.0), 30)
        self.assertAlmostEqual(q.crap(10, 0.5), 10 * 10 * 0.125 + 10)

    def test_function_coverage_counts_only_its_lines(self):
        fn = q.Function('f', 'src/a.cpp', 10, 13, 2)
        self.assertEqual(q.function_coverage(fn, {9: 0, 10: 1, 11: 0, 13: 4, 14: 0}), 2 / 3)
        self.assertIsNone(q.function_coverage(fn, {1: 1}))


class ScopeTest(unittest.TestCase):
    def test_function_in_scope_when_a_changed_line_is_inside(self):
        fns = [q.Function('a', 'x.cpp', 1, 5, 1), q.Function('b', 'x.cpp', 7, 9, 1),
               q.Function('c', 'y.cpp', 1, 3, 1)]
        self.assertEqual([f.name for f in q.in_scope(fns, {'x.cpp': {8}})], ['b'])


class LizardCsvTest(unittest.TestCase):
    def test_reads_rows(self):
        csv_text = ('15,3,90,1,15,"ns::f@12-26@src/a.cpp","src/a.cpp","ns::f","ns::f( QByteArrayView d)",12,26\n'
                    '4,1,15,1,4,"g@28-31@src/a.cpp","src/a.cpp","g","g( int, int)",28,31\n')
        fns = q.parse_lizard_csv(csv_text)
        self.assertEqual(fns[0], q.Function('ns::f', 'src/a.cpp', 12, 26, 3))
        self.assertEqual(fns[1].ccn, 1)


class NormalizeTest(unittest.TestCase):
    def test_locals_and_literals_drop_calls_and_operators_stay(self):
        a = q.normalize(q.tokenize('{ int total = order.amount + 3; return compute(total, u"x"_s); }'))
        b = q.normalize(q.tokenize('{ int sum = row.price + 7; return compute(sum, u"yy"_s); }'))
        c = q.normalize(q.tokenize('{ int sum = row.price + 7; return other(sum, u"yy"_s); }'))
        self.assertEqual(a, b)
        self.assertNotEqual(a, c)
        self.assertIn('compute', a)
        self.assertIn('return', a)

    def test_qualified_names_stay_so_tables_of_other_enums_differ(self):
        a = q.normalize(q.tokenize('{ switch (k) { case Change::Added: return 1; } }'))
        b = q.normalize(q.tokenize('{ switch (e) { case Reply::Timeout: return 2; } }'))
        self.assertNotEqual(a, b)
        self.assertIn('Change', a)
        self.assertIn('Added', a)

    def test_similar_pairs_finds_a_renamed_copy_only(self):
        body = '{ int total = 0; for (const auto &x : items) { if (x.ok) total += weigh(x, 3); } return total; }'
        copy = body.replace('total', 'sum').replace('items', 'rows').replace('3', '4')
        other = '{ QString s = name(); s.replace(u"a"_s, u"b"_s); emitChanged(s); return s.size() > 2 ? 1 : 0; }'
        fns = {q.Function('a', 'a.cpp', 1, 1, 3): body, q.Function('b', 'b.cpp', 1, 1, 3): copy,
               q.Function('c', 'c.cpp', 1, 1, 2): other}
        shapes = {f: q.normalize(q.tokenize(t)) for f, t in fns.items()}
        a = next(f for f in fns if f.name == 'a')
        pairs = q.similar_pairs([a], list(fns), shapes, 0.8, 5)
        self.assertEqual([(p[1].name, p[2].name) for p in pairs], [('a', 'b')])
        self.assertEqual(pairs[0][0], 1.0)

    def test_file_shape_per_line_matches_normalize(self):
        src = ('#include <x>\n'
               'int f(int a) {\n'
               '  return g(a) + Kind::One; // c\n'
               '}\n'
               'auto s = R"(\n'
               'two lines)";\n'
               'int h() { return 2; }\n')
        lines = q.file_shape(src)
        self.assertEqual(len(lines), src.count('\n') + 1)
        self.assertEqual(q.shape_of(lines, 2, 4),
                         q.normalize([t for t in q.tokenize(src) if 2 <= t.line <= 4]))
        self.assertEqual(lines[2], ['return', 'g', '(', 'ID', ')', '+', 'Kind', '::', 'One', ';'])
        self.assertEqual(lines[4], ['auto', 'ID', '=', 'LIT'])  # a token counts on its first line
        self.assertEqual(lines[5], [';'])

    def test_shape_cache_is_keyed_by_content(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            first = q.cached_file_shape('int f() { return a; }\n', d)
            self.assertEqual(len(os.listdir(d)), 1)
            again = q.cached_file_shape('int f() { return a; }\n', d)
            self.assertEqual(first, again)
            self.assertEqual(len(os.listdir(d)), 1)
            q.cached_file_shape('int f() { return b(); }\n', d)
            self.assertEqual(len(os.listdir(d)), 2)

    def test_similarity(self):
        self.assertEqual(q.similarity(['a', 'b'], ['a', 'b']), 1.0)
        self.assertEqual(q.similarity(['a'] * 10, ['b'] * 30), 0.0)


class MutantsTest(unittest.TestCase):
    def mutants(self, src, lines=None):
        toks = q.tokenize(src)
        return [(m.line, m.original, m.replacement) for m in q.mutants(src, toks, lines or {1})]

    def test_relational_and_logical_operators_with_spaces(self):
        got = self.mutants('if (a == b && c < d || !e) return true;')
        self.assertIn((1, '==', '!='), got)
        self.assertIn((1, '&&', '||'), got)
        self.assertIn((1, '||', '&&'), got)
        self.assertIn((1, '<', '<='), got)
        self.assertIn((1, '!', ''), got)
        self.assertIn((1, 'true', 'false'), got)

    def test_templates_and_references_are_left_alone(self):
        got = self.mutants('QList<int> f(T&& x, const QHash<A, B> &h);')
        self.assertEqual(got, [])

    def test_numbers(self):
        got = self.mutants('x = 0; y = 1; z = 41; w = 0x10; v = 2.5;')
        self.assertIn((1, '0', '1'), got)
        self.assertIn((1, '1', '0'), got)
        self.assertIn((1, '41', '42'), got)
        self.assertNotIn('0x10', [g[1] for g in got])
        self.assertNotIn('2.5', [g[1] for g in got])

    def test_only_given_lines(self):
        got = self.mutants('a == b;\nc == d;\n', lines={2})
        self.assertEqual(got, [(2, '==', '!=')])

    def test_strings_and_comments_never_mutated(self):
        got = self.mutants('// a == b\nf(u"a == b"_s);\n', lines={1, 2})
        self.assertEqual([g for g in got if g[1] == '=='], [])
        self.assertEqual(got, [(2, 'f(u"a == b"_s);', '')])  # the whole call may go, its text never

    def test_call_statement_removed(self):
        got = self.mutants('  {\n  emitChanged(a, b);\n  }\n', lines={2})
        self.assertIn((2, 'emitChanged(a, b);', ''), got)

    def test_apply_replaces_exactly_one_span(self):
        src = 'a == b; c == d;'
        m = q.mutants(src, q.tokenize(src), {1})[1]
        self.assertEqual(q.apply(src, m), 'a == b; c != d;')


class JournalTest(unittest.TestCase):
    def test_restore_after_crash_is_idempotent(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'a.cpp')
            with open(path, 'w', encoding='utf-8') as f:
                f.write('mutated')
            journal = os.path.join(d, 'journal.json')
            q.write_journal(journal, path, 'original ąę')
            q.restore_from_journal(journal)
            q.restore_from_journal(journal)  # a second run: nothing left to do
            with open(path, encoding='utf-8') as f:
                self.assertEqual(f.read(), 'original ąę')
            self.assertFalse(os.path.exists(journal))


if __name__ == '__main__':
    unittest.main()
