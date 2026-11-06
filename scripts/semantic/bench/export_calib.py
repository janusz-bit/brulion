"""Calibration JSON (queryMean, passageMean, direction) from the train vault, for the C++ prototype."""
import json, sys
from bench import *
from validate import check
import edits as E
docs, qs = load("train"); check(qs, docs)
for name, mid in [("e5-small", "intfloat/multilingual-e5-small"), ("mmlw-e5-small", "sdadas/mmlw-e5-small")]:
    st = E.train_stats(Dense(mid), docs, qs)
    d = st["lang_dir"] / np.linalg.norm(st["lang_dir"])
    json.dump({"queryMean": st["mu_q"].tolist(), "passageMean": st["mu_p"].tolist(), "direction": d.tolist()},
              open(f"models/{name}.calib.json", "w"))
    print(name, "ok")
