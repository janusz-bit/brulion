"""Build the multi-domain benchmark database from the downloaded raw sets.

db/<name>/notes/*.md        one note per document (title = file name)
db/<name>/queries.jsonl     {"id", "query", "relevant": [file names], "category"}
db/index.json               name -> {domain, lang, queries, source, licence}

Real-qrel sets get category "realne"; vaults without queries get them from agents later
(db/<name>/queries.jsonl written by them, categories as in data/SPEC.md).
"""
import bz2, gzip, json, random, re, sys, unicodedata
import xml.etree.ElementTree as ET
from pathlib import Path

EXP = Path(__file__).resolve().parent
RAW, DB = EXP / "raw", EXP / "db"
rng = random.Random(7)
INDEX = {}


def safe(s, n=70):
    s = re.sub(r'[\\/:*?"<>|\n\r\t#^\[\]]+', " ", s).strip(" .")
    s = re.sub(r"\s+", " ", s)
    return s[:n].strip() or "bez tytułu"


class Builder:
    def __init__(self, name, **meta):
        self.dir = DB / name
        (self.dir / "notes").mkdir(parents=True, exist_ok=True)
        self.name, self.meta, self.files, self.queries = name, meta, {}, []
        self.used = set()

    def note(self, key, title, text):
        fn = safe(title) + ".md"
        if fn.lower() in self.used:
            fn = f"{safe(title, 60)} ~{safe(str(key), 30)}.md"
        self.used.add(fn.lower())
        (self.dir / "notes" / fn).write_text(text.strip() + "\n", encoding="utf-8")
        self.files[key] = fn
        return fn

    def query(self, qid, text, rel_keys, category="realne"):
        rel = [self.files[k] for k in rel_keys if k in self.files]
        if rel and text.strip():
            self.queries.append({"id": f"{self.name}-{qid}", "query": text.strip(), "relevant": rel, "category": category})

    def done(self):
        if self.queries:
            with open(self.dir / "queries.jsonl", "w", encoding="utf-8") as f:
                for q in self.queries:
                    f.write(json.dumps(q, ensure_ascii=False) + "\n")
        INDEX[self.name] = dict(self.meta, notes=len(self.files), queries=len(self.queries))
        print(f"{self.name}: {len(self.files)} notes, {len(self.queries)} queries", file=sys.stderr)


def jl(path):
    op = gzip.open if str(path).endswith(".gz") else open
    with op(path, "rt", encoding="utf-8") as f:
        for line in f:
            if line.strip():
                yield json.loads(line)


def tsv(path):
    rows = [l.rstrip("\n").split("\t") for l in open(path, encoding="utf-8")][1:]
    return [(r[0], r[1], int(float(r[2]))) for r in rows if len(r) >= 3]


# ---------------------------------------------------------------- BEIR-PL (medicine)
def beir(name, domain, maxq=None):
    b = Builder(name, domain=domain, lang="pl (MT)", source=f"clarin-knext/{name}-pl", kind="real qrels")
    for d in jl(RAW / name / "corpus.jsonl.gz"):
        b.note(d["_id"], d.get("title") or d["_id"], d["text"])
    qs = {q["_id"]: q["text"] for q in jl(RAW / name / "queries.jsonl.gz")}
    rel = {}
    for q, d, s in tsv(RAW / name / "test.tsv"):
        if s > 0:
            rel.setdefault(q, []).append(d)
    keys = sorted(rel)
    if maxq and len(keys) > maxq:
        keys = rng.sample(keys, maxq)
    for q in keys:
        b.query(q, qs.get(q, ""), rel[q])
    b.done()


