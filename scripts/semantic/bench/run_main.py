"""Main experiment. Everything chosen on the train vault (trainC+trainD), reported on test
(testA+testB, other topics) and guide (the real Polish user guide).
Usage: run_main.py [base|edits|ft|all]"""
import json, sys, time
from bench import *
from validate import check
from fusion import tune
import edits as E
import ft
import torch

what = sys.argv[1] if len(sys.argv) > 1 else "all"
V = {}
for name in ["train", "test", "guide"]:
    docs, qs = load(name)
    check(qs, docs)
    V[name] = (docs, qs, bm25_run(name, qs))
CATS2 = CATS + ["mieszane", "ALL"]
OUT = open(EXP / f"results_{what}.md", "w")


def say(*a):
    print(*a); print(*a, file=OUT); OUT.flush()


def evaluate(rows_by_vault, label, dense, fusion=None):
    """Index each vault with `dense`, add dense and fused rows."""
    for name, (docs, qs, bm) in V.items():
        run = dense.index(docs).search(qs)
        rows_by_vault[name].append((label, metrics(run, qs)))
        if fusion:
            rows_by_vault[name].append((f"  + BM25 [{fusion[0]}]", metrics(fusion[1](bm, run), qs)))
    return rows_by_vault


def dump(rows_by_vault, title):
    for name in ["train", "test", "guide"]:
        docs, qs, _ = V[name]
        say(f"\n### {title} — {name} ({len(docs)} docs, {len(qs)} queries), MRR@10\n```")
        say(table(rows_by_vault[name], CATS2))
        say("```")


def tuned_fusion(dense):
    docs, qs, bm = V["train"]
    return tune(bm, dense.index(docs).search(qs), qs)


MODELS = {"e5-small": ("intfloat/multilingual-e5-small", "query: ", "passage: "),
          "mmlw-e5-small": ("sdadas/mmlw-e5-small", "query: ", "passage: ")}

for mname, (mid, qp, pp) in MODELS.items():
    base = Dense(mid, qp, pp)
    fus = tuned_fusion(base)
    say(f"\n## {mname}: fusion chosen on train: {fus[0]}")
    rows = {n: [("BM25 (Brulion)", metrics(V[n][2], V[n][1]))] for n in V}
    evaluate(rows, f"{mname}", base, fus)

    if what in ("edits", "all"):
        st = E.train_stats(base, V["train"][0], V["train"][1])
        say(f"(train chunks: {st['n_pl']} pl, {st['n_en']} en; |lang_dir|={np.linalg.norm(st['lang_dir']):.3f}, "
            f"cos(mu_q, mu_p)={st['mu_q']@st['mu_p']/np.linalg.norm(st['mu_q'])/np.linalg.norm(st['mu_p']):.3f})")
        for mk in [lambda: E.edit_center(base, st), lambda: E.edit_center_shared(base, st),
                   lambda: E.edit_lang_out(base, st), lambda: E.edit_whiten(base, st),
                   lambda: E.edit_whiten(base, st, 256),
                   lambda: E.edit_ablate_lang(base, st, list(range(8, 12))),
                   lambda: E.edit_ablate_lang(base, st, list(range(0, 12))),
                   lambda: E.edit_diacritics(base, 0.5), lambda: E.edit_diacritics(base, 1.0),
                   lambda: E.edit_diacritics(base, 0.7, 1.0)]:
            ed = mk()
            evaluate(rows, f"{mname} | {ed.name}", ed)
            del ed; import gc; gc.collect(); torch.cuda.empty_cache()

    if what in ("ft", "all"):
        for aug in (False, True):
            t = time.time()
            pairs = ft.make_pairs(base, V["train"][0], V["train"][1], augment=aug)
            m = ft.finetune(mid, pairs, qp, pp)
            d = Dense(m, qp, pp)
            fus2 = tuned_fusion(d)
            evaluate(rows, f"{mname} | fine-tune{' +aug' if aug else ''} ({len(pairs)} pairs, {time.time()-t:.0f}s)", d, fus2)
            del m
    dump(rows, mname)
