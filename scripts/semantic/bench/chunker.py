"""Python twin of src/core/semantic/chunker.cpp (chunkNote), so that GPU runs match the C++ prototype."""
import re


def chunk_note(title, content, max_words=120):
    lines = [l[:-1] if l.endswith("\r") else l for l in content.split("\n")]
    start = 0
    if lines and lines[0].strip() == "---":
        for i in range(1, len(lines)):
            if lines[i].strip() == "---":
                start = i + 1
                break
    parts, cur = [], []

    def flush():
        t = "\n".join(cur).strip()
        if t:
            words = t.split()
            if len(words) > max_words:
                for i in range(0, len(words), max_words):
                    parts.append((" ".join(words[i:i + max_words]), min(max_words, len(words) - i)))
            else:
                parts.append((t, len(words)))
        cur.clear()

    for l in lines[start:]:
        if not l.strip():
            flush(); continue
        if l.startswith("#") or l.startswith("- "):
            flush()
        cur.append(l)
    flush()
    chunks, text, words = [], "", 0
    for t, w in parts:
        if text and words + w > max_words:
            chunks.append(title + "\n" + text); text, words = "", 0
        text = t if not text else text + " " + t
        words += w
    if text or not chunks:
        chunks.append(title + "\n" + text)
    return chunks
