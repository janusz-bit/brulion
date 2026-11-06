"""Fusion variants of BM25 and dense runs, tuned on the train vault."""
import itertools
from bench import rrf, metrics


def convex(bm, de, alpha, limit=100):
    """alpha * dense_minmax + (1-alpha) * bm25_minmax; documents missing from a run score 0."""
    out = {}
    for q in de:
        def norm(run):
            if not run:
                return {}
            s = [x[1] for x in run]
            lo, hi = min(s), max(s)
            return {d: (v - lo) / (hi - lo) if hi > lo else 1.0 for d, v in run}
        b, d = norm(bm.get(q, [])), norm(de[q])
        sc = {k: alpha * d.get(k, 0) + (1 - alpha) * b.get(k, 0) for k in set(b) | set(d)}
        out[q] = sorted(([k, v] for k, v in sc.items()), key=lambda x: -x[1])[:limit]
    return out


def tune(bm, de, queries):
    """Best fusion on these (train) queries by overall MRR@10. Returns (label, fn)."""
    cands = []
    for w, k in itertools.product([1, 1.5, 2, 3, 5], [10, 30, 60]):
        cands.append((f"RRF w_dense={w} k={k}", lambda b, d, w=w, k=k: rrf(b, d, k=k, weights=[1, w])))
    for a in [0.3, 0.5, 0.6, 0.7, 0.8, 0.9]:
        cands.append((f"convex a={a}", lambda b, d, a=a: convex(b, d, a)))
    best = max(cands, key=lambda c: metrics(c[1](bm, de), queries)["ALL"]["mrr"])
    return best
