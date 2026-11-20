#!/usr/bin/env python3
"""Build the Apple Notes fixture (tst_io_import_applenotes): a group.com.apple.notes folder as a
Mac has it, with NoteStore.sqlite in WAL mode (its last transaction only in NoteStore.sqlite-wal),
Core Data's tables (Z_PRIMARYKEY, ZICCLOUDSYNCINGOBJECT, ZICNOTEDATA), notes as gzipped
NoteStoreProto messages (the schema of the Apple Cloud Notes Parser / Obsidian Importer), a table
attachment (its CRDT), media, a drawing, a scan, folders, a deleted and a locked note.

    python3 -I tests/core/io/fixtures/applenotes/make-fixture.py tests/core/io/fixtures/applenotes

Deterministic: fixed UUIDs, dates and a seeded random text (gzip without a timestamp).
"""
import gzip
import os
import random
import shutil
import sqlite3
import struct
import sys
import tempfile

ACCOUNT = "4B2C6E1A-9F3D-4C5B-8A7E-1D2F3A4B5C6D"
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
GROUP = os.path.join(OUT, "group.com.apple.notes")
# Core Data seconds since 2001-01-01: 2026-11-10 10:30:00 UTC and 2026-11-13 17:05:00 UTC.
CREATED = 815999400.0
MODIFIED = 816282300.0


# ---- protobuf wire format ----------------------------------------------------------------------

def varint(n):
    if n < 0:
        n += 1 << 64
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def field_varint(num, value):
    return varint(num << 3) + varint(value)


