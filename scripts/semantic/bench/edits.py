"""Gradient-free weight edits of an e5-style (XLM-R/BERT) embedding model, derived only from the
train vault. Each edit returns a new Dense (the base model is copied, never changed in place)."""
import copy, json, re, unicodedata
import numpy as np, torch
from bench import Dense, chunks_of

PL_HINT = re.compile(r"[ąćęłńóśźż]|\b(się|jest|nie|że|już|jak|przez|oraz|dla)\b", re.I)
EN_HINT = re.compile(r"\b(the|and|is|with|for|that|this|to|of|was|have)\b", re.I)


def lang(text):
    return "pl" if len(PL_HINT.findall(text)) >= len(EN_HINT.findall(text)) else "en"


def fold(s):
    s = s.replace("ł", "l").replace("Ł", "L")
    return "".join(c for c in unicodedata.normalize("NFKD", s) if not unicodedata.combining(c))


class EditedDense(Dense):
    """Dense with separate linear post-maps for queries and passages (an extra final layer)."""
    def __init__(self, base: Dense, qmap=None, pmap=None, st=None, name=""):
        super().__init__(st or base.st, base.qp, base.pp, name=name)
        self.qmap, self.pmap = qmap, pmap

    def enc(self, texts, prefix, bs=64):
        e = self.st.encode([prefix + t for t in texts], batch_size=bs, convert_to_numpy=True,
                           normalize_embeddings=True, show_progress_bar=False)
        f = self.qmap if prefix == self.qp else self.pmap
        if f is not None:
            e = f(e)
            e = e / np.linalg.norm(e, axis=1, keepdims=True)
        return e


# ---------------------------------------------------------------- statistics from the train vault
def train_stats(base: Dense, docs, queries):
    texts, langs = [], []
    for p, t in docs.items():
        for c in chunks_of(p, t):
            texts.append(c); langs.append(lang(c))
    P = base.enc(texts, base.pp)
    Q = base.enc([q["query"] for q in queries], base.qp)
    langs = np.array(langs)
    return dict(P=P, Q=Q, langs=langs, mu_p=P.mean(0), mu_q=Q.mean(0),
                lang_dir=P[langs == "pl"].mean(0) - P[langs == "en"].mean(0),
                n_pl=int((langs == "pl").sum()), n_en=int((langs == "en").sum()))


def proj_out(u):
    u = u / np.linalg.norm(u)
    return lambda e: e - np.outer(e @ u, u)


# ---------------------------------------------------------------- edit 1: output-space (closed form)
def edit_center(base, st):
    """Remove the common component (anisotropy): subtract the mean passage / mean query vector."""
    mp, mq = st["mu_p"], st["mu_q"]
    return EditedDense(base, qmap=lambda e: e - mq, pmap=lambda e: e - mp, name="center")


def edit_center_shared(base, st):
    m = (st["mu_p"] + st["mu_q"]) / 2
    return EditedDense(base, qmap=lambda e: e - m, pmap=lambda e: e - m, name="center-shared")


def edit_lang_out(base, st, center=True):
    """Project the Polish-vs-English direction out of every embedding (after centering)."""
    pr = proj_out(st["lang_dir"])
    mp, mq = (st["mu_p"], st["mu_q"]) if center else (0, 0)
    return EditedDense(base, qmap=lambda e: pr(e - mq), pmap=lambda e: pr(e - mp), name="center+lang")


def edit_whiten(base, st, k=None, eps=1e-3):
    """BERT-whitening: centre and rescale principal axes (optionally keep the top k)."""
    P = st["P"] - st["mu_p"]
    U, S, _ = np.linalg.svd(np.cov(P.T))
    W = U / np.sqrt(S + eps)
    if k:
        W = W[:, :k]
    mp, mq = st["mu_p"], st["mu_q"]
    return EditedDense(base, qmap=lambda e: (e - mq) @ W, pmap=lambda e: (e - mp) @ W, name=f"whiten{k or ''}")


# ---------------------------------------------------------------- edit 2: in-weight directional ablation
def edit_ablate_lang(base, st, layers):
    """As in the Qwen experiment of the article: remove a direction from what chosen layers write
    into the residual stream (attention output + FFN output projections, weights and biases)."""
    m = copy.deepcopy(base.st)
    bert = m[0].auto_model
    u = torch.tensor(st["lang_dir"] / np.linalg.norm(st["lang_dir"]), dtype=torch.float32, device=bert.device)
    P = torch.eye(len(u), device=bert.device) - torch.outer(u, u)
    with torch.no_grad():
        for li in layers:
            L = bert.encoder.layer[li]
            for lin in (L.attention.output.dense, L.output.dense):
                lin.weight.copy_(P @ lin.weight)
                lin.bias.copy_(P @ lin.bias)
    return EditedDense(base, st=m, name=f"ablate-lang L{layers[0]}-{layers[-1]}")


# ---------------------------------------------------------------- edit 3: token embeddings for missing diacritics
def unigram_scores(tok):
    model = json.loads(tok.backend_tokenizer.to_str())["model"]
    return {p: s for p, s in model["vocab"]}


def edit_diacritics(base, alpha=0.5, min_gap=0.0):
    """For every vocabulary piece written with Polish diacritics, pull the embedding row of its
    diacritic-free twin (if that twin exists) towards it: E[twin] = (1-a) E[twin] + a * E[piece].
    Only when the diacritic piece is more frequent than the twin by `min_gap` (log-prob units),
    so real ASCII words such as 'laska' stay mostly themselves."""
    m = copy.deepcopy(base.st)
    tok = m.tokenizer
    vocab = tok.get_vocab()
    sc = unigram_scores(tok)
    E = m[0].auto_model.embeddings.word_embeddings.weight
    groups = {}
    for p, i in vocab.items():
        f = fold(p)
        if f != p and f in vocab and re.search(r"[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]", p):
            groups.setdefault(f, []).append(p)
    n = 0
    with torch.no_grad():
        for twin, ps in groups.items():
            ps = [p for p in ps if sc.get(p, -99) - sc.get(twin, -99) >= min_gap]
            if not ps:
                continue
            w = torch.tensor([np.exp(sc[p]) for p in ps], device=E.device)
            w = w / w.sum()
            target = (E[[vocab[p] for p in ps]] * w[:, None]).sum(0)
            E[vocab[twin]] = (1 - alpha) * E[vocab[twin]] + alpha * target
            n += 1
    return EditedDense(base, st=m, name=f"diacritics a={alpha} gap={min_gap} ({n} rows)")
