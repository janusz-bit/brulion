#include "publish/protect.h"

#include "crypto/aead.h"
#include "crypto/base64.h"
#include "crypto/hash.h"
#include "crypto/secure.h"
#include "publish/messages.h"
#include "util/paths.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace {

using crypto::Hash;
using crypto::SecureBytes;

constexpr qsizetype NameBytes = 10;
constexpr qsizetype SealSaltSize = 16;
constexpr qsizetype IvSize = crypto::aesgcm::StandardIvSize;

/// The lock page's script: reads the sealed page, derives K from the password (or takes it from
/// sessionStorage), opens the page and puts it in place of the lock page (DOMParser: nothing of it
/// runs), then fetches and opens the files its pictures and links name. Only DOM and WebCrypto: no
/// eval, no inline handlers, no external resource.
const char *lockScript()
{
    return R"js((function(){
"use strict";
var d=document,sealed=JSON.parse(d.getElementById("brulion-sealed").textContent);
var form=d.getElementById("brulion-unlock"),input=d.getElementById("brulion-password");
var remember=d.getElementById("brulion-remember"),note=d.getElementById("brulion-note");
var button=form.querySelector("button"),subtle=window.crypto&&window.crypto.subtle;
var slot="brulion-key:"+sealed.salt;
function bytes(t){var s=atob(t),o=new Uint8Array(s.length);for(var i=0;i<s.length;i++)o[i]=s.charCodeAt(i);return o;}
function base64(b){var a=new Uint8Array(b),s="";for(var i=0;i<a.length;i++)s+=String.fromCharCode(a[i]);return btoa(s);}
function say(what){note.textContent=form.getAttribute("data-"+what);note.hidden=false;}
function stored(){try{return sessionStorage.getItem(slot);}catch(e){return null;}}
function keep(k){try{sessionStorage.setItem(slot,base64(k));}catch(e){}}
function forget(){try{sessionStorage.removeItem(slot);}catch(e){}}
async function open(k,salt,iv,data){
var mac=await subtle.importKey("raw",k,{name:"HMAC",hash:"SHA-256"},false,["sign"]);
var key=await subtle.importKey("raw",await subtle.sign("HMAC",mac,salt),"AES-GCM",false,["decrypt"]);
return new Uint8Array(await subtle.decrypt({name:"AES-GCM",iv:iv},key,data));}
async function derive(password){
var base=await subtle.importKey("raw",new TextEncoder().encode(password),"PBKDF2",false,["deriveBits"]);
return new Uint8Array(await subtle.deriveBits({name:"PBKDF2",hash:"SHA-256",salt:bytes(sealed.salt),iterations:sealed.iterations},base,256));}
async function fill(el,attr,k){
try{var r=await fetch(el.getAttribute("data-sealed-"+attr));if(!r.ok)return;
var all=new Uint8Array(await r.arrayBuffer());
var p=await open(k,all.subarray(4,20),all.subarray(20,32),all.subarray(32)),n=p.indexOf(10);
var head=JSON.parse(new TextDecoder().decode(p.subarray(0,n)));
el.setAttribute(attr,URL.createObjectURL(new Blob([p.subarray(n+1)],{type:head.type})));
if(attr==="href"&&!el.hasAttribute("download"))el.setAttribute("download",head.name);
}catch(e){}}
async function unlock(k){
var html=new TextDecoder().decode(await open(k,bytes(sealed.page),bytes(sealed.iv),bytes(sealed.data)));
var page=new DOMParser().parseFromString(html,"text/html");
d.replaceChild(d.adoptNode(page.documentElement),d.documentElement);
var t=location.hash&&d.getElementById(decodeURIComponent(location.hash.slice(1)));
if(t)t.scrollIntoView();
var els=d.querySelectorAll("[data-sealed-src],[data-sealed-href]");
for(var i=0;i<els.length;i++)fill(els[i],els[i].hasAttribute("data-sealed-src")?"src":"href",k);}
if(!subtle){say("insecure");input.disabled=true;button.disabled=true;return;}
form.addEventListener("submit",async function(ev){
ev.preventDefault();if(!input.value)return;
button.disabled=true;note.hidden=true;say("opening");
try{var k=await derive(input.value);}catch(e){say("insecure");button.disabled=false;return;}
try{await unlock(k);}catch(e){say("wrong");button.disabled=false;input.select();return;}
if(remember.checked)keep(k);});
var saved=stored();
if(saved){form.hidden=true;unlock(bytes(saved)).catch(function(){forget();form.hidden=false;input.focus();});}
})();
)js";
}

