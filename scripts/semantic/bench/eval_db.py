import json, sys, numpy as np
from run_db import load, chunks, encode, search, bm25, calibrate, EXP
from bench import metrics
from fusion import convex
from validate import check
import edits as E
c = json.load(open(EXP / "models/e5-small.calib.json"))
print("korpus | C++ BM25 | C++ semantyka | C++ hybryda | Python semantyka (reguła) | Python hybryda | zgodność top-1")
for n in sys.argv[1:]:
    runs = json.load(open(f"cpp/db_{n}.json"))
    docs, qs = load(n); check(qs, docs)
    m = {k: metrics(runs[k], qs)["ALL"]["mrr"] for k in ("bm25", "semantic", "hybrid")}
    ids, owner, texts = chunks(docs); langs = np.array([E.lang(t) for t in texts])
    P = encode("intfloat/multilingual-e5-small", texts, "passage: ", n); Q = encode("intfloat/multilingual-e5-small", [q["query"] for q in qs], "query: ", n + "_q")
    raw = search(Q, P, owner, ids, qs); Pc, Qc = calibrate(P, Q, langs, c, True); cal = search(Qc, Pc, owner, ids, qs)
    py = {q["query"]: (cal if len(q["query"].split()) <= 5 else raw)[q["query"]] for q in qs}
    pyh = convex(bm25(n, qs), py, 0.8)
    same = np.mean([runs["semantic"][q["query"]][0][0] == py[q["query"]][0][0] for q in qs if runs["semantic"].get(q["query"])])
    print(f"{n} | {m['bm25']:.3f} | {m['semantic']:.3f} | {m['hybrid']:.3f} | {metrics(py, qs)['ALL']['mrr']:.3f} | {metrics(pyh, qs)['ALL']['mrr']:.3f} | {same:.1%}")