# ---------------------------------------------------------------- PolEval 2022 (law, help desk)
def poleval(name, domain, title_field=True):
    b = Builder(name, domain=domain, lang="pl", source=f"piotr-rybak/poleval2022 {name}", kind="real qrels")
    for d in jl(RAW / name / "passages.jl"):
        key = str(d["id"])
        if title_field and d.get("title"):
            art = re.match(r"(Art\. \d+[a-z]*)", d["text"])
            title = f"{d['title'][:50]} – {art.group(1) if art else key}"
        else:
            title = f"{name} {key}"
        b.note(key, title, d["text"])
    qs = {str(q["id"]): q["text"] for q in jl(RAW / name / "questions-test.jl")}
    rel = {}
    for q, d, s in tsv(RAW / name / "pairs-test.tsv"):
        if s > 0:
            rel.setdefault(q, []).append(d)
    for q in sorted(rel, key=lambda x: int(x)):
        b.query(q, qs.get(q, ""), rel[q])
    b.done()


# ---------------------------------------------------------------- PoliChat (university regulations)
def polichat():
    b = Builder("polichat", domain="regulaminy uczelni", lang="pl", source="clarin-pl/polichat-rag-evaluation pwr_512",
                kind="real qrels")
    for d in jl(RAW / "polichat" / "fragments_512.jsonl"):
        text = d["contents"]
        first = next((l.strip() for l in text.splitlines() if len(l.strip()) > 15), d["id"])
        b.note(d["id"], f"{first[:50]} ~{d['id']}", text)
    for q in json.load(open(RAW / "polichat" / "ir.json", encoding="utf-8")):
        b.query(q["id"], q["question"], q["expect"].get("cite") or [])
    b.done()


# ---------------------------------------------------------------- WebFAQ pol (mixed web FAQs), sampled
def webfaq(nq=1000, ndocs=30000):
    b = Builder("webfaq", domain="FAQ z sieci (różne)", lang="pl", source="PaDaS-Lab/webfaq-retrieval pol",
                kind="real qrels", note=f"sample: {nq} test queries, {ndocs} docs")
    qrels = {}
    for r in jl(RAW / "webfaq" / "qrels-test.jsonl"):
        qrels.setdefault(str(r["query-id"]), []).append(str(r["corpus-id"]))
    keys = rng.sample(sorted(qrels), nq)
    need = {d for k in keys for d in qrels[k]}
    docs = list(jl(RAW / "webfaq" / "corpus.jsonl"))
    others = [d for d in docs if str(d["_id"]) not in need]
    keep = [d for d in docs if str(d["_id"]) in need] + rng.sample(others, ndocs - len(need))
    for d in keep:
        b.note(str(d["_id"]), f"faq {d['_id']}", d["text"])
    qs = {str(q["_id"]): q["text"] for q in jl(RAW / "webfaq" / "queries-test.jsonl")}
    for k in keys:
        b.query(k, qs[k], qrels[k])
    b.done()


# ---------------------------------------------------------------- Markdown vaults
def vault(name, src_dirs, domain, lang, source, exclude=()):
    b = Builder(name, domain=domain, lang=lang, source=source, kind="vault, generated queries")
    for src in src_dirs:
        for f in sorted(Path(src).rglob("*.md")):
            rel = str(f.relative_to(src))
            if any(re.search(x, rel) for x in exclude):
                continue
            text = f.read_text(encoding="utf-8", errors="replace")
            if len(text.strip()) < 40:
                continue
            b.note(str(f.relative_to(src)), f.stem, text)
    b.done()
    return b


def dnd_queries(b, n=400):
    """galgilor/dnd-5e-srd-anchor-positive-pairs: question -> passage; the note holding the passage."""
    import pandas as pd
    d = pd.read_parquet(RAW / "dnd" / "pairs.parquet")
    texts = {fn: re.sub(r"\s+", " ", (b.dir / "notes" / fn).read_text(encoding="utf-8")) for fn in b.files.values()}
    rows = d.sample(frac=1, random_state=7).itertuples()
    added = 0
    for r in rows:
        probe = re.sub(r"\s+", " ", r.positive)[:60]
        hits = [fn for fn, t in texts.items() if probe in t]
        if len(hits) == 1:
            key = next(k for k, v in b.files.items() if v == hits[0])
            b.query(f"en{added:04d}", r.anchor, [key], category="realne-en (LLM)")
            added += 1
            if added >= n:
                break


