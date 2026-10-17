#!/usr/bin/env python3
"""Builds a small, representative Trilium document through its HTTP APIs and exports it.

    ./run.sh python3 generate.py <output dir>

Written for Trilium 0.106 (also runs against TriliumNext 0.99). Run it against a throwaway,
uninitialised server (run.sh): it creates the document (Polish UI locale, demo content removed),
fills the tree through ETAPI, protects one note through the internal API (ETAPI cannot) and writes
the exports. All content is synthetic.

Text note content is written the way Trilium's CKEditor 5 stores it (one line of HTML with the
editor's element and class names, list item ids included), so the exports are what the real
application produces for such notes. The mermaid/canvas SVG attachments are added the way the web
client adds them after rendering.
"""

import json
import os
import sys

from trilium_api import Trilium, pdf, png, save, todo

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"
t = Trilium(os.environ.get("TRILIUM_URL", "http://127.0.0.1:37841"), "brulion-fixture", 20261016)
info = t.setup("/api/setup/new-document?skipDemoDb=1", {"locale": "pl"})
print("Trilium", info["appVersion"], "db", info["dbVersion"], "sync", info["syncVersion"])
t.remove_demo()

# Ids decided up front, so that content can link to notes created later.
t.plan("projekty", "ogrod", "zadania1", "zadania2", "kod", "skrypt", "dane", "markdown", "python",
       "biblioteka", "rozdzial1", "rozdzial2", "diagram", "szkic", "obraz", "dokument", "spotkanie",
       "pytajniki", "archiwum", "sekret", "wykluczona", "zarchiwizowana", "dlugi", "osoby", "jan",
       "szablony", "szablon", "inne", "wyszukiwanie", "mapa_relacji", "mapa_notatek", "www",
       "render", "widok", "mapa_mysli", "pusta")

SVG_MERMAID = ('<svg xmlns="http://www.w3.org/2000/svg" width="120" height="40" viewBox="0 0 120 40">'
               '<rect x="1" y="1" width="50" height="38" fill="#eee" stroke="#333"/>'
               '<text x="8" y="25" font-size="12">Siew</text>'
               '<line x1="51" y1="20" x2="69" y2="20" stroke="#333"/>'
               '<rect x="69" y="1" width="50" height="38" fill="#eee" stroke="#333"/>'
               '<text x="76" y="25" font-size="12">Zbiór</text></svg>')
SVG_CANVAS = ('<svg xmlns="http://www.w3.org/2000/svg" width="100" height="60" viewBox="0 0 100 60">'
              '<rect x="5" y="5" width="90" height="50" fill="none" stroke="#1e1e1e"/>'
              '<text x="20" y="35" font-size="14">Grządka</text></svg>')


def li(text):
    return f'<li data-list-item-id="{t.li_id()}">{text}</li>'


def ref(title, *keys, query=""):
    return f'<a class="reference-link" href="{t.path(*keys)}{query}">{title}</a>'


# --- Projekty ------------------------------------------------------------------------------------

t.note("projekty", "root", "Projekty",
       content='<p>Wszystkie bieżące projekty. Zobacz też <a class="reference-link" href="'
               + t.path("archiwum") + '">Archiwum</a>.</p>')
t.request("PATCH", f"/etapi/branches/root_{t.nid('projekty')}", {"isExpanded": True})
t.label("projekty", "projekt")
t.label("projekty", "status", "w toku", inheritable=True)

t.note("ogrod", "projekty", "Ogród: plan/budżet?")  # content below, once its attachments exist
t.note("zadania1", "projekty", "Zadania", content=todo(t, [
    ("Kupić nasiona marchwi", True),
    ("Przekopać grządkę (ok. 2 h)", False),
    ("Zamówić <strong>kompostownik</strong>", False, "doing", "W trakcie"),
    ("Szklarnia?", False, "maybe", "Może"),
    ("Oczko wodne", False, "cancelled", "Anulowane"),
], nested=todo(t, [("zagnieżdżone: wybrać folię", False)])))
t.note("zadania2", "projekty", "Zadania",
       content="<p>Drugi notatnik o tym samym tytule (rodzeństwo z duplikatem nazwy).</p>"
               + todo(t, [("Zrobione zadanie", True, "done", "Zrobione")]))

