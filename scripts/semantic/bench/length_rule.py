import json, numpy as np
from run_db import load, chunks, encode, search, bm25, DB, EXP, calibrate
from bench import metrics
from fusion import convex
from validate import check
import edits as E
idx = json.load(open(DB / "index.json"))
res = {}
for mlabel, mid, cal in [("e5-small", "intfloat/multilingual-e5-small", "models/e5-small.calib.json"), ("mmlw-e5-small", "sdadas/mmlw-e5-small", "models/mmlw-e5-small.calib.json")]:
    c = json.load(open(EXP / cal))
    for n in idx:
        docs, qs = load(n); check(qs, docs)
        ids, owner, texts = chunks(docs)
        langs = np.array([E.lang(t) for t in texts])
        P = encode(mid, texts, "passage: ", n); Q = encode(mid, [q["query"] for q in qs], "query: ", n + "_q")
        raw = search(Q, P, owner, ids, qs)
        Pc, Qc = calibrate(P, Q, langs, c, True)
        cal_ = search(Qc, Pc, owner, ids, qs)
        bm = bm25(n, qs)
        for th in [0, 3, 4, 5, 6, 8, 99]:
            mixed = {q["query"]: (cal_ if len(q["query"].split()) <= th else raw)[q["query"]] for q in qs}
            for fuse in (False, True):
                run = convex(bm, mixed, 0.8) if fuse else mixed
                res.setdefault((mlabel, th, fuse), {})[n] = metrics(run, qs)["ALL"]["mrr"]
real = [n for n in idx if idx[n]["kind"] == "real qrels"]; gen = [n for n in idx if idx[n]["kind"] != "real qrels"]
print("model | kalibruj zapytania o <= N słowach | +BM25 | prawdziwe | wygenerowane | wszystkie 11")
for (m, th, f), v in res.items():
    a = np.mean([v[n] for n in real]); b = np.mean([v[n] for n in gen]); al = np.mean(list(v.values()))
    print(f"{m:14} N={'wszystkie' if th==99 else ('nigdy' if th==0 else th):9} {'+B' if f else '  '} {a:.3f} {b:.3f} {al:.3f}")
