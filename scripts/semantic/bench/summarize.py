"""Summary tables of run_db.py results -> results_db.md"""
import json
from collections import defaultdict
from pathlib import Path

EXP = Path(__file__).resolve().parent
idx = json.load(open(EXP / "db" / "index.json"))
R = {n: json.load(open(p)) for n in idx if (p := EXP / "cache" / "db" / f"result_{n}.json").exists()}
SYS = ["BM25 (Brulion)",
       "e5-small", "e5-small + kalibracja z notatnika", "e5-small + BM25", "e5-small + kal. z notatnika + BM25",
       "mmlw-e5-small", "mmlw-e5-small + kalibracja z notatnika", "mmlw-e5-small + BM25", "mmlw-e5-small + kal. z notatnika + BM25",
       "e5-base", "e5-base + BM25", "bge-m3", "bge-m3 + BM25"]
SHORT = {"BM25 (Brulion)": "BM25", "e5-small": "e5s", "e5-small + kalibracja z notatnika": "e5s+kal",
         "e5-small + BM25": "e5s+B", "e5-small + kal. z notatnika + BM25": "e5s+kal+B", "mmlw-e5-small": "mmlw",
         "mmlw-e5-small + kalibracja z notatnika": "mmlw+kal", "mmlw-e5-small + BM25": "mmlw+B",
         "mmlw-e5-small + kal. z notatnika + BM25": "mmlw+kal+B", "e5-base": "e5b", "e5-base + BM25": "e5b+B",
         "bge-m3": "bge-m3", "bge-m3 + BM25": "bge+B"}
out = []


def tab(metric, names):
    head = "| korpus | notatki | zapyt. | " + " | ".join(SHORT[s] for s in SYS) + " |"
    out.append(head)
    out.append("|" + "---|" * (3 + len(SYS)))
    avg = defaultdict(list)
    for n in names:
        r = R[n]
        row = [n, str(r["n_docs"]), str(r["n_q"])]
        best = max(r["res"][s]["ALL"][metric] for s in SYS if s in r["res"])
        for s in SYS:
            v = r["res"].get(s, {}).get("ALL", {}).get(metric)
            if v is None:
                row.append("–"); continue
            avg[s].append(v)
            row.append(f"**{v:.3f}**" if abs(v - best) < 1e-9 else f"{v:.3f}")
        out.append("| " + " | ".join(row) + " |")
    out.append("| **średnia** | | | " + " | ".join(f"{sum(avg[s])/len(avg[s]):.3f}" if avg[s] else "–" for s in SYS) + " |")


real = [n for n in R if idx[n]["kind"] == "real qrels"]
gen = [n for n in R if idx[n]["kind"] != "real qrels"]
out.append("# Wyniki na wielodziedzinowej bazie notatek\n")
out.append("Legenda: e5s = multilingual-e5-small, mmlw = mmlw-e5-small, e5b = multilingual-e5-base (2,4x większy), "
           "bge-m3 (4,8x większy), +kal = kalibracja z notatnika (2 wektory), +B = fuzja z BM25 (0,8 / 0,2).\n")
for title, names in (("Zbiory z prawdziwymi zapytaniami (qrels)", real), ("Sejfy notatek, zapytania wygenerowane", gen)):
    for metric, label in (("mrr", "MRR@10"), ("r10", "Recall@10")):
        out.append(f"\n## {title} — {label}\n")
        tab(metric, names)

# categories over generated vaults (micro average)
out.append("\n## Kategorie zapytań (sejfy z wygenerowanymi zapytaniami + D&D EN), MRR@10, średnia ważona liczbą zapytań\n")
cats = ["dokladne", "odmiana", "synonim", "jezyk", "literowka", "mieszane", "realne-en (LLM)"]
out.append("| system | " + " | ".join(cats) + " |")
out.append("|" + "---|" * (1 + len(cats)))
for s in SYS:
    row = [SHORT[s]]
    for c in cats:
        num = den = 0
        for n in gen:
            m = R[n]["res"].get(s, {}).get(c)
            if m:
                num += m["mrr"] * m["n"]; den += m["n"]
        row.append(f"{num/den:.3f} ({den})" if den else "–")
    out.append("| " + " | ".join(row) + " |")
(EXP / "results_db.md").write_text("\n".join(out) + "\n", encoding="utf-8")
print("\n".join(out))