# ---------------------------------------------------------------- Polish wikis (wikitext -> Markdown-ish)
def wiki_pages(path, keep):
    ns = None
    with bz2.open(path, "rb") as f:
        for _, el in ET.iterparse(f, events=("end",)):
            if el.tag.endswith("}page"):
                ns = ns or el.tag[: el.tag.index("}") + 1]
                title = el.findtext(f"{ns}title")
                nsid = el.findtext(f"{ns}ns")
                text = el.findtext(f"{ns}revision/{ns}text") or ""
                if nsid == "0" and not el.find(f"{ns}redirect") is not None and keep(title, text):
                    yield title, text
                el.clear()


def wikitext_md(text):
    import mwparserfromhell as mw
    text = re.sub(r"^(=+)\s*(.*?)\s*\1\s*$", lambda m: f"HDR{len(m.group(1))}§ {m.group(2)}", text, flags=re.M)
    text = re.sub(r"^\*+\s*", "BUL§ ", text, flags=re.M)
    text = re.sub(r"^#+\s*", "BUL§ ", text, flags=re.M)
    out = mw.parse(text).strip_code(normalize=True, collapse=True)
    out = re.sub(r"HDR(\d)§ ", lambda m: "#" * max(1, int(m.group(1)) - 1) + " ", out)
    out = out.replace("BUL§ ", "- ")
    out = re.sub(r"\n{3,}", "\n\n", out)
    return out.strip()


def wikibooks_recipes():
    b = Builder("przepisy", domain="przepisy kulinarne", lang="pl", source="plwikibooks Książka kucharska (CC-BY-SA)",
                kind="vault, generated queries")
    for title, text in wiki_pages(RAW / "wiki" / "plwikibooks.xml.bz2", lambda t, x: t.startswith("Książka kucharska/")):
        md = wikitext_md(text)
        if len(md) >= 200:
            b.note(title, title.split("/")[-1], md)
    b.done()


def wikivoyage(n=4000):
    b = Builder("podroze", domain="przewodniki podróżne", lang="pl", source="plwikivoyage (CC-BY-SA)",
                kind="vault, generated queries", note=f"random {n} articles of >= 600 chars")
    pages = [(t, x) for t, x in wiki_pages(RAW / "wiki" / "plwikivoyage.xml.bz2", lambda t, x: len(x) > 600)]
    for title, text in rng.sample(pages, min(n, len(pages))):
        md = wikitext_md(text)
        if len(md) >= 300:
            b.note(title, title, md)
    b.done()


if __name__ == "__main__":
    beir("nfcorpus", "medycyna i żywienie")
    beir("scifact", "biomedycyna (twierdzenia naukowe)")
    poleval("legal-questions", "prawo (ustawy 1993–2004)")
    poleval("allegro-faq", "pomoc techniczna sklepu", title_field=False)
    polichat()
    webfaq()
    # Aggregates (…_All, …+Header, letter files …_A-Z/X.md) repeat the single entries: left out.
    b = vault("rpg-dnd", [RAW / "dnd" / "srd"], "RPG: zasady i świat D&D 5.2.1 SRD", "en",
              "oldmanumby/dnd.srd.5.2.1 (CC-BY-4.0); queries galgilor/... (CC-BY-4.0)",
              exclude=[r"_All", r"\+H", r"_Header", r"(?<!Equipment)_A-Z/", r"docs_compiled", r"^(structure|README|Legal)\.md$"])
    dnd_queries(b)
    b.done()
    vault("osobiste-bluebook", [RAW / "bluebook" / "docs"], "osobista baza wiedzy (tech + życie)", "en",
          "lyz-code/blue-book (CC0)")
    vault("logseq-docs", [RAW / "logseq" / "pages", RAW / "logseq" / "journals"], "notatki Logseq: strony + dzienniki",
          "en", "logseq/docs (MIT)")
    wikibooks_recipes()
    wikivoyage()
    json.dump(INDEX, open(DB / "index.json", "w"), ensure_ascii=False, indent=1)