t.note("kod", "projekty", "Kod")  # empty content + children: exported as a directory only
t.note("skrypt", "kod", "skrypt.js", "code", mime="application/javascript;env=frontend",
       content='// Licznik grządek\nconst grządki = ["marchew", "pietruszka"];\n'
               'api.log(`Liczba: ${grządki.length}`);\n')
t.note("dane", "kod", "dane", "code", mime="application/json",
       content='{\n  "roślina": "pomidor",\n  "ilość": 12,\n  "cena": "12,50 zł"\n}\n')
t.note("markdown", "kod", "Notatka Markdown", "code", mime="text/x-markdown",
       content="# Notatka w Markdown\n\nLink do [planu ogrodu](" + t.path("projekty", "ogrod")
               + ") i **pogrubienie**.\n\n- [ ] zadanie w Markdown\n- [x] zrobione\n")
t.note("python", "kod", "Analiza plonów", "code", mime="text/x-python",
       content='def plon(kg: float) -> str:\n    return f"Zebrano {kg} kg — świetnie!"\n')

t.note("biblioteka", "projekty", "Biblioteka", "book")
t.label("biblioteka", "viewType", "list")
t.note("rozdzial1", "biblioteka", "Rozdział 1: Gleba", prefix="Część I",
       content="<p>Gleba powinna być <em>przepuszczalna</em>.</p>")
t.note("rozdzial2", "biblioteka", "Rozdział 2: Nawożenie",
       content="<p>Kompost &gt; nawozy sztuczne &amp; inne.</p>")

t.note("diagram", "projekty", "Diagram przepływu", "mermaid",
       content="graph LR\n    A[Siew] --> B[Zbiór]\n")
t.attachment("diagram", "image", "image/svg+xml", "mermaid-export.svg", SVG_MERMAID)
t.note("szkic", "projekty", "Szkic grządek", "canvas", content=json.dumps({
    "type": "excalidraw", "version": 2, "elements": [
        {"id": "r1", "type": "rectangle", "x": 5, "y": 5, "width": 90, "height": 50,
         "strokeColor": "#1e1e1e", "backgroundColor": "transparent", "version": 1},
        {"id": "t1", "type": "text", "x": 20, "y": 22, "width": 60, "height": 20,
         "text": "Grządka", "fontSize": 14, "version": 1}],
    "files": {}, "appState": {"scrollX": 0, "scrollY": 0, "zoom": {"value": 1}}},
    ensure_ascii=False))
t.attachment("szkic", "image", "image/svg+xml", "canvas-export.svg", SVG_CANVAS)

t.note("obraz", "projekty", "kropka.png", "image", mime="image/png")
t.set_content("obraz", png(2, 2, (34, 139, 34, 255)))
t.label("obraz", "originalFileName", "kropka.png")
t.note("dokument", "projekty", "umowa.pdf", "file", mime="application/pdf")
t.set_content("dokument", pdf("Umowa dzierzawy dzialki"))
t.label("dokument", "originalFileName", "umowa.pdf")

t.note("spotkanie", "projekty", "Spotkanie 2026-10-14",
       content="<p>Notatka sklonowana do Archiwum (ta sama notatka w dwóch miejscach drzewa).</p>")
t.note("pytajniki", "projekty", "???",
       content="<p>Tytuł złożony wyłącznie z niedozwolonych znaków.</p>")

# --- Archiwum, Szablony, Osoby -------------------------------------------------------------------

t.note("archiwum", "root", "Archiwum", content="<p>Stare rzeczy.</p>")
t.note("sekret", "archiwum", "Sekretne hasła",
       content="<p>Tajne: <strong>zażółć gęślą jaźń</strong></p>")
t.clone("spotkanie", "archiwum", prefix="2026")
t.note("wykluczona", "archiwum", "Nie eksportuj",
       content="<p>Ta notatka ma #excludeFromExport.</p>")
t.label("wykluczona", "excludeFromExport")
t.note("zarchiwizowana", "archiwum", "Zarchiwizowana notatka",
       content="<p>Ukryta w drzewie przez #archived, ale eksportowana.</p>")
t.label("zarchiwizowana", "archived")
t.note("dlugi", "archiwum",
       "Bardzo długi tytuł notatki o planowaniu ogrodu warzywnego na sezon 2026 i dalej",
       content="<p>Długi tytuł (starsze wersje Trilium obcinały nazwy plików do 30 znaków).</p>")

