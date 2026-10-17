#!/usr/bin/env python3
"""The same kind of document for zadam/trilium 0.63 (the last release before TriliumNext).

    LEGACY=1 ./run.sh python3 generate_legacy.py <output dir>

0.63 always creates its demo document; it is deleted first so that the root export holds only the
notes below. Content uses the markup of that version's CKEditor build (<i>, <s>,
<mark class="marker-yellow">, no list item ids, no admonitions/footnotes/task states).
"""

import json
import os
import sys

from trilium_api import Trilium, pdf, png, save, todo

OUT = sys.argv[1] if len(sys.argv) > 1 else "out-legacy"
t = Trilium(os.environ.get("TRILIUM_URL", "http://127.0.0.1:37841"), "brulion-fixture", 20230063)
info = t.setup()
print("Trilium", info["appVersion"], "db", info["dbVersion"], "sync", info["syncVersion"])
t.remove_demo()

t.plan("projekty", "ogrod", "zadania1", "zadania2", "kod", "skrypt", "python", "diagram", "szkic",
       "obraz", "dokument", "spotkanie", "dlugi", "archiwum", "sekret", "osoby", "jan")


def ref(title, *keys, query=""):
    return f'<a class="reference-link" href="{t.path(*keys)}{query}">{title}</a>'


t.note("projekty", "root", "Projekty",
       content="<p>Wszystkie bieżące projekty. Zobacz też " + ref("Archiwum", "archiwum") + ".</p>")
t.label("projekty", "projekt")
t.label("projekty", "status", "w toku", inheritable=True)
t.note("ogrod", "projekty", "Ogród: plan/budżet?")
t.note("zadania1", "projekty", "Zadania", content=todo(None, [
    ("Kupić nasiona marchwi", True), ("Przekopać grządkę", False)]))
t.note("zadania2", "projekty", "Zadania", content="<p>Duplikat tytułu.</p>")
t.note("kod", "projekty", "Kod")
t.note("skrypt", "kod", "skrypt.js", "code", mime="application/javascript;env=frontend",
       content='const grządki = ["marchew"];\n')
t.note("python", "kod", "Analiza plonów", "code", mime="text/x-python",
       content='print("świetnie")\n')
t.note("diagram", "projekty", "Diagram przepływu", "mermaid", content="graph LR\n    A --> B\n")
t.note("szkic", "projekty", "Szkic grządek", "canvas", content=json.dumps(
    {"type": "excalidraw", "version": 2, "elements": [], "files": {}, "appState": {}}))
t.note("obraz", "projekty", "kropka.png", "image", mime="image/png")
t.set_content("obraz", png(2, 2, (34, 139, 34, 255)))
t.label("obraz", "originalFileName", "kropka.png")
t.note("dokument", "projekty", "umowa.pdf", "file", mime="application/pdf")
t.set_content("dokument", pdf("Umowa"))
t.label("dokument", "originalFileName", "umowa.pdf")
t.note("spotkanie", "projekty", "Spotkanie 2026-10-14", content="<p>Sklonowana notatka.</p>")
t.note("dlugi", "projekty",
       "Bardzo długi tytuł notatki o planowaniu ogrodu warzywnego na sezon 2026 i dalej",
       content="<p>Ta wersja obcina nazwy plików z danymi do 30 znaków.</p>")

t.note("archiwum", "root", "Archiwum", content="<p>Stare rzeczy.</p>")
t.note("sekret", "archiwum", "Sekretne hasła", content="<p>Tajne: <strong>zażółć</strong></p>")
t.clone("spotkanie", "archiwum", prefix="2026")
t.note("osoby", "root", "Osoby")
t.note("jan", "osoby", "Jan Kowalski", content="<p>Fikcyjna osoba.</p>")
t.label("jan", "telefon", "000 000 000")

att_img = t.attachment("ogrod", "image", "image/png", "zdjęcie grządki.png",
                       png(4, 2, (200, 60, 30, 255)))