def field_bytes(num, data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    return varint((num << 3) | 2) + varint(len(data)) + data


def u16len(s):
    return len(s.encode("utf-16-le")) // 2


def run(length, style=None, indent=0, checklist=None, quote=False, weight=0, strike=False, link=None,
        emphasis=0, attachment=None):
    ps = b""
    if style is not None:
        ps += field_varint(1, style)
    if indent:
        ps += field_varint(4, indent)
    if checklist is not None:
        ps += field_bytes(5, field_bytes(1, b"\x01" * 16) + field_varint(2, 1 if checklist else 0))
    if quote:
        ps += field_varint(8, 1)
    out = field_varint(1, length)
    if ps or style is not None:
        out += field_bytes(2, ps)
    if weight:
        out += field_varint(5, weight)
    if strike:
        out += field_varint(7, 1)
    if link:
        out += field_bytes(9, link)
    if attachment:
        out += field_bytes(12, field_bytes(1, attachment[0]) + field_bytes(2, attachment[1]))
    if emphasis:
        out += field_varint(14, emphasis)
    return out


def note_message(pieces):
    """pieces: [(text, dict of run attributes)] -> a Note message."""
    text = "".join(p[0] for p in pieces)
    runs = b"".join(field_bytes(5, run(u16len(t), **attrs)) for t, attrs in pieces)
    return field_bytes(2, text) + runs


def note_data(pieces):
    document = field_varint(2, 0) + field_bytes(3, note_message(pieces))
    return gzip.compress(field_bytes(2, document), mtime=0)


# ---- table CRDT ---------------------------------------------------------------------------------

def object_id(index=None, uint=None, string=None):
    out = b""
    if uint is not None:
        out += field_varint(2, uint)
    if string is not None:
        out += field_bytes(4, string)
    if index is not None:
        out += field_varint(6, index)
    return out


def table_data(cells):
    """cells: rows of texts -> a gzipped MergableDataProto of an ICTable."""
    rows, cols = len(cells), len(cells[0])
    keys = ["crRows", "crColumns", "cellColumns", "UUIDIndex", "self"]
    types = ["com.apple.notes.ICTable", "com.apple.CRDT.NSUUID", "com.apple.CRDT.CRTree"]
    uuids = [bytes([0x10 + i] * 16) for i in range(rows)] + [bytes([0x80 + i] * 16) for i in range(cols)]
    objects = []

    def add(entry):
        objects.append(entry)
        return len(objects) - 1

    # Placeholders, filled once the indexes are known.
    root = add(None)
    row_set = add(None)
    col_set = add(None)
    cell_columns = add(None)
    uuid_refs = []
    for i in range(len(uuids)):
        m = field_varint(1, 1) + field_bytes(3, field_varint(1, 3) + field_bytes(2, object_id(uint=i)))
        uuid_refs.append(add(field_bytes(13, m)))
    column_rows = []
    for c in range(cols):
        elements = b""
        for r in range(rows):
            text = cells[r][c]
            note = note_message([(text, {})]) if text else note_message([])
            content = add(field_bytes(10, note))
            elements += field_bytes(1, field_bytes(1, object_id(index=uuid_refs[r])) + field_bytes(2, object_id(index=content)))
        column_rows.append(add(field_bytes(6, elements)))

    def ordered(first, count):
        array = b"".join(field_bytes(2, field_varint(1, i) + field_bytes(2, uuids[first + i])) for i in range(count))
        contents = b"".join(
            field_bytes(1, field_bytes(1, object_id(index=uuid_refs[first + i])) + field_bytes(2, object_id(index=uuid_refs[first + i])))
            for i in range(count))
        ordering = field_bytes(1, array) + field_bytes(2, contents)
        return field_bytes(16, field_bytes(1, ordering))

    objects[row_set] = ordered(0, rows)
    objects[col_set] = ordered(rows, cols)
    elements = b"".join(field_bytes(1, field_bytes(1, object_id(index=uuid_refs[rows + c])) + field_bytes(2, object_id(index=column_rows[c])))
                        for c in range(cols))
    objects[cell_columns] = field_bytes(6, elements)
    root_map = field_varint(1, 0)
    for key, target in ((0, row_set), (1, col_set), (2, cell_columns)):
        root_map += field_bytes(3, field_varint(1, key) + field_bytes(2, object_id(index=target)))
    objects[root] = field_bytes(13, root_map)
    data = b"".join(field_bytes(3, o) for o in objects)
    data += b"".join(field_bytes(4, k) for k in keys)
    data += b"".join(field_bytes(5, t) for t in types)
    data += b"".join(field_bytes(6, u) for u in uuids)
    proto = field_bytes(2, field_varint(2, 0) + field_bytes(3, data))
    return gzip.compress(proto, mtime=0)


def gallery_data(image_ids):
    entries = b""
    for image in image_ids:
        m = field_varint(1, 0) + field_bytes(3, field_varint(1, 0) + field_bytes(2, object_id(string=image)))
        entries += field_bytes(3, field_bytes(13, m))
    proto = field_bytes(2, field_varint(2, 0) + field_bytes(3, entries + field_bytes(4, "self") + field_bytes(5, "com.apple.notes.gallery")))
    return gzip.compress(proto, mtime=0)


# ---- the database --------------------------------------------------------------------------------

PNG = bytes.fromhex("89504e470d0a1a0a0000000d4948445200000001000000010806000000"
                    "1f15c4890000000d49444154789c6360f8cf00000301010018dd8db00000000049454e44ae426082")

ENT = {"ICAccount": 14, "ICFolder": 15, "ICNote": 12, "ICAttachment": 5, "ICMedia": 11, "ICHashtag": 9}
COLUMNS = ["Z_ENT", "Z_OPT", "ZIDENTIFIER", "ZTITLE1", "ZTITLE2", "ZNAME", "ZFOLDER", "ZPARENT", "ZFOLDERTYPE",
           "ZOWNER", "ZNOTE", "ZMEDIA", "ZTYPEUTI", "ZFILENAME", "ZGENERATION1", "ZFALLBACKIMAGEGENERATION",
           "ZFALLBACKPDFGENERATION", "ZSIZEWIDTH", "ZSIZEHEIGHT", "ZALTTEXT", "ZTOKENCONTENTIDENTIFIER", "ZTITLE",
           "ZURLSTRING", "ZMERGEABLEDATA1", "ZCREATIONDATE", "ZCREATIONDATE1", "ZCREATIONDATE3", "ZMODIFICATIONDATE",
           "ZMODIFICATIONDATE1", "ZISPASSWORDPROTECTED", "ZISPINNED", "ZMARKEDFORDELETION", "ZSNIPPET"]


def write_file(rel, data):
    path = os.path.join(GROUP, "Accounts", ACCOUNT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def main():
    if os.path.isdir(GROUP):
        shutil.rmtree(GROUP)
    os.makedirs(GROUP)
    work = tempfile.mkdtemp()
    db_path = os.path.join(work, "NoteStore.sqlite")
    con = sqlite3.connect(db_path, isolation_level=None)
    con.execute("PRAGMA page_size=4096")
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA wal_autocheckpoint=0")
    con.execute("CREATE TABLE Z_PRIMARYKEY (Z_ENT INTEGER PRIMARY KEY, Z_NAME VARCHAR, Z_SUPER INTEGER, Z_MAX INTEGER)")
    cols = ", ".join(c + (" BLOB" if c == "ZMERGEABLEDATA1" else " TIMESTAMP" if "DATE" in c else " VARCHAR" if c.startswith(("ZIDENT", "ZTITLE", "ZNAME", "ZTYPE", "ZFILE", "ZGEN", "ZFALL", "ZALT", "ZTOKEN", "ZURL", "ZSNIP")) else " INTEGER")
                     for c in COLUMNS)
    con.execute(f"CREATE TABLE ZICCLOUDSYNCINGOBJECT ( Z_PK INTEGER PRIMARY KEY, {cols} )")
    con.execute("CREATE TABLE ZICNOTEDATA ( Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, Z_OPT INTEGER, ZNOTE INTEGER, ZDATA BLOB )")
    con.execute("CREATE INDEX ZICCLOUDSYNCINGOBJECT_ZFOLDER_INDEX ON ZICCLOUDSYNCINGOBJECT (ZFOLDER)")
    con.execute("BEGIN")
    for name, ent in ENT.items():
        con.execute("INSERT INTO Z_PRIMARYKEY VALUES (?, ?, 0, 0)", (ent, name))

    pk = [0]

    def obj(**values):
        pk[0] += 1
        values.setdefault("Z_OPT", 1)
        names = ["Z_PK"] + list(values)
        con.execute(f"INSERT INTO ZICCLOUDSYNCINGOBJECT ({', '.join(names)}) VALUES ({', '.join('?' * len(names))})",
                    [pk[0]] + list(values.values()))
        return pk[0]

    def note(folder, title, pieces, identifier, **extra):
        p = obj(Z_ENT=ENT["ICNote"], ZIDENTIFIER=identifier, ZTITLE1=title, ZFOLDER=folder, ZCREATIONDATE3=CREATED,
                ZMODIFICATIONDATE1=MODIFIED, **extra)
        con.execute("INSERT INTO ZICNOTEDATA (Z_ENT, Z_OPT, ZNOTE, ZDATA) VALUES (4, 1, ?, ?)", (p, note_data(pieces)))
        return p

    account = obj(Z_ENT=ENT["ICAccount"], ZIDENTIFIER=ACCOUNT, ZNAME="iCloud")
    default = obj(Z_ENT=ENT["ICFolder"], ZIDENTIFIER="DefaultFolder-CloudKit", ZTITLE2="Notatki", ZOWNER=account, ZFOLDERTYPE=0)
    recipes = obj(Z_ENT=ENT["ICFolder"], ZIDENTIFIER="F0000001-0000-4000-8000-000000000001", ZTITLE2="Przepisy", ZOWNER=account, ZFOLDERTYPE=0)
    cakes = obj(Z_ENT=ENT["ICFolder"], ZIDENTIFIER="F0000002-0000-4000-8000-000000000002", ZTITLE2="Ciasta: słodkie", ZOWNER=account,
                ZPARENT=recipes, ZFOLDERTYPE=0)
    trash = obj(Z_ENT=ENT["ICFolder"], ZIDENTIFIER="TrashFolder-CloudKit", ZTITLE2="Ostatnio usunięte", ZOWNER=account, ZFOLDERTYPE=1)

    # Attachments of the shopping list: a hashtag and a photo.
    obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER="A0000001-0000-4000-8000-0000000000A1", ZTYPEUTI="com.apple.notes.inlinetextattachment.hashtag",
        ZALTTEXT="#zakupy")
    media = obj(Z_ENT=ENT["ICMedia"], ZIDENTIFIER="M0000001-0000-4000-8000-0000000000B1", ZFILENAME="paragon.png", ZGENERATION1="1_8C2E")
    obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER="A0000002-0000-4000-8000-0000000000A2", ZTYPEUTI="public.png", ZMEDIA=media)
    write_file("Media/M0000001-0000-4000-8000-0000000000B1/1_8C2E/paragon.png", PNG)

    shopping_id = "N0000001-0000-4000-8000-000000000001"
    note(default, "Lista zakupów", [
        ("Lista zakupów\n", {"style": 0}),
        ("Na sobotę ", {}), ("koniecznie", {"weight": 1}), (", a w niedzielę ", {}), ("może", {"weight": 2}), (".\n", {}),
        ("Mleko 2%\n", {"style": 103, "checklist": True}),
        ("Chleb żytni\n", {"style": 103, "checklist": False}),
        ("Owoce\n", {"style": 100}),
        ("Jabłka\n", {"style": 100, "indent": 1}),
        ("Gruszki\n", {"style": 100, "indent": 1}),
        ("Sklep: ", {}), ("Biedronka", {"link": "https://www.biedronka.pl/pl/sklepy"}), (" ", {}),
        ("￼", {"attachment": ("A0000001-0000-4000-8000-0000000000A1", "com.apple.notes.inlinetextattachment.hashtag")}),
        ("\n", {}),
        ("￼", {"attachment": ("A0000002-0000-4000-8000-0000000000A2", "public.png")}),
        ("\n#1 cena $5 i $10", {}),
    ], shopping_id, ZISPINNED=1)

    # The apple pie: a heading, a numbered list, code, a table, a link to the list, a URL card.
    obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER="A0000003-0000-4000-8000-0000000000A3", ZTYPEUTI="com.apple.notes.table",
        ZMERGEABLEDATA1=table_data([["Składnik", "Ilość"], ["Mąka", "300 g"], ["Jabłka | kwaśne", "1 kg"]]))
    obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER="A0000004-0000-4000-8000-0000000000A4", ZTYPEUTI="public.url",
        ZTITLE="Szarlotka babci", ZURLSTRING="https://przepisy.example.pl/szarlotka")
    obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER="A0000005-0000-4000-8000-0000000000A5", ZTYPEUTI="com.apple.notes.inlinetextattachment.link",
        ZTOKENCONTENTIDENTIFIER="applenotes:note/" + shopping_id.lower() + "?ownerIdentifier=_x")
    note(cakes, "Szarlotka", [
        ("Szarlotka\n", {"style": 0}),
        ("Składniki\n", {"style": 1, "weight": 1}),
        ("￼", {"attachment": ("A0000003-0000-4000-8000-0000000000A3", "com.apple.notes.table")}),
        ("\n", {}),
        ("Kroki\n", {"style": 2}),
        ("Obrać jabłka\n", {"style": 102}),
        ("Upiec w 180°C\n", {"style": 102}),
        ("temp = 180\nczas = 45\n", {"style": 4}),
        ("Zakupy: ", {}),
        ("￼", {"attachment": ("A0000005-0000-4000-8000-0000000000A5", "com.apple.notes.inlinetextattachment.link")}),
        ("\n", {}),
        ("Nie dodawać cukru waniliowego", {"strike": True}), ("\n", {}),
        ("Najważniejsze: kwaśne jabłka", {"emphasis": 3}), ("\n", {}),
        ("Cytat z babci\n", {"quote": True}),
        ("￼", {"attachment": ("A0000004-0000-4000-8000-0000000000A4", "public.url")}),
    ], "N0000002-0000-4000-8000-000000000002")

    # A drawing, a scanned document and a picture whose file was not copied.
    obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER="A0000006-0000-4000-8000-0000000000A6", ZTYPEUTI="com.apple.paper",
        ZFALLBACKIMAGEGENERATION="2_D1")
    write_file("FallbackImages/A0000006-0000-4000-8000-0000000000A6/2_D1/FallbackImage.png", PNG)
    obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER="A0000007-0000-4000-8000-0000000000A7", ZTYPEUTI="com.apple.paper.doc.scan",
        ZFALLBACKPDFGENERATION="3_E1")
    write_file("FallbackPDFs/A0000007-0000-4000-8000-0000000000A7/3_E1/FallbackPDF.pdf", b"%PDF-1.4\n% fixture\n")
    missing = obj(Z_ENT=ENT["ICMedia"], ZIDENTIFIER="M0000002-0000-4000-8000-0000000000B2", ZFILENAME="brak.jpg")
    obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER="A0000008-0000-4000-8000-0000000000A8", ZTYPEUTI="public.jpeg", ZMEDIA=missing)
    note(recipes, "Pomysły", [
        ("Pomysły\n", {}),
        ("Szkic tortu:\n", {}),
        ("￼", {"attachment": ("A0000006-0000-4000-8000-0000000000A6", "com.apple.paper")}),
        ("\n", {}),
        ("￼", {"attachment": ("A0000007-0000-4000-8000-0000000000A7", "com.apple.paper.doc.scan")}),
        ("\n", {}),
        ("￼", {"attachment": ("A0000008-0000-4000-8000-0000000000A8", "public.jpeg")}),
    ], "N0000003-0000-4000-8000-000000000003")

    # Left out: a deleted and a locked note.
    note(trash, "Stara lista", [("Stara lista\nnieaktualne", {})], "N0000004-0000-4000-8000-000000000004")
    note(default, "Hasła", [("Hasła\n(zaszyfrowane)", {})], "N0000005-0000-4000-8000-000000000005", ZISPASSWORDPROTECTED=1)

    # A long note: its data spans overflow pages; many rows: interior B-tree pages.
    rng = random.Random(20261120)
    words = ["źdźbło", "żółw", "łąka", "gęś", "jabłoń", "ćma", "śnieg", "pszczoła", "dąb", "róża", "chrząszcz", "wiśnia"]
    long_text = " ".join(rng.choice(words) + str(rng.randint(0, 99999)) for _ in range(2500))
    note(default, "Długa notatka", [("Długa notatka\n", {}), (long_text, {})], "N0000006-0000-4000-8000-000000000006")
    for i in range(400):
        obj(Z_ENT=ENT["ICAttachment"], ZIDENTIFIER=f"X{i:07d}-0000-4000-8000-000000000000", ZTYPEUTI="public.data",
            ZSNIPPET="wypełnienie " * 4)
    con.execute("COMMIT")
    con.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    # The last transaction stays in the log: a note added on the Mac just before copying.
    con.execute("BEGIN")
    note(default, "Z dziennika", [("Z dziennika\nTa notatka jest tylko w NoteStore.sqlite-wal.", {})],
         "N0000007-0000-4000-8000-000000000007")
    con.execute("COMMIT")
    shutil.copy(db_path, os.path.join(GROUP, "NoteStore.sqlite"))
    shutil.copy(db_path + "-wal", os.path.join(GROUP, "NoteStore.sqlite-wal"))
    con.close()
    shutil.rmtree(work)


if __name__ == "__main__":
    main()
