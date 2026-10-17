"""Minimal client for a throwaway Trilium server (ETAPI + the few internal routes ETAPI lacks).

Shared by generate.py (current Trilium) and generate_legacy.py (zadam/trilium 0.63). Standard
library only.
"""

import json
import os
import random
import re
import struct
import urllib.error
import urllib.parse
import urllib.request
import zlib
from http.cookiejar import CookieJar

ALNUM = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"


class Trilium:
    def __init__(self, base, password, seed):
        self.base = base
        self.password = password
        self.rng = random.Random(seed)
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(CookieJar()))
        self.token = None
        self.csrf = None
        self.ids = {}

    # --- transport ---------------------------------------------------------------------------

    def request(self, method, path, body=None, ctype=None, raw=False, missing_ok=False):
        data = None
        hdrs = {}
        if body is not None:
            if isinstance(body, (dict, list)):
                data = json.dumps(body).encode()
                hdrs["Content-Type"] = "application/json"
            elif isinstance(body, str):
                data = body.encode()
                hdrs["Content-Type"] = ctype or "text/plain; charset=utf-8"
            else:
                data = body
                hdrs["Content-Type"] = ctype or "application/octet-stream"
        if self.token and path.startswith("/etapi"):
            hdrs["Authorization"] = self.token
        if self.csrf and path.startswith("/api"):
            hdrs["x-csrf-token"] = self.csrf
        req = urllib.request.Request(self.base + path, data=data, method=method, headers=hdrs)
        try:
            with self.opener.open(req) as resp:
                payload = resp.read()
        except urllib.error.HTTPError as e:
            if missing_ok and e.code == 404:
                return None
            raise SystemExit(f"{method} {path} -> {e.code}: {e.read()[:500]!r}")
        if raw:
            return payload
        if not payload:
            return None
        try:
            return json.loads(payload)
        except ValueError:
            return payload

    def form(self, path, fields):
        return self.request("POST", path, urllib.parse.urlencode(fields),
                            ctype="application/x-www-form-urlencoded")

    # --- setup and sessions ------------------------------------------------------------------

    def setup(self, new_document_path="/api/setup/new-document", body=None):
        status = self.request("GET", "/api/setup/status")
        if status.get("isInitialized"):
            raise SystemExit("server already initialised; use a fresh data directory")
        self.request("POST", new_document_path, body if body is not None else {})
        self.form("/set-password", {"password1": self.password, "password2": self.password})
        self.token = self.request("POST", "/etapi/auth/login",
                                  {"password": self.password, "tokenName": "fixtures"})["authToken"]
        return self.request("GET", "/etapi/app-info")

    def web_login(self):
        """Logs a browser-like session in (needed for protecting notes and single-note export).

        The CSRF token is rendered into the main page by older versions and served by
        /bootstrap (JSON) by newer ones.
        """
        self.form("/login", {"password": self.password})
        page = self.request("GET", "/", raw=True).decode("utf-8", "replace")
        found = re.search(r"csrfToken[\"']?\s*[:=]\s*[\"']([^\"']+)[\"']", page)
        self.csrf = found.group(1) if found else self.request("GET", "/bootstrap")["csrfToken"]

    # --- ids ---------------------------------------------------------------------------------

    def new_id(self, n=12):
        """Trilium entity ids are 12 random alphanumerics; seeded so reruns produce the same ids."""
        return "".join(self.rng.choice(ALNUM) for _ in range(n))

    def plan(self, *keys):
        for k in keys:
            self.ids[k] = self.new_id()

    def nid(self, key):
        return self.ids.get(key, key)

    def path(self, *keys):
        return "#root/" + "/".join(self.nid(k) for k in keys)

    def ancestors(self, note_id):
        """The note path below root (first parents), as the editor writes it into links."""
        chain = []
        while note_id != "root":
            chain.insert(0, note_id)
            note_id = self.request("GET", f"/etapi/notes/{note_id}")["parentNoteIds"][0]
        return chain

    def li_id(self):
        """CKEditor's list feature stamps every <li> with one of these (dropped again on export)."""
        return "".join(self.rng.choice("0123456789abcdef") for _ in range(32))

    # --- entities ----------------------------------------------------------------------------

    def note(self, key, parent, title, type_="text", content="", mime=None, **extra):
        body = {"noteId": self.nid(key), "parentNoteId": self.nid(parent), "title": title,
                "type": type_, "content": content}
        if mime is not None:
            body["mime"] = mime
        body.update(extra)
        return self.request("POST", "/etapi/create-note", body)

    def set_content(self, key, content):
        """PUT content, as a client save does; this also runs the server's link post-processing."""
        ctype = None if isinstance(content, str) else "application/octet-stream"
        self.request("PUT", f"/etapi/notes/{self.nid(key)}/content", content, ctype=ctype)

    def attr(self, key, type_, name, value="", inheritable=False):
        return self.request("POST", "/etapi/attributes", {
            "attributeId": self.new_id(), "noteId": self.nid(key), "type": type_, "name": name,
            "value": value, "isInheritable": inheritable})

    def label(self, key, name, value="", inheritable=False):
        return self.attr(key, "label", name, value, inheritable)

    def relation(self, key, name, target, inheritable=False):
        return self.attr(key, "relation", name, self.nid(target), inheritable)

    def attachment(self, owner, role, mime, title, content, position=10):
        att = self.request("POST", "/etapi/attachments", {
            "ownerId": self.nid(owner), "role": role, "mime": mime, "title": title,
            "position": position})
        ctype = None if isinstance(content, str) else "application/octet-stream"
        self.request("PUT", f"/etapi/attachments/{att['attachmentId']}/content", content, ctype=ctype)
        return att["attachmentId"]

    def clone(self, key, parent, prefix=None):
        body = {"noteId": self.nid(key), "parentNoteId": self.nid(parent)}
        if prefix is not None:
            body["prefix"] = prefix
        return self.request("POST", "/etapi/branches", body)

    def day(self, date):
        return self.request("GET", f"/etapi/calendar/days/{date}")["noteId"]

    def remove_demo(self):
        """Deletes whatever the new document came with (versions before 0.100 always add the demo)."""
        for child in self.request("GET", "/etapi/notes/root")["childNoteIds"]:
            if not child.startswith("_"):
                self.request("DELETE", f"/etapi/notes/{child}")

    def export_web(self, branch_id, kind, fmt, task):
        """Export through the web API, as the UI's export dialog does: kind "single" (HTML or
        Markdown file of one note) or "subtree" (also the only way to get OPML). Versions before
        0.100 have an extra version segment in the route."""
        return (self.request("GET", f"/api/branches/{branch_id}/export/{kind}/{fmt}/{task}",
                             raw=True, missing_ok=True)
                or self.request("GET", f"/api/branches/{branch_id}/export/{kind}/{fmt}/1.0/{task}",
                                raw=True))

    def export(self, key, fmt):
        return self.request("GET", f"/etapi/notes/{self.nid(key)}/export?format={fmt}", raw=True)


