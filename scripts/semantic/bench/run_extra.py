"""Combinations, own-vault calibration, an exact in-weight fold of centering, bootstrap CIs."""
import copy, gc, random
import numpy as np, torch
from bench import *
from validate import check
from fusion import tune
import edits as E

V = {}
for name in ["train", "test", "guide"]:
    docs, qs = load(name); check(qs, docs)
    V[name] = (docs, qs, bm25_run(name, qs))
OUT = open(EXP / "results_extra.md", "w")


def say(*a):
    print(*a); print(*a, file=OUT); OUT.flush()


def ranks(run, qs):
    return [rank_of(run[q["query"]], set(q["relevant"])) for q in qs]


def rr(rs):
    return np.array([1 / r if r and r <= 10 else 0 for r in rs])


def boot(a, b, n=4000, seed=0):
    """Paired bootstrap of mean(a-b): (diff, lo, hi)."""
    rng = np.random.default_rng(seed)
    d = a - b
    idx = rng.integers(0, len(d), (n, len(d)))
    m = d[idx].mean(1)
    return d.mean(), np.percentile(m, 2.5), np.percentile(m, 97.5)


def fold_center(base, mu):
    """Exact in-weight version of shared centering: mean pooling is linear, so subtracting mu
    from the bias of the last LayerNorm subtracts it from the sentence embedding."""
    m = copy.deepcopy(base.st)
    ln = m[0].auto_model.encoder.layer[-1].output.LayerNorm
    with torch.no_grad():
        ln.bias -= torch.tensor(mu, dtype=ln.bias.dtype, device=ln.bias.device)
    # sentence-transformers' Normalize module normalises the result; our cosine does the rest
    return Dense(m, base.qp, base.pp, name="fold")


def own_vault_stats(base, docs, st_train):
    """mu_p and the language direction from the target vault's notes only (no queries, no labels);
    mu_q is a shipped constant from the train queries. Falls back to train when one language is rare."""
    texts = [c for p, t in docs.items() for c in chunks_of(p, t)]
    langs = np.array([E.lang(c) for c in texts])
    P = base.enc(texts, base.pp)
    st = dict(st_train)
    st["mu_p"] = P.mean(0)
    if min((langs == "pl").sum(), (langs == "en").sum()) >= 5:
        st["lang_dir"] = P[langs == "pl"].mean(0) - P[langs == "en"].mean(0)
    return st


for mname, mid in [("e5-small", "intfloat/multilingual-e5-small"), ("mmlw-e5-small", "sdadas/mmlw-e5-small")]:
    base = Dense(mid)
    st = E.train_stats(base, V["train"][0], V["train"][1])
    variants = {
        "base": lambda name: base,
        "center+lang (train stats)": lambda name: E.edit_lang_out(base, st),
        "center+lang (own vault)": lambda name: E.edit_lang_out(base, own_vault_stats(base, V[name][0], st)),
        "center-shared, folded into LN bias": lambda name: fold_center(base, (st["mu_p"] + st["mu_q"]) / 2),
        "center-shared (post-map, check)": lambda name: E.edit_center_shared(base, st),
    }
    fus = {}
    res = {n: {} for n in V}
    for vlabel, mk in variants.items():
        d = mk("train")
        tr_run = d.index(V["train"][0]).search(V["train"][1])
        fus[vlabel] = tune(V["train"][2], tr_run, V["train"][1])
        for name in ["test", "guide"]:
            d = mk(name)
            run = d.index(V[name][0]).search(V[name][1])
            res[name][vlabel] = run
            res[name][vlabel + f" + BM25 [{fus[vlabel][0]}]"] = fus[vlabel][1](V[name][2], run)
        gc.collect(); torch.cuda.empty_cache()
    for name in ["test", "guide"]:
        qs = V[name][1]
        rows = [("BM25 (Brulion)", metrics(V[name][2], qs))] + [(f"{mname} | {k}", metrics(r, qs)) for k, r in res[name].items()]
        say(f"\n### {mname} — {name}, MRR@10\n```")
        say(table(rows, CATS + ["mieszane", "ALL"]))
        say("```")
        # paired bootstrap against BM25 and against base(+BM25)
        ref_bm = rr(ranks(V[name][2], qs))
        keys = list(res[name])
        ref_base = rr(ranks(res[name][keys[0]], qs))
        ref_basef = rr(ranks(res[name][keys[1]], qs))
        say("paired bootstrap of ΔMRR@10 (95% CI):")
        for k in keys:
            a = rr(ranks(res[name][k], qs))
            d1, l1, h1 = boot(a, ref_bm)
            d2, l2, h2 = boot(a, ref_basef)
            say(f"  {k[:70]:70} vs BM25 {d1:+.3f} [{l1:+.3f},{h1:+.3f}]   vs base+BM25 {d2:+.3f} [{l2:+.3f},{h2:+.3f}]")