att_txt = t.attachment("ogrod", "file", "text/plain", "lista zakupów.txt", "Marchew\n".encode(),
                       position=20)
t.label("ogrod", "priorytet", "wysoki")
t.label("ogrod", "ważne")
t.relation("ogrod", "autor", "jan")

t.ids["2026-10-16"] = t.day("2026-10-16")
t.set_content("2026-10-16", "<p>Dziś: <strong>sadzenie czosnku</strong>.</p>"
              + todo(None, [("Posadzić czosnek", False)]))
day_path = t.ancestors(t.ids["2026-10-16"])

t.set_content("ogrod", (
    '<p>Tekst <strong>pogrubiony</strong>, <i>kursywa</i>, <u>podkreślenie</u>, '
    '<s>przekreślenie</s>, <code>kod()</code>, H<sub>2</sub>O, '
    '<mark class="marker-yellow">zaznaczenie</mark>, '
    '<span style="color:hsl(0,75%,60%);">czerwony</span> 🌱.</p>'
    '<p>Znaki: &lt;b&gt;, *gwiazdki*, _podkreślniki_, [nawiasy], a&amp;b.</p>'
    '<h2>Nagłówek 2</h2><h3>Nagłówek 3</h3>'
    '<ul><li>Warzywa<ul><li>marchew</li></ul></li><li>Kwiaty</li></ul>'
    '<ol><li>Pierwszy</li><li>Drugi</li></ol>'
    + todo(None, [("Zrobione", True), ("Do zrobienia", False)]) +
    '<figure class="table"><table><thead><tr><th>Roślina</th><th>Koszt</th></tr></thead>'
    '<tbody><tr><td>Pomidory</td><td>12,50 zł</td></tr></tbody></table></figure>'
    '<pre><code class="language-text-x-python">def f():\n    return "żółw"</code></pre>'
    '<blockquote><p>Cytat ąę.</p></blockquote>'
    '<p>Wzór <span class="math-tex">\\(E=mc^2\\)</span>.</p>'
    '<hr>'
    '<p>Linki: ' + ref("Spotkanie 2026-10-14", "projekty", "spotkanie")
    + ', <a href="' + t.path("osoby", "jan") + '">autor</a>, '
    + ref("Kod", "projekty", "kod") + ', ' + ref("Zadania", "projekty", "zadania2") + ', '
    + ref("Sekretne hasła", "archiwum", "sekret") + ', ' + ref("16 - Friday", *day_path) + ', '
    + ref("Bardzo długi tytuł", "projekty", "dlugi") + ', '
    '<a href="https://example.org/">example.org</a>, załącznik '
    + ref("lista zakupów.txt", "projekty", "ogrod",
          query=f"?viewMode=attachments&amp;attachmentId={att_txt}") + '.</p>'
    f'<figure class="image"><img src="api/attachments/{att_img}/image/zdj%C4%99cie%20grz%C4%85dki.png">'
    '</figure>'
    f'<p><img src="api/images/{t.nid("obraz")}/kropka.png"></p>'
    f'<section class="include-note" data-note-id="{t.nid("zadania1")}" data-box-size="small">'
    '&nbsp;</section>'))

t.web_login()
t.request("POST", "/api/login/protected", {"password": t.password})
t.request("PUT", f"/api/notes/{t.nid('sekret')}/protect/1", {})
t.request("POST", "/api/logout/protected", {})

save(OUT, "0.63-html.zip", t.export("root", "html"))
save(OUT, "0.63-markdown.zip", t.export("root", "markdown"))
with open(os.path.join(OUT, "ids-0.63.json"), "w") as f:
    json.dump({"appVersion": info["appVersion"], "dbVersion": info["dbVersion"],
               "notes": t.ids, "attachments": {"image": att_img, "file": att_txt}},
              f, ensure_ascii=False, indent=1)