def save(out, name, data):
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)
    print(f"{name}: {len(data)} bytes")


# --- tiny binary payloads ------------------------------------------------------------------------

def png(w, h, rgba):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = b"".join(b"\x00" + bytes(rgba) * w for _ in range(h))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def pdf(text):
    stream = f"BT /F1 12 Tf 20 50 Td ({text}) Tj ET".encode()
    objs = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents 4 0 R "
        b"/Resources << /Font << /F1 5 0 R >> >> >>",
        b"<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]
    out = b"%PDF-1.4\n"
    offsets = []
    for i, o in enumerate(objs, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % i + o + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    out += b"".join(b"%010d 00000 n \n" % off for off in offsets)
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, xref)
    return out


def todo(t, items, nested=None):
    """A CKEditor 5 to-do list: items are (text_html, checked) or (text_html, checked, state, title)."""
    out = '<ul class="todo-list">'
    for i, item in enumerate(items):
        text, checked = item[0], item[1]
        attrs = f' data-list-item-id="{t.li_id()}"' if t is not None else ""
        if len(item) > 2:
            attrs += f' data-trilium-task-state="{item[2]}" title="{item[3]}"'
        box = '<input type="checkbox" checked="checked" disabled="disabled">' if checked \
            else '<input type="checkbox" disabled="disabled">'
        out += (f'<li{attrs}><label class="todo-list__label">{box}'
                f'<span class="todo-list__label__description">{text}</span></label>')
        if nested and i == len(items) - 1:
            out += nested
        out += "</li>"
    return out + "</ul>"