const char *lockStyle()
{
    return R"css(:root{color-scheme:light dark;--bg:#fbfaf7;--fg:#1f1d1a;--muted:#6b665e;--line:#d9d4ca;--accent:#3a6ea5}
@media (prefers-color-scheme:dark){:root{--bg:#1b1a18;--fg:#ece8e1;--muted:#a39d93;--line:#3b3833;--accent:#8db4e2}}
body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:var(--bg);color:var(--fg);
font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
form{width:min(22rem,calc(100vw - 2rem));display:flex;flex-direction:column;gap:.75rem}
h1{font-size:1.4rem;margin:0}p{margin:0;color:var(--muted)}
input[type=password]{font:inherit;padding:.5rem .7rem;border:1px solid var(--line);border-radius:6px;background:transparent;color:inherit}
label.remember{display:flex;gap:.5rem;align-items:center;color:var(--muted);font-size:.9rem}
button{font:inherit;padding:.5rem;border:0;border-radius:6px;background:var(--accent);color:#fff;cursor:pointer}
button:disabled{opacity:.6}#brulion-note{color:var(--fg)}
)css";
}

QString escapeHtml(QStringView s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        switch (c.unicode()) {
        case u'&': out += u"&amp;"; break;
        case u'<': out += u"&lt;"; break;
        case u'>': out += u"&gt;"; break;
        case u'"': out += u"&quot;"; break;
        case u'\'': out += u"&#39;"; break;
        default: out += c;
        }
    }
    return out;
}

/// An attribute value as the exporter escaped it, read back.
QString unescapeHtml(QString s)
{
    if (!s.contains(u'&'))
        return s;
    s.replace(u"&lt;"_s, u"<"_s).replace(u"&gt;"_s, u">"_s).replace(u"&quot;"_s, u"\""_s).replace(u"&#39;"_s, u"'"_s);
    return s.replace(u"&amp;"_s, u"&"_s);
}

QByteArray hmacOf(QByteArrayView key, QByteArrayView data)
{
    return crypto::hmac(Hash::Sha256, key, data);
}

QByteArray labelled(const char *label, const QString &path)
{
    QByteArray out(label);
    out.append('\0');
    out.append(path.toUtf8());
    return out;
}

/// The relative URL from folder `fromDir` to site path `to` (both of the protected site's paths:
/// plain ASCII, no encoding needed).
QString relativeUrl(const QString &fromDir, const QString &to)
{
    if (fromDir.isEmpty())
        return to;
    const QStringList from = fromDir.split(u'/');
    const QStringList target = to.split(u'/');
    qsizetype common = 0;
    while (common < from.size() && common + 1 < target.size() && from.at(common) == target.at(common))
        ++common;
    QString out;
    for (qsizetype i = common; i < from.size(); ++i)
        out += u"../"_s;
    return out + target.mid(common).join(u'/');
}

/// Does `url` name a place of its own (a scheme, an absolute path, only a fragment or a query)?
bool isOwnPlace(QStringView url)
{
    if (url.isEmpty() || url.startsWith(u'#') || url.startsWith(u'/') || url.startsWith(u'?'))
        return true;
    const qsizetype colon = url.indexOf(u':');
    if (colon < 0)
        return false;
    const qsizetype slash = url.indexOf(u'/');
    const qsizetype question = url.indexOf(u'?');
    const qsizetype hash = url.indexOf(u'#');
    return (slash < 0 || colon < slash) && (question < 0 || colon < question) && (hash < 0 || colon < hash);
}

struct Sealed {
    QByteArray salt;
    QByteArray iv;
    QByteArray data; ///< ciphertext + tag
};

Sealed seal(const SecureBytes &k, const QString &newPath, QByteArrayView plaintext)
{
    Sealed out;
    out.salt = hmacOf(k.view(), labelled("salt", newPath)).left(SealSaltSize);
    QByteArray key = hmacOf(k.view(), out.salt);
    out.iv = hmacOf(key, plaintext).left(IvSize);
    out.data = crypto::aesgcm::seal(key, out.iv, {}, plaintext);
    crypto::wipe(key);
    return out;
}

/// One HTML file's links rewritten to the protected site's names.
QString rewriteLinks(const QString &html, const QString &oldPath, const QString &newPath,
                     const QHash<QString, QString> &moved, const QHash<QString, QString> &sealedFiles)
{
    static const QRegularExpression attribute(uR"re((\s)(href|src)="([^"]*)")re"_s);
    const QString oldDir = paths::dirname(oldPath);
    const QString newDir = paths::dirname(newPath);
    QString out;
    out.reserve(html.size());
    qsizetype last = 0;
    for (QRegularExpressionMatchIterator it = attribute.globalMatch(html); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString value = unescapeHtml(m.captured(3));
        if (isOwnPlace(value))
            continue;
        qsizetype cut = value.size();
        for (const QChar stop : {u'#', u'?'})
            if (const qsizetype at = value.indexOf(stop); at >= 0)
                cut = std::min(cut, at);
        const QString rest = value.mid(cut);
        const QString path = paths::normalize(
                oldDir.isEmpty() ? QUrl::fromPercentEncoding(value.left(cut).toUtf8())
                                 : oldDir + u'/' + QUrl::fromPercentEncoding(value.left(cut).toUtf8()));
        QString replacement;
        if (const auto file = sealedFiles.constFind(path); file != sealedFiles.constEnd()) {
            replacement =
                    m.captured(1) + u"data-sealed-"_s + m.captured(2) + u"=\""_s + relativeUrl(newDir, *file) + u'"';
        } else if (const auto page = moved.constFind(path); page != moved.constEnd()) {
            replacement = m.captured(1) + m.captured(2) + u"=\""_s + relativeUrl(newDir, *page)
                          + escapeHtml(rest.startsWith(u'#') ? rest : QString()) + u'"';
        } else {
            continue;
        }
        out += QStringView(html).sliced(last, m.capturedStart() - last);
        out += replacement;
        last = m.capturedEnd();
    }
    out += QStringView(html).sliced(last);
    return out;
}

QString lockPage(const Sealed &sealed, const QByteArray &siteSalt, quint32 iterations, const LockTexts &t)
{
    const QString json =
            uR"({"v":1,"kdf":"PBKDF2","hash":"SHA-256","iterations":%1,"salt":"%2","page":"%3","iv":"%4","data":"%5"})"_s
                    .arg(iterations)
                    .arg(crypto::encodeBase64(siteSalt), crypto::encodeBase64(sealed.salt),
                         crypto::encodeBase64(sealed.iv), crypto::encodeBase64(sealed.data));
    const auto attr = [](const QString &name, const QString &value) {
        return u' ' + name + u"=\""_s + escapeHtml(value) + u'"';
    };
    return u"<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
           u"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           u"<meta name=\"robots\" content=\"noindex, nofollow, noarchive\">\n"
           u"<meta name=\"referrer\" content=\"no-referrer\">\n"
           u"<meta name=\"generator\" content=\"Brulion\">\n<title>"_s
           + escapeHtml(t.title) + u"</title>\n<style>\n"_s + QString::fromUtf8(lockStyle())
           + u"</style>\n</head>\n<body>\n"_s + u"<form id=\"brulion-unlock\""_s + attr(u"data-wrong"_s, t.wrong)
           + attr(u"data-insecure"_s, t.insecure) + attr(u"data-opening"_s, t.opening) + u">\n<h1>"_s
           + escapeHtml(t.title) + u"</h1>\n<p>"_s + escapeHtml(t.prompt)
           + u"</p>\n<input type=\"password\" id=\"brulion-password\" autocomplete=\"current-password\""_s
           + attr(u"aria-label"_s, t.password) + attr(u"placeholder"_s, t.password)
           + u" autofocus required>\n<label class=\"remember\"><input type=\"checkbox\" id=\"brulion-remember\" checked> "_s
           + escapeHtml(t.remember) + u"</label>\n<button type=\"submit\">"_s + escapeHtml(t.open)
           + u"</button>\n<p id=\"brulion-note\" role=\"status\" aria-live=\"polite\" hidden></p>\n<noscript><p>"_s
           + escapeHtml(t.noScript)
           + u"</p></noscript>\n</form>\n<script type=\"application/json\" id=\"brulion-sealed\">"_s + json
           + u"</script>\n<script>\n"_s + QString::fromUtf8(lockScript()) + u"</script>\n</body>\n</html>\n"_s;
}

bool isHtml(const QString &path)
{
    return path.endsWith(u".html"_s, Qt::CaseInsensitive) || path.endsWith(u".htm"_s, Qt::CaseInsensitive);
}

/// The site's own files that hold no note content: kept as they are.
bool isPlainSiteFile(const QString &path)
{
    return path == u"style.css" || path == u".nojekyll";
}

} // namespace

LockTexts lockTexts()
{
    LockTexts t;
    t.title = PublishMessages::tr("Protected page");
    t.prompt = PublishMessages::tr("Type the password to read this page.");
    t.password = PublishMessages::tr("Password");
    t.remember = PublishMessages::tr("Remember it until the browser is closed");
    t.open = PublishMessages::tr("Open");
    t.opening = PublishMessages::tr("Opening…");
    t.wrong = PublishMessages::tr("Wrong password.");
    t.insecure = PublishMessages::tr(
            "This browser cannot open the page here: it needs a secure (https) address and a current browser.");
    t.noScript = PublishMessages::tr("The page opens with JavaScript: turn it on to type the password.");
    return t;
}

ProtectedSite protectSite(const QList<io::FileEntry> &files, const SiteProtection &protection, const LockTexts &texts)
{
    ProtectedSite out;
    if (protection.password.isEmpty()) {
        out.errorCode = u"no-page-password"_s;
        return out;
    }
    const QByteArray salt = protection.salt.size() >= ProtectSaltSize
                                    ? protection.salt
                                    : crypto::randomBytes(ProtectSaltSize, protection.random);
    const quint32 iterations = std::max<quint32>(1, protection.iterations);
    const SecureBytes password = SecureBytes::fromUtf8(protection.password);
    const SecureBytes k = crypto::pbkdf2(Hash::Sha256, password.view(), salt, iterations, 32);

    // The new names.
    QHash<QString, QString> moved;       // old path -> new path (pages and the site's own files)
    QHash<QString, QString> sealedFiles; // old path -> new path (files)
    QHash<QString, QString> taken;
    const auto name = [&](const QString &path) {
        QString hex = QString::fromLatin1(hmacOf(k.view(), labelled("name", path)).left(NameBytes).toHex());
        return hex;
    };
    for (const io::FileEntry &f : files) {
        QString to;
        if (isPlainSiteFile(f.path) || f.path == u"index.html")
            to = f.path;
        else if (isHtml(f.path))
            to = u"p/"_s + name(f.path) + u".html"_s;
        else
            to = u"f/"_s + name(f.path);
        if (taken.contains(to)) // never with 80 bits of HMAC; a clash keeps the files apart anyway
            to = to + u'-' + QString::number(taken.size());
        taken.insert(to, f.path);
        (isHtml(f.path) || isPlainSiteFile(f.path) ? moved : sealedFiles).insert(f.path, to);
    }

    const QMimeDatabase mimes;
    for (const io::FileEntry &f : files) {
        if (isPlainSiteFile(f.path)) {
            out.files.append(f);
            continue;
        }
        if (isHtml(f.path)) {
            const QString to = moved.value(f.path);
            const QByteArray html = rewriteLinks(QString::fromUtf8(f.data), f.path, to, moved, sealedFiles).toUtf8();
            out.files.append(io::FileEntry{to, lockPage(seal(k, to, html), salt, iterations, texts).toUtf8()});
            continue;
        }
        const QString to = sealedFiles.value(f.path);
        QString type = mimes.mimeTypeForFile(f.path, QMimeDatabase::MatchExtension).name();
        if (type.isEmpty() || type == u"application/octet-stream")
            type = u"application/octet-stream"_s;
        QByteArray plain = QJsonDocument(QJsonObject{{u"name"_s, paths::basename(f.path)}, {u"type"_s, type}})
                                   .toJson(QJsonDocument::Compact);
        plain.append('\n');
        plain.append(f.data);
        const Sealed s = seal(k, to, plain);
        crypto::wipe(plain);
        QByteArray data(ProtectFileMagic);
        data += s.salt + s.iv + s.data;
        out.files.append(io::FileEntry{to, data});
    }
    return out;
}

} // namespace brulion::publish
