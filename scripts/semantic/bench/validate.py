"""Check category rules of generated queries automatically; queries breaking their category's rule
are relabelled 'mieszane' (mixed) so that categories stay pure."""
import re, unicodedata, sys
from bench import load, CATS

STOP = set("""a aby albo ale bez by być co czy dla do i ich jak jaki jakie jest już kiedy ktory
która które który mi mój na nad nie o od po pod przy się sie ta te ten to tu w we z za ze że
jaka jaką moje moja mojego gdzie the a an of to in on for and or is how what when where my with
from at by do does did""".split())


def fold(s):
    s = s.lower().replace("ł", "l")
    s = unicodedata.normalize("NFKD", s)
    return "".join(c for c in s if not unicodedata.combining(c))


def toks(s):
    return [t for t in re.findall(r"\w+", fold(s)) if t not in {fold(x) for x in STOP} and len(t) > 1]


def check(queries, docs):
    stats = {}
    for q in queries:
        cat = q["category"]
        text = " ".join(docs[r] + " " + r.split("/")[-1][:-3] for r in q["relevant"])
        dt = set(toks(text))
        qt = toks(q["query"])
        exact = [t for t in qt if t in dt]
        stem = [t for t in qt if len(t) >= 4 and any(d.startswith(t[:4]) for d in dt)]
        bad = (cat == "odmiana" and exact) or (cat == "synonim" and (exact or stem))
        q["orig_category"] = cat
        if bad:
            q["category"] = "mieszane"
        stats.setdefault(cat, [0, 0])
        stats[cat][0] += 1
        stats[cat][1] += bool(bad)
    return stats


if __name__ == "__main__":
    for name in sys.argv[1:]:
        docs, qs = load(name)
        st = check(qs, docs)
        print(name, len(docs), "docs", len(qs), "queries;", {k: f"{v[1]}/{v[0]} broken" for k, v in st.items()})
