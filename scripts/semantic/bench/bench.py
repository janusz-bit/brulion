"""Search benchmark: corpora, BM25 (Brulion's real SearchIndex via brulion-cli), dense retrieval,
hybrids and metrics."""
import json, os, re, subprocess, sys, hashlib
from pathlib import Path
from collections import defaultdict
import numpy as np

EXP = Path(__file__).resolve().parent
DATA = EXP / "data"
CACHE = EXP / "cache"
CACHE.mkdir(exist_ok=True)
CLI = "/home/dinosaur/workspace/brulion/build/src/cli/brulion-cli"
GUIDE_SRC = Path("/home/dinosaur/workspace/brulion/src/app/shell/guide/pl")

# vault name -> slices (folders inside the vault)
VAULTS = {"test": ["testA", "testB"], "guide": ["guide"], "train": ["trainC", "trainD"]}
CATS = ["dokladne", "odmiana", "synonim", "jezyk", "literowka"]


def vault_dir(name):
    """Materialise a vault: <exp>/vaults/<name>/<slice>/<Title>.md"""
    root = EXP / "vaults" / name
    if not root.exists():
        root.mkdir(parents=True)
        for s in VAULTS[name]:
            src = GUIDE_SRC if s == "guide" else DATA / s / "notes"
            dst = root / s
            dst.mkdir()
            for f in sorted(src.glob("*.md")):
                (dst / f.name).write_bytes(f.read_bytes())
    return root


def load(name):
    root = vault_dir(name)
    docs = {}
    for s in VAULTS[name]:
        for f in sorted((root / s).glob("*.md")):
            docs[f"{s}/{f.name}"] = f.read_text(encoding="utf-8")
    queries = []
    for s in VAULTS[name]:
        for line in (DATA / s / "queries.jsonl").read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            q = json.loads(line)
            q["relevant"] = [f"{s}/{r}" for r in q["relevant"]]
            q["slice"] = s
            if all(r in docs for r in q["relevant"]):
                queries.append(q)
    return docs, queries


# ---------------------------------------------------------------- BM25 (Brulion)
def bm25_run(name, queries, limit=100, cli=CLI, tag="bm25"):
    path = CACHE / f"{tag}_{name}.json"
    cache = json.loads(path.read_text()) if path.exists() else {}
    root = vault_dir(name)
    changed = False
    for q in queries:
        if q["query"] in cache:
            continue
        out = subprocess.run([cli, "--vault", str(root), "--no-history", "--read-only", "--compact",
                              "search", "--limit", str(limit), "--", *q["query"].split()],
                             capture_output=True, text=True)
        try:
            hits = json.loads(out.stdout)["result"]["hits"]
            cache[q["query"]] = [[h["path"], h["score"]] for h in hits]
        except Exception:
            cache[q["query"]] = []
        changed = True
    if changed:
        path.write_text(json.dumps(cache, ensure_ascii=False))
    return {q["query"]: cache[q["query"]] for q in queries}


# ---------------------------------------------------------------- dense
def chunks_of(path, text, max_words=120):
    """Title + blocks of up to ~max_words words (split on blank lines / headings / top bullets)."""
    title = Path(path).stem
    text = re.sub(r"^---\n.*?\n---\n", "", text, flags=re.S)
    parts = re.split(r"\n\s*\n|\n(?=#)|\n(?=- )", text)
    out, cur = [], []
    n = 0
    for p in parts:
        w = len(p.split())
        if cur and n + w > max_words:
            out.append(" ".join(cur)); cur, n = [], 0
        cur.append(p.strip()); n += w
    if cur:
        out.append(" ".join(cur))
    out = [c for c in out if c.strip()] or [""]
    return [f"{title}\n{c}" for c in out]


class Dense:
    def __init__(self, model, qp="query: ", pp="passage: ", name=None, device="cuda"):
        from sentence_transformers import SentenceTransformer
        self.st = model if not isinstance(model, str) else SentenceTransformer(model, device=device)
        self.qp, self.pp = qp, pp
        self.name = name or (model if isinstance(model, str) else "custom")
        self.post = None  # optional np function applied to embeddings (before normalisation)

    def enc(self, texts, prefix, bs=64):
        e = self.st.encode([prefix + t for t in texts], batch_size=bs, convert_to_numpy=True,
                           normalize_embeddings=self.post is None, show_progress_bar=False)
        if self.post is not None:
            e = self.post(e)
            e = e / np.linalg.norm(e, axis=1, keepdims=True)
        return e

    def index(self, docs):
        self.ids, self.owner, texts = list(docs), [], []
        for i, (p, t) in enumerate(docs.items()):
            for c in chunks_of(p, t):
                texts.append(c); self.owner.append(i)
        self.owner = np.array(self.owner)
        self.chunk_texts = texts
        self.C = self.enc(texts, self.pp)
        return self

    def search(self, queries, limit=100):
        Q = self.enc([q["query"] for q in queries], self.qp)
        S = Q @ self.C.T  # queries x chunks
        res = {}
        for qi, q in enumerate(queries):
            best = np.full(len(self.ids), -9.0)
            np.maximum.at(best, self.owner, S[qi])
            order = np.argsort(-best)[:limit]
            res[q["query"]] = [[self.ids[i], float(best[i])] for i in order]
        return res


# ---------------------------------------------------------------- fusion & metrics
def rrf(*runs, k=60, weights=None, limit=100):
    weights = weights or [1.0] * len(runs)
    out = {}
    for q in runs[0]:
        sc = defaultdict(float)
        for w, run in zip(weights, runs):
            for r, (d, _) in enumerate(run.get(q, [])):
                sc[d] += w / (k + r + 1)
        out[q] = sorted(([d, s] for d, s in sc.items()), key=lambda x: -x[1])[:limit]
    return out


def rank_of(hits, relevant):
    for i, (d, _) in enumerate(hits):
        if d in relevant:
            return i + 1
    return None


def metrics(run, queries):
    by = defaultdict(list)
    for q in queries:
        r = rank_of(run.get(q["query"], []), set(q["relevant"]))
        for key in (q["category"], "ALL"):
            by[key].append(r)
    res = {}
    for key, ranks in by.items():
        n = len(ranks)
        res[key] = dict(n=n,
                        mrr=sum(1 / r for r in ranks if r and r <= 10) / n,
                        r1=sum(1 for r in ranks if r == 1) / n,
                        r5=sum(1 for r in ranks if r and r <= 5) / n,
                        r10=sum(1 for r in ranks if r and r <= 10) / n)
    return res


def table(rows, cats=CATS + ["ALL"], key="mrr"):
    """rows: list of (label, metrics)"""
    w = max(len(l) for l, _ in rows) + 2
    head = "".ljust(w) + "".join(c[:9].rjust(10) for c in cats)
    lines = [head]
    for label, m in rows:
        lines.append(label.ljust(w) + "".join(
            (f"{m[c][key]:.3f}" if c in m else "-").rjust(10) for c in cats))
    ns = rows[0][1]
    lines.append("n".ljust(w) + "".join(str(ns[c]["n"] if c in ns else 0).rjust(10) for c in cats))
    return "\n".join(lines)