t.note("szablony", "root", "Szablony")
t.note("szablon", "szablony", "Osoba (szablon)", content="<p>Szablon notatki o osobie.</p>")
t.label("szablon", "template")
t.label("szablon", "label:telefon", "promoted,single,text")
t.label("szablon", "label:urodziny", "promoted,single,date")
t.label("szablon", "relation:pracodawca", "promoted,single")
t.note("osoby", "root", "Osoby")
t.note("jan", "osoby", "Jan Kowalski", content="<p>Fikcyjna osoba do testów.</p>")
t.relation("jan", "template", "szablon")
t.label("jan", "telefon", "000 000 000")
t.label("jan", "urodziny", "1990-01-31")
t.label("jan", "opis", 'Ma "cudzysłowy", przecinki, #hasze i: dwukropki')
t.relation("jan", "pracodawca", "projekty")

# --- other note types ----------------------------------------------------------------------------

t.note("inne", "root", "Inne typy")
t.note("wyszukiwanie", "inne", "Wyszukiwanie: projekty", "search")
t.label("wyszukiwanie", "searchString", "#projekt")
t.note("mapa_relacji", "inne", "Mapa relacji", "relationMap")
t.set_content("mapa_relacji", json.dumps({
    "notes": [{"noteId": t.nid("jan"), "x": 40, "y": 40},
              {"noteId": t.nid("projekty"), "x": 300, "y": 40}],
    "transform": {"x": 0, "y": 0, "scale": 1}}))
t.note("mapa_notatek", "inne", "Mapa notatek", "noteMap")
t.note("www", "inne", "Strona WWW", "webView")
t.label("www", "webViewSrc", "https://example.org/")
t.note("widok", "inne", "widok.html", "code", mime="text/html",
       content="<p>Widok renderowany: <b>ąęś</b></p>\n")
t.note("render", "inne", "Notatka renderowana", "render")
t.relation("render", "renderNote", "widok")
t.note("mapa_mysli", "inne", "Mapa myśli", "mindMap", content=json.dumps({
    "nodeData": {"id": "root", "topic": "Ogród", "children": [
        {"id": "n1", "topic": "Warzywa", "children": []}, {"id": "n2", "topic": "Kwiaty"}]},
    "arrows": [], "summaries": [], "direction": 2}, ensure_ascii=False))
t.note("pusta", "inne", "Pusta notatka")  # empty text leaf: still exported

# --- day notes -----------------------------------------------------------------------------------

t.ids["2026-10-14"] = t.day("2026-10-14")
t.ids["2026-10-16"] = t.day("2026-10-16")
t.ids["2025-12-31"] = t.day("2025-12-31")
day_path = t.ancestors(t.ids["2026-10-16"])  # calendar root, year, month, day
t.set_content("2026-10-14",
              "<p>Spotkanie w sprawie działki: " + ref("Spotkanie 2026-10-14", "projekty", "spotkanie")
              + ".</p>" + todo(t, [("Zadzwonić do sąsiada", True)]))
t.set_content("2026-10-16",
              "<p>Dziś: <strong>sadzenie czosnku</strong>.</p>"
              + todo(t, [("Posadzić czosnek o 10:00", False),
                         ("Grabienie liści", False, "doing", "W trakcie")])
              + "<p>Zobacz " + ref("Zadania", "projekty", "zadania1") + ".</p>")
t.set_content("2025-12-31", "<p>Podsumowanie roku. Sylwester 🎆</p>")
t.label("2026-10-16", "nastrój", "dobry")

# --- the rich note -------------------------------------------------------------------------------

att_img = t.attachment("ogrod", "image", "image/png", "zdjęcie grządki.png",
                       png(4, 2, (200, 60, 30, 255)))
att_txt = t.attachment("ogrod", "file", "text/plain", "lista zakupów.txt",
                       "Marchew\nPietruszka\nŻółte pomidory\n".encode(), position=20)
t.label("ogrod", "priorytet", "wysoki")
t.label("ogrod", "ważne")
t.label("ogrod", "iconClass", "bx bx-leaf")
t.relation("ogrod", "autor", "jan")
t.relation("ogrod", "inspiracja", "rozdzial1")


