# The remote MCP server's whole sign-in, as claude.ai or ChatGPT does it, run inside the VM of
# checks.x86_64-linux.web-vm (nix/tests/web-vm.nix) against services.brulion-mcp: metadata, the
# 401 challenge, dynamic client registration, the sign-in and consent pages, the token with PKCE,
# a tool call that writes (per-request protocol 2026-07-28), and the token refused once the
# connection is revoked (the test script revokes it between `call` and `refused`).
#
#   python3 mcp-flow.py connect <base> <password>    # prints the access token
#   python3 mcp-flow.py call <base> <token>
#   python3 mcp-flow.py refused <base> <token>
import base64
import hashlib
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

step, base = sys.argv[1], sys.argv[2]
REDIRECT = "https://claude.ai/api/mcp/auth_callback"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


opener = urllib.request.build_opener(NoRedirect)


def request(method, path, body=None, headers=None):
    req = urllib.request.Request(base + path, data=body, method=method, headers=headers or {})
    try:
        with opener.open(req, timeout=60) as r:
            return r.status, dict(r.headers), r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read().decode()


def form(path, fields):
    return request("POST", path, urllib.parse.urlencode(fields).encode(),
                   {"Content-Type": "application/x-www-form-urlencoded"})


def hidden(html, name):
    return re.search(rf'name="{name}" value="([^"]*)"', html).group(1)


def mcp(token, method, params):
    params = dict(params, _meta={"io.modelcontextprotocol/protocolVersion": "2026-07-28",
                                 "io.modelcontextprotocol/clientCapabilities": {}})
    headers = {"Authorization": "Bearer " + token, "Content-Type": "application/json",
               "Accept": "application/json, text/event-stream", "MCP-Protocol-Version": "2026-07-28",
               "Mcp-Method": method}
    if "name" in params:
        headers["Mcp-Name"] = params["name"]
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode()
    return request("POST", "/mcp", body, headers)


if step == "connect":
    password = sys.argv[3]
    status, headers, _ = request("POST", "/mcp", b"{}", {"Content-Type": "application/json"})
    assert status == 401 and "resource_metadata=" in headers["WWW-Authenticate"], (status, headers)
    status, _, body = request("GET", "/.well-known/oauth-protected-resource")
    assert json.loads(body)["resource"] == base + "/mcp", body
    status, _, body = request("GET", "/.well-known/oauth-authorization-server")
    assert json.loads(body)["code_challenge_methods_supported"] == ["S256"], body
    status, _, body = request("POST", "/register", json.dumps(
        {"redirect_uris": [REDIRECT], "client_name": "Claude"}).encode(), {"Content-Type": "application/json"})
    assert status == 201, (status, body)
    client = json.loads(body)["client_id"]
    verifier = base64.urlsafe_b64encode(os.urandom(40)).rstrip(b"=").decode()
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    query = urllib.parse.urlencode({"response_type": "code", "client_id": client, "redirect_uri": REDIRECT,
                                    "code_challenge": challenge, "code_challenge_method": "S256",
                                    "state": "vm", "scope": "notes:read notes:write",
                                    "resource": base + "/mcp"})
    status, _, page = request("GET", "/authorize?" + query)
    assert status == 200, (status, page)
    status, _, wrong = form("/authorize", {"request": hidden(page, "request"), "csrf": hidden(page, "csrf"),
                                           "action": "signin", "password": "not the password"})
    assert "Wrong password." in wrong, wrong
    status, _, consent = form("/authorize", {"request": hidden(page, "request"), "csrf": hidden(page, "csrf"),
                                             "action": "signin", "password": password})
    assert 'name="write"' in consent, consent
    status, headers, _ = form("/authorize", {"request": hidden(page, "request"), "csrf": hidden(consent, "csrf"),
                                             "action": "allow", "write": "on"})
    assert status == 303, status
    location = urllib.parse.urlparse(headers["Location"])
    answer = dict(urllib.parse.parse_qsl(location.query))
    assert answer["iss"] == base and answer["state"] == "vm", answer
    status, _, body = form("/token", {"grant_type": "authorization_code", "code": answer["code"],
                                      "client_id": client, "code_verifier": verifier, "redirect_uri": REDIRECT,
                                      "resource": base + "/mcp"})
    assert status == 200, (status, body)
    tokens = json.loads(body)
    assert tokens["scope"] == "notes:read notes:write", tokens
    print(tokens["access_token"])
elif step == "call":
    status, _, body = mcp(sys.argv[3], "tools/call", {"name": "brulion_search", "arguments": {"text": "zolw"}})
    hits = json.loads(body)["result"]["structuredContent"]["result"]["hits"]
    assert status == 200 and len(hits) == 1, (status, body)
    status, _, body = mcp(sys.argv[3], "tools/call", {"name": "brulion_capture",
                                                      "arguments": {"text": "Zapisane przez łącznik"}})
    assert status == 200 and json.loads(body)["result"]["isError"] is False, (status, body)
    print("call: ok")
elif step == "refused":
    status, _, body = mcp(sys.argv[3], "tools/list", {})
    assert status == 401, (status, body)
    print("refused: ok")
