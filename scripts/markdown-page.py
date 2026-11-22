#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Renders a Markdown document into an HTML page of the project's site (GitHub Pages).

scripts/fdroid-repo.sh makes privacy.html of docs/PRIVACY.md with it (docs/GOOGLE_PLAY.md "Privacy
policy"): the page Google Play and the app link. Python's standard library only — no Markdown
package to pin or install on the runner. It knows the small part of Markdown that document uses and
refuses anything else, so that nothing is ever published half-rendered:

* `#` … `######` headings, each with an id made of its text (ASCII, lower case, `-` between words:
  "Polityka prywatności" -> `polityka-prywatnosci`; a repeated one gets `-2`, `-3`…);
* paragraphs, `---` (a rule), `*`/`-` and `1.` lists of one level (a line indented by two spaces
  or more continues the item), tables (`| a | b |` with a `|---|---|` line under the header);
* inline: `code`, **bold**, [text](address) and <https://…>; an address is http(s), mailto, a
  `#fragment` or a relative path (never `javascript:` and the like); everything else is escaped;
* HTML comments on lines of their own are left out; `<!-- section lang="pl" -->` starts a
  <section lang="pl"> that lasts to the next such comment or the end.

Usage: scripts/markdown-page.py TEMPLATE MARKDOWN > PAGE
  TEMPLATE  an HTML file with @CONTENT@ (on a line of its own) and @TITLE@ (the first heading's
            text, escaped)
Exit status 1 with a message naming the line for Markdown it does not know.
"""

import html
import re
import sys
import unicodedata


class MarkdownError(Exception):
    pass


SAFE_URL = re.compile(r"^(https?://|mailto:|#|(?![a-zA-Z][a-zA-Z0-9+.-]*:))[^\s<>\"']*$")
INLINE = re.compile(
    r"(?P<code>`[^`]+`)"
    r"|(?P<link>\[(?P<text>[^\]]+)\]\((?P<url>[^)\s]+)\))"
    r"|(?P<auto><(?P<autourl>https?://[^>\s]+)>)"
    r"|(?P<bold>\*\*(?P<strong>[^*]+)\*\*)"
)


def url_attr(url, lineno):
    if not SAFE_URL.match(url):
        raise MarkdownError(f"line {lineno}: unsafe or unknown link address {url!r}")
    return html.escape(url, quote=True)


def inline(text, lineno, links=True):
    out, pos = [], 0
    for m in INLINE.finditer(text):
        out.append(html.escape(text[pos:m.start()], quote=False))
        pos = m.end()
        if m.group("code"):
            out.append("<code>" + html.escape(m.group("code")[1:-1], quote=False) + "</code>")
        elif m.group("link"):
            if not links:
                raise MarkdownError(f"line {lineno}: a link inside a link")
            out.append(f'<a href="{url_attr(m.group("url"), lineno)}">'
                       + inline(m.group("text"), lineno, links=False) + "</a>")
        elif m.group("auto"):
            url = m.group("autourl")
            out.append(f'<a href="{url_attr(url, lineno)}">{html.escape(url, quote=False)}</a>')
        else:
            out.append("<strong>" + inline(m.group("strong"), lineno, links) + "</strong>")
    out.append(html.escape(text[pos:], quote=False))
    return "".join(out)


def slug(text):
    plain = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text).replace("ł", "l").replace("Ł", "L")
    plain = unicodedata.normalize("NFKD", plain).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", plain.lower()).strip("-") or "section"


def cells(line, lineno):
    if "\\|" in line:
        raise MarkdownError(f"line {lineno}: an escaped | in a table")
    return [c.strip() for c in line.strip().strip("|").split("|")]


def render(markdown):
    lines = markdown.splitlines()
    out, ids, title = [], {}, None
    section_open = False
    i = 0

    def close_section():
        nonlocal section_open
        if section_open:
            out.append("</section>")
            section_open = False

    while i < len(lines):
        line, lineno = lines[i], i + 1
        stripped = line.strip()
        if not stripped:
            i += 1
            continue
        if stripped.startswith("<!--"):
            end = i
            while "-->" not in lines[end]:
                end += 1
                if end == len(lines):
                    raise MarkdownError(f"line {lineno}: an HTML comment that never ends")
            if not lines[end].rstrip().endswith("-->"):
                raise MarkdownError(f"line {end + 1}: text after the end of an HTML comment")
            m = re.fullmatch(r'<!-- section lang="([A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*)" -->', stripped)
            if m:
                close_section()
                out.append(f'<section lang="{m.group(1)}">')
                section_open = True
            i = end + 1
            continue
        if re.match(r"^(```|~~~|>|    |\t)", line) or re.match(r"^\s*<(?!https?://)", line):
            raise MarkdownError(f"line {lineno}: not supported here (code block, quote or HTML): {line!r}")
        m = re.match(r"^(#{1,6}) +(.+?) *#*$", line)
        if m:
            level, text = len(m.group(1)), m.group(2)
            base = slug(text)
            ids[base] = ids.get(base, 0) + 1
            hid = base if ids[base] == 1 else f"{base}-{ids[base]}"
            if title is None:
                title = html.escape(re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text), quote=False)
            out.append(f'<h{level} id="{hid}">{inline(text, lineno)}</h{level}>')
            i += 1
            continue
        if re.fullmatch(r"(-{3,}|\*{3,})", stripped):
            out.append("<hr>")
            i += 1
            continue
        if stripped.startswith("|"):
            if i + 1 >= len(lines) or not re.fullmatch(r"\|?(\s*:?-{3,}:?\s*\|)+\s*:?-*:?\s*\|?", lines[i + 1].strip()):
                raise MarkdownError(f"line {lineno}: a table needs a |---| line under its header")
            head = cells(line, lineno)
            rows = []
            i += 2
            while i < len(lines) and lines[i].strip().startswith("|"):
                row = cells(lines[i], i + 1)
                if len(row) != len(head):
                    raise MarkdownError(f"line {i + 1}: {len(row)} cells, the header has {len(head)}")
                rows.append((row, i + 1))
                i += 1
            out.append("<div class=\"table\"><table>")
            out.append("<thead><tr>" + "".join(f"<th>{inline(c, lineno)}</th>" for c in head) + "</tr></thead>")
            out.append("<tbody>")
            for row, rowno in rows:
                out.append("<tr>" + "".join(f"<td>{inline(c, rowno)}</td>" for c in row) + "</tr>")
            out.append("</tbody></table></div>")
            continue
        item = re.match(r"^([*-]|\d+\.) +(.*)$", line)
        if item:
            tag = "ol" if item.group(1)[0].isdigit() else "ul"
            out.append(f"<{tag}>")
            while i < len(lines):
                m = re.match(r"^([*-]|\d+\.) +(.*)$", lines[i])
                if not m or ("ol" if m.group(1)[0].isdigit() else "ul") != tag:
                    break
                start, text = i + 1, [m.group(2)]
                i += 1
                while i < len(lines) and lines[i].startswith("  ") and lines[i].strip():
                    if re.match(r"^\s+([*-]|\d+\.) ", lines[i]):
                        raise MarkdownError(f"line {i + 1}: nested lists are not supported")
                    text.append(lines[i].strip())
                    i += 1
                out.append("<li>" + inline(" ".join(text), start) + "</li>")
            out.append(f"</{tag}>")
            continue
        text = []
        while i < len(lines) and lines[i].strip() and not re.match(
                r"^(#{1,6} |[*-] |\d+\. |\||<!--|-{3,}\s*$)", lines[i]):
            if re.match(r"^(```|~~~|>|    |\t)", lines[i]):
                raise MarkdownError(f"line {i + 1}: not supported here: {lines[i]!r}")
            text.append(lines[i].strip())
            i += 1
        out.append("<p>" + inline(" ".join(text), lineno) + "</p>")
    close_section()
    if title is None:
        raise MarkdownError("no heading: the page needs a title")
    return title, "\n".join(out)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1], encoding="utf-8") as f:
        template = f.read()
    with open(argv[2], encoding="utf-8") as f:
        markdown = f.read()
    if template.count("@CONTENT@") != 1:
        sys.stderr.write(f"markdown-page: {argv[1]} needs @CONTENT@ once\n")
        return 1
    try:
        title, content = render(markdown)
    except MarkdownError as e:
        sys.stderr.write(f"markdown-page: {argv[2]}: {e}\n")
        return 1
    sys.stdout.write(template.replace("@TITLE@", title).replace("@CONTENT@", content))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