RICH = (
    '<p>Plan ogrodu na rok 2026. Tekst <strong>pogrubiony</strong>, <em>kursywa</em>, '
    '<u>podkreślenie</u>, <del>przekreślenie</del>, <code>kod_w_linii()</code>, '
    'H<sub>2</sub>O, m<sup>2</sup>, <kbd>Ctrl</kbd>+<kbd>S</kbd>, '
    '<span style="background-color:hsl(60, 75%, 60%);">zaznaczenie</span>, '
    '<span style="color:hsl(0,75%,60%);">czerwony tekst</span> i emoji 🌱.</p>'
    '<p>Znaki specjalne: &lt;script&gt;alert(1)&lt;/script&gt;, *gwiazdki*, _podkreślniki_, '
    '[nawiasy], a&amp;b, 100% i \\ukośnik.</p>'
    '<p># To nie jest nagłówek</p>'
    '<h2>Nagłówek drugiego poziomu</h2>'
    '<h3>Nagłówek trzeci — zażółć gęślą jaźń</h3>'
    '<h4>Czwarty</h4>'
    f'<ul><li data-list-item-id="{t.li_id()}">Warzywa<ul>{li("marchew")}{li("pietruszka")}</ul></li>'
    f'{li("Kwiaty")}</ul>'
    f'<ol>{li("Pierwszy krok")}{li("Drugi krok")}</ol>'
    f'<ol start="5">{li("Lista od piątki")}</ol>'
    + todo(t, [("Zrobione", True), ("Do zrobienia", False)]) +
    '<figure class="table"><table><thead><tr><th>Roślina</th><th>Koszt</th></tr></thead>'
    '<tbody><tr><td>Pomidory</td><td>12,50 zł</td></tr>'
    '<tr><td><strong>Ogórki</strong> | gruntowe</td><td>8 zł</td></tr></tbody></table>'
    '<figcaption>Budżet</figcaption></figure>'
    '<pre><code class="language-text-x-python">def podlej(grządka):\n'
    '    return "podlano: " + grządka\n</code></pre>'
    '<pre><code class="language-application-javascript-env-frontend">'
    'const a = 1 &lt; 2 &amp;&amp; true;</code></pre>'
    '<pre><code class="language-text-plain">zwykły tekst\n  z wcięciem</code></pre>'
    '<pre spellcheck="false"><code class="language-mermaid">graph TD\n'
    '    A[Start] --&gt; B[Koniec]</code></pre>'
    '<blockquote><p>Cytat: „Kto sieje wiatr, ten zbiera burzę.”</p></blockquote>'
    '<aside class="admonition tip"><p>Wskazówka: podlewaj rano.</p></aside>'
    '<aside class="admonition warning"><p>Uwaga na <strong>przymrozki</strong>!</p></aside>'
    '<p>Wzór w linii <span class="math-tex">\\(E=mc^2\\)</span> i blokowy:</p>'
    '<span class="math-tex">\\[\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}\\]</span>'
    '<p>Przypis do tekstu<span class="footnote-reference" data-footnote-reference="" '
    'data-footnote-index="1" data-footnote-id="k3m9x2a7pq" role="doc-noteref" '
    'id="fnrefk3m9x2a7pq"><sup><a href="#fnk3m9x2a7pq">[1]</a></sup></span>.</p>'
    '<details class="trilium-collapsible"><summary>Szczegóły (zwijane)</summary>'
    '<p>Ukryta treść.</p></details>'
    '<hr>'
    '<h2>Linki</h2>'
    '<p>Odnośnik referencyjny: ' + ref("Spotkanie 2026-10-14", "projekty", "spotkanie") + '.</p>'
    '<p>Zwykły link wewnętrzny z własnym tekstem: <a href="' + t.path("osoby", "jan")
    + '">autor planu</a>; do klonu przez drugą ścieżkę: <a href="'
    + t.path("archiwum", "spotkanie") + '">spotkanie w archiwum</a>.</p>'
    '<p>Do innego poddrzewa: ' + ref("Osoba (szablon)", "szablony", "szablon")
    + ', do folderu: ' + ref("Kod", "projekty", "kod")
    + ', do kodu: ' + ref("skrypt.js", "projekty", "kod", "skrypt")
    + ', do duplikatu: ' + ref("Zadania", "projekty", "zadania2")
    + ', do dnia: ' + ref("16 - Piątek", *day_path) + '.</p>'
    '<p>Do notatki chronionej: ' + ref("Sekretne hasła", "archiwum", "sekret")
    + ', do wykluczonej: ' + ref("Nie eksportuj", "archiwum", "wykluczona")
    + ', do nieistniejącej: <a class="reference-link" href="#root/Brakuj4cy123">[missing note]</a>.</p>'
    '<p>Zewnętrzny: <a href="https://example.org/ścieżka?a=1&amp;b=2">example.org</a>, '
    'mail: <a href="mailto:ogrod@example.org">ogrod@example.org</a>.</p>'
    '<p>Załącznik (plik): '
    + ref("lista zakupów.txt", "projekty", "ogrod",
          query=f"?viewMode=attachments&amp;attachmentId={att_txt}") + '.</p>'
    '<p>Podgląd linku: <span class="link-mention" data-url="https://example.org/" '
    'data-embed-type="opengraph" data-title="Example Domain"></span> w zdaniu.</p>'
    '<section class="link-embed" data-url="https://example.org/artykul" '
    'data-embed-type="opengraph" data-title="Artykuł o ogrodach" '
    'data-description="Opis artykułu" data-site-name="Example"></section>'
    '<p>Ikona w linii <span class="tn-icon bx bx-star"></span> w tekście.</p>'
    '<h2>Obrazy i osadzenia</h2>'
    '<figure class="image image-style-align-left"><img style="aspect-ratio:4/2;" '
    f'src="api/attachments/{att_img}/image/zdj%C4%99cie%20grz%C4%85dki.png" width="4" height="2">'
    '<figcaption>Zdjęcie grządki (załącznik)</figcaption></figure>'
    f'<p>Obraz z notatki-obrazu w linii: <img src="api/images/{t.nid("obraz")}/kropka.png"></p>'
    f'<figure class="image"><img src="api/images/{t.nid("diagram")}/Diagram%20przep%C5%82ywu.svg">'
    '</figure>'
    f'<section class="include-note" data-note-id="{t.nid("zadania1")}" data-box-size="medium">'
    '&nbsp;</section>'
    '<ol class="footnote-section footnotes" data-footnote-section="" role="doc-endnotes">'
    '<li class="footnote-item" data-footnote-item="" data-footnote-index="1" '
    'data-footnote-id="k3m9x2a7pq" role="doc-endnote" id="fnk3m9x2a7pq">'
    '<span class="footnote-back-link" data-footnote-back-link="" data-footnote-id="k3m9x2a7pq">'
    '<sup><strong><a href="#fnrefk3m9x2a7pq">^</a></strong></sup></span>'
    '<div class="footnote-content" data-footnote-content="">'
    '<p>Treść przypisu z <em>ogonkami</em>: ąćęłńóśźż.</p></div></li></ol>'
)

