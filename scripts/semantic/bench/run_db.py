"""Multi-domain benchmark over db/: BM25 (Brulion's SearchIndex through brulion-searchbench) vs
embedding models (GPU, chunks exactly as the C++ prototype), raw / calibrated / hybrid.
Usage: run_db.py [corpus …]"""
import json, subprocess, sys, time
from pathlib import Path
import numpy as np, torch
from bench import metrics, table, CATS, rank_of
from chunker import chunk_note
from validate import check
import edits as E
from fusion import convex

EXP = Path(__file__).resolve().parent
DB, CACHE = EXP / "db", EXP / "cache" / "db"
CACHE.mkdir(parents=True, exist_ok=True)
SB = "/home/dinosaur/workspace/brulion-semantic/build/tests/searchbench/brulion-searchbench"
WT = "/home/dinosaur/workspace/brulion-semantic"
ALPHA = 0.8
MODELS = [  # label, HF id, query prefix, passage prefix, calibration file
    ("e5-small", "intfloat/multilingual-e5-small", "query: ", "passage: ", "models/e5-small.calib.json"),
    ("mmlw-e5-small", "sdadas/mmlw-e5-small", "query: ", "passage: ", "models/mmlw-e5-small.calib.json"),
    ("e5-base", "intfloat/multilingual-e5-base", "query: ", "passage: ", None),
    ("bge-m3", "BAAI/bge-m3", "", "", None),
]


def load(name):
    d = DB / name
    docs = {f.name: f.read_text(encoding="utf-8") for f in sorted((d / "notes").glob("*.md"))}
    qs = []
    for fn in ("queries.jsonl", "queries_gen.jsonl"):
        p = d / fn
        if p.exists():
            for line in p.read_text(encoding="utf-8").splitlines():
                if line.strip():
                    q = json.loads(line)
                    if all(r in docs for r in q["relevant"]) and q["query"].strip():
                        qs.append(q)
    # duplicate query texts would collide in run dicts: keep the first
    seen, out = set(), []
    for q in qs:
        if q["query"] not in seen:
            seen.add(q["query"]); out.append(q)
    return docs, out


def bm25(name, qs):
    out = CACHE / f"bm25_{name}.json"
    if not out.exists():
        qf = CACHE / f"q_{name}.jsonl"
        qf.write_text("".join(json.dumps({"query": q["query"]}, ensure_ascii=False) + "\n" for q in qs), encoding="utf-8")
        subprocess.run(["nix", "develop", WT, "-c", SB, "--corpus", str(DB / name / "notes"), "--queries", str(qf),
                        "--out", str(out)], check=True, capture_output=True)
    return json.load(open(out))["bm25"]


_st = {}


def model(mid):
    from sentence_transformers import SentenceTransformer
    if mid not in _st:
        _st.clear(); torch.cuda.empty_cache()
        m = SentenceTransformer(mid, device="cuda", model_kwargs={"torch_dtype": torch.float16})
        m.max_seq_length = 512
        _st[mid] = m
    return _st[mid]


def chunks(docs):
    ids, owner, texts = list(docs), [], []
    for i, (fn, t) in enumerate(docs.items()):
        for c in chunk_note(fn[:-3], t):
            texts.append(c); owner.append(i)
    return ids, np.array(owner), texts


def encode(mid, texts, prefix, tag):
    p = CACHE / f"emb_{mid.replace('/', '_')}_{tag}.npy"
    if p.exists():
        return np.load(p)
    m = model(mid)
    # longest first: less padding per batch
    order = np.argsort([-len(t) for t in texts])
    e = m.encode([prefix + texts[i] for i in order], batch_size=128, convert_to_numpy=True,
                 normalize_embeddings=True, show_progress_bar=False).astype(np.float32)
    out = np.empty_like(e); out[order] = e
    np.save(p, out)
    return out


def search(Q, C, owner, ids, qs, limit=100):
    res = {}
    S = Q @ C.T
    for qi, q in enumerate(qs):
        best = np.full(len(ids), -9.0, dtype=np.float32)
        np.maximum.at(best, owner, S[qi])
        top = np.argpartition(-best, min(limit, len(ids) - 1))[:limit]
        top = top[np.argsort(-best[top])]
        res[q["query"]] = [[ids[i], float(best[i])] for i in top]
    return res


def calibrate(P, Q, langs, calib, own):
    """Calibration of the prototype: queryMean from the shipped file; passageMean and direction
    from this corpus when `own` (direction only when both languages have >= 5 chunks)."""
    mq, mp, d = (np.array(calib[k], dtype=np.float32) for k in ("queryMean", "passageMean", "direction"))
    if own:
        mp = P.mean(0)
        if (langs == "pl").sum() >= 5 and (langs == "en").sum() >= 5:
            d = P[langs == "pl"].mean(0) - P[langs == "en"].mean(0)
    d = d / np.linalg.norm(d)

    def f(X, m):
        X = X - m
        X = X - np.outer(X @ d, d)
        return X / np.linalg.norm(X, axis=1, keepdims=True)
    return f(P, mp), f(Q, mq)


def run(name):
    docs, qs = load(name)
    check(qs, docs)  # odmiana/synonim breaking their rule -> "mieszane"
    t0 = time.time()
    ids, owner, texts = chunks(docs)
    langs = np.array([E.lang(t) for t in texts])
    runs = {"BM25 (Brulion)": bm25(name, qs)}
    for label, mid, qp, pp, cal in MODELS:
        P = encode(mid, texts, pp, name)
        Q = encode(mid, [q["query"] for q in qs], qp, name + "_q")
        runs[label] = search(Q, P, owner, ids, qs)
        if cal:
            c = json.load(open(EXP / cal))
            for tag, own in (("kalibracja", False), ("kalibracja z notatnika", True)):
                Pc, Qc = calibrate(P, Q, langs, c, own)
                runs[f"{label} + {tag}"] = search(Qc, Pc, owner, ids, qs)
            runs[f"{label} + kal. z notatnika + BM25"] = convex(runs["BM25 (Brulion)"], runs[f"{label} + kalibracja z notatnika"], ALPHA)
        runs[f"{label} + BM25"] = convex(runs["BM25 (Brulion)"], runs[label], ALPHA)
    res = {k: metrics(v, qs) for k, v in runs.items()}
    json.dump({"res": res, "n_docs": len(docs), "n_chunks": len(texts), "n_q": len(qs),
               "langs": {l: int((langs == l).sum()) for l in ("pl", "en")}},
              open(CACHE / f"result_{name}.json", "w"), ensure_ascii=False)
    print(f"{name}: {len(docs)} notes, {len(texts)} chunks, {len(qs)} queries, {time.time()-t0:.0f}s", file=sys.stderr)
    return res


if __name__ == "__main__":
    idx = json.load(open(DB / "index.json"))
    names = sys.argv[1:] or list(idx)
    for n in names:
        run(n)
