"""Which part of the calibration helps or hurts on the multi-domain DB (cached embeddings only)."""
import json, sys
import numpy as np
from run_db import load, chunks, encode, search, bm25, DB, EXP
from bench import metrics
from fusion import convex
from validate import check
import edits as E

VARIANTS = ["surowy", "środek z notatnika (wspólny)", "kierunek języka", "środek wspólny + kierunek",
            "pełna (średnia zapytań z pliku)"]


def apply(P, Q, variant, calib, langs):
    mp = P.mean(0)
    d = None
    if (langs == "pl").sum() >= 5 and (langs == "en").sum() >= 5:
        d = P[langs == "pl"].mean(0) - P[langs == "en"].mean(0)
    else:
        d = np.array(calib["direction"], dtype=np.float32)
    d = d / np.linalg.norm(d)
    mq = np.array(calib["queryMean"], dtype=np.float32)

    def f(X, m, proj):
        X = X - m
        if proj:
            X = X - np.outer(X @ d, d)
        return X / np.linalg.norm(X, axis=1, keepdims=True)
    if variant == "surowy":
        return P, Q
    if variant == "środek z notatnika (wspólny)":
        return f(P, mp, False), f(Q, mp, False)
    if variant == "kierunek języka":
        return f(P, 0, True), f(Q, 0, True)
    if variant == "środek wspólny + kierunek":
        return f(P, mp, True), f(Q, mp, True)
    return f(P, mp, True), f(Q, mq, True)


idx = json.load(open(DB / "index.json"))
names = sys.argv[1:] or list(idx)
rows = []
for mlabel, mid, qp, pp, cal in [("e5-small", "intfloat/multilingual-e5-small", "query: ", "passage: ", "models/e5-small.calib.json"),
                                 ("mmlw-e5-small", "sdadas/mmlw-e5-small", "query: ", "passage: ", "models/mmlw-e5-small.calib.json")]:
    calib = json.load(open(EXP / cal))
    for n in names:
        p = EXP / "cache" / "db" / f"emb_{mid.replace('/', '_')}_{n}.npy"
        if not p.exists():
            continue
        docs, qs = load(n)
        check(qs, docs)
        ids, owner, texts = chunks(docs)
        langs = np.array([E.lang(t) for t in texts])
        P = encode(mid, texts, pp, n)
        Q = encode(mid, [q["query"] for q in qs], qp, n + "_q")
        bm = bm25(n, qs)
        for v in VARIANTS:
            Pc, Qc = apply(P, Q, v, calib, langs)
            run = search(Qc, Pc, owner, ids, qs)
            rows.append((mlabel, n, v, metrics(run, qs)["ALL"]["mrr"], metrics(convex(bm, run, 0.8), qs)["ALL"]["mrr"]))
        print(mlabel, n, file=sys.stderr)

print("| model | wariant | " + " | ".join(names) + " | średnia | średnia + BM25 |")
print("|" + "---|" * (len(names) + 4))
for m in ("e5-small", "mmlw-e5-small"):
    for v in VARIANTS:
        vals = {r[1]: r for r in rows if r[0] == m and r[2] == v}
        if not vals:
            continue
        cells = [f"{vals[n][3]:.3f}" if n in vals else "–" for n in names]
        a = [vals[n][3] for n in names if n in vals]
        b = [vals[n][4] for n in names if n in vals]
        print(f"| {m} | {v} | " + " | ".join(cells) + f" | {np.mean(a):.3f} | {np.mean(b):.3f} |")