t.set_content("ogrod", RICH)

# Plain content PUTs for the remaining text notes, as the editor's saves would do (this is what
# creates the link relations; only includeNoteLink/relationMapLink survive into the export).
for key in ["projekty", "zadania1", "zadania2", "spotkanie", "archiwum", "dlugi"]:
    current = t.request("GET", f"/etapi/notes/{t.nid(key)}/content", raw=True).decode()
    t.set_content(key, current)

# --- protect one note (internal API with a web session; ETAPI cannot) ---------------------------

t.web_login()
t.request("POST", "/api/login/protected", {"password": t.password})
t.request("PUT", f"/api/notes/{t.nid('sekret')}/protect/1", {})

# --- exports -------------------------------------------------------------------------------------

# With the protected session still open the protected note is exported decrypted.
save(OUT, "archiwum-protected-session.zip", t.export("archiwum", "html"))
t.request("POST", "/api/logout/protected", {})
# Without it, it is a "[protected]" entry without a data file.
save(OUT, "html.zip", t.export("root", "html"))
save(OUT, "markdown.zip", t.export("root", "markdown"))
save(OUT, "subtree-projekty-html.zip", t.export("projekty", "html"))
save(OUT, "subtree-projekty-markdown.zip", t.export("projekty", "markdown"))
branch = f"{t.nid('projekty')}_{t.nid('ogrod')}"
save(OUT, "single-ogrod.html", t.export_web(branch, "single", "html", "fixture1"))
save(OUT, "single-ogrod.md", t.export_web(branch, "single", "markdown", "fixture2"))
save(OUT, "projekty.opml", t.export_web(f"root_{t.nid('projekty')}", "subtree", "opml", "fixture3"))
with open(os.path.join(OUT, "ids.json"), "w") as f:
    json.dump({"appVersion": info["appVersion"], "dbVersion": info["dbVersion"],
               "notes": t.ids, "attachments": {"image": att_img, "file": att_txt}},
              f, ensure_ascii=False, indent=1)
