"""Gradient fine-tuning baseline: in-batch negatives + one hard negative (MNRL), plain torch loop."""
import random, unicodedata
import numpy as np, torch
from sentence_transformers import SentenceTransformer
from bench import Dense, chunks_of


def strip_diacritics(s):
    s = s.replace("ł", "l").replace("Ł", "L")
    return "".join(c for c in unicodedata.normalize("NFKD", s) if not unicodedata.combining(c))


def typo(s, rng):
    w = list(s)
    if len(w) < 4:
        return s
    i = rng.randrange(1, len(w) - 1)
    op = rng.choice("swap del dup".split())
    if op == "swap":
        w[i], w[i + 1 if i + 1 < len(w) else i - 1] = w[i + 1 if i + 1 < len(w) else i - 1], w[i]
    elif op == "del":
        del w[i]
    else:
        w.insert(i, w[i])
    return "".join(w)


def make_pairs(base: Dense, docs, queries, augment=True, seed=0):
    """(query, positive chunk, hard negative chunk) using the base model to pick chunks."""
    rng = random.Random(seed)
    base.index(docs)
    Q = base.enc([q["query"] for q in queries], base.qp)
    S = Q @ base.C.T
    pairs = []
    for qi, q in enumerate(queries):
        rel = {base.ids.index(r) for r in q["relevant"]}
        pos_idx = [c for c in range(len(base.owner)) if base.owner[c] in rel]
        pos = max(pos_idx, key=lambda c: S[qi, c])
        neg_idx = [c for c in np.argsort(-S[qi]) if base.owner[c] not in rel][:3]
        neg = rng.choice(neg_idx)
        variants = [q["query"]]
        if augment:
            variants.append(strip_diacritics(q["query"]))
            variants.append(typo(q["query"], rng))
        for v in dict.fromkeys(variants):
            pairs.append((v, base.chunk_texts[pos], base.chunk_texts[neg]))
    return pairs


def finetune(model_name, pairs, qp="query: ", pp="passage: ", epochs=3, lr=3e-5, bs=32, scale=20.0,
             seed=0, device="cuda"):
    torch.manual_seed(seed)
    rng = random.Random(seed)
    m = SentenceTransformer(model_name, device=device)
    m.max_seq_length = 256
    m.train()
    # word embeddings frozen: 96M of 118M parameters, keeps memory low and the vocabulary prunable
    m[0].auto_model.embeddings.word_embeddings.weight.requires_grad_(False)
    opt = torch.optim.AdamW([p for p in m.parameters() if p.requires_grad], lr=lr, weight_decay=0.01)
    steps = epochs * ((len(pairs) + bs - 1) // bs)
    sched = torch.optim.lr_scheduler.LambdaLR(opt, lambda s: min(1.0, (s + 1) / (0.1 * steps)) * max(0.0, 1 - s / steps))

    def emb(texts):
        f = m.tokenize(texts)
        f = {k: v.to(device) for k, v in f.items() if isinstance(v, torch.Tensor)}
        return torch.nn.functional.normalize(m(f)["sentence_embedding"], dim=-1)

    for ep in range(epochs):
        rng.shuffle(pairs)
        tot = 0
        for i in range(0, len(pairs), bs):
            b = pairs[i:i + bs]
            q = emb([qp + x[0] for x in b])
            d = emb([pp + x[1] for x in b] + [pp + x[2] for x in b])
            logits = q @ d.T * scale
            # mask false negatives: another column holding this row's positive text
            cols = [x[1] for x in b] + [x[2] for x in b]
            mask = torch.tensor([[j != i and cols[j] == b[i][1] for j in range(len(cols))]
                                 for i in range(len(b))], device=device)
            logits = logits.masked_fill(mask, -1e4)
            loss = torch.nn.functional.cross_entropy(logits, torch.arange(len(b), device=device))
            opt.zero_grad(); loss.backward(); opt.step(); sched.step()
            tot += loss.item() * len(b)
        print(f"  epoch {ep+1}: loss {tot/len(pairs):.3f}")
    m.eval()
    return m
