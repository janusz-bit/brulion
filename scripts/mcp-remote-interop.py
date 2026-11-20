"""brulion-cli mcp serve against the official MCP Python SDK (scripts/mcp-remote-interop.sh runs it).

The SDK's own OAuth client discovers the server (protected resource and authorization server
metadata), registers itself (dynamic client registration), runs the authorization code flow with
PKCE and checks the RFC 9207 issuer; this script plays the owner in the browser: it signs in on
the sign-in page and allows the connection. Then MCP over Streamable HTTP in both eras the SDK
speaks — "auto" (server/discover, the per-request 2026-07-28 protocol) and "legacy" (initialize,
a session): tools/list and tools/call (search finds Polish text, capture writes, a dry run).

    python3 -I mcp-remote-interop.py <endpoint> <password> <vault>
"""

import asyncio
import json
import pathlib
import re
import sys
import urllib.parse

import httpx2
from mcp import Client
from mcp.client.auth import OAuthClientProvider, TokenStorage
from mcp.client.streamable_http import streamable_http_client
from mcp.shared.auth import AuthorizationCodeResult, OAuthClientInformationFull, OAuthClientMetadata, OAuthToken

ENDPOINT, PASSWORD, VAULT = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
BASE = ENDPOINT[: -len("/mcp")]
REDIRECT = "http://127.0.0.1:9/callback"


class Memory(TokenStorage):
    def __init__(self):
        self.tokens = None
        self.client = None

    async def get_tokens(self):
        return self.tokens

    async def set_tokens(self, tokens: OAuthToken):
        self.tokens = tokens

    async def get_client_info(self):
        return self.client

    async def set_client_info(self, info: OAuthClientInformationFull):
        self.client = info


def hidden(html: str, name: str) -> str:
    match = re.search(rf'name="{name}" value="([^"]*)"', html)
    assert match, f"no {name} in the page: {html[:400]}"
    return match.group(1)


class Owner:
    """The person at the browser: signs in and allows; hands the code to the SDK."""

    def __init__(self):
        self.result = None

    async def redirect(self, url: str):
        async with httpx2.AsyncClient() as browser:
            page = await browser.get(url)
            assert page.status_code == 200, (page.status_code, page.text[:400])
            request = hidden(page.text, "request")
            consent = await browser.post(
                BASE + "/authorize",
                data={"request": request, "csrf": hidden(page.text, "csrf"), "action": "signin", "password": PASSWORD},
            )
            assert 'value="allow"' in consent.text, consent.text[:400]
            answer = await browser.post(
                BASE + "/authorize",
                data={"request": request, "csrf": hidden(consent.text, "csrf"), "action": "allow", "write": "on"},
            )
            assert answer.status_code == 303, (answer.status_code, answer.text[:400])
            location = urllib.parse.urlparse(answer.headers["location"])
            assert location.geturl().startswith(REDIRECT), location
            query = dict(urllib.parse.parse_qsl(location.query))
            self.result = AuthorizationCodeResult(code=query["code"], state=query.get("state"), iss=query.get("iss"))

    async def callback(self) -> AuthorizationCodeResult:
        assert self.result is not None
        return self.result


def envelope(result) -> dict:
    assert not result.is_error, result
    data = result.structured_content or json.loads(result.content[0].text)
    assert data["ok"], data
    return data["result"]


async def run(mode: str):
    owner = Owner()
    storage = Memory()
    auth = OAuthClientProvider(
        server_url=ENDPOINT,
        client_metadata=OAuthClientMetadata(
            client_name="MCP Python SDK (interop)",
            redirect_uris=[REDIRECT],
            grant_types=["authorization_code", "refresh_token"],
            response_types=["code"],
            token_endpoint_auth_method="none",
            scope="notes:read notes:write",
        ),
        storage=storage,
        redirect_handler=owner.redirect,
        callback_handler=owner.callback,
    )
    async with httpx2.AsyncClient(auth=auth, timeout=60) as http:
        async with Client(streamable_http_client(ENDPOINT, http_client=http), mode=mode) as client:
            print(f"[{mode}] protocol {client.protocol_version}")
            names = [t.name for t in (await client.list_tools()).tools]
            assert "brulion_search" in names and "brulion_capture" in names, names
            assert not any(n.startswith("brulion_app_") for n in names), names
            hits = envelope(await client.call_tool("brulion_search", {"text": "zolw"}))["hits"]
            assert any("żółw" in h["excerpt"] for h in hits), hits
            dry = envelope(await client.call_tool("brulion_journal_append", {"text": "Próba", "dryRun": True}))
            assert dry["dryRun"], dry
            envelope(await client.call_tool("brulion_capture", {"text": f"Zapisane przez SDK ({mode})"}))
            print(f"[{mode}] {len(names)} tools, search and capture work")
    assert storage.tokens is not None and storage.tokens.refresh_token, storage.tokens
    assert storage.client is not None and storage.client.client_id.startswith("bcl_"), storage.client


async def main():
    for mode in ("auto", "legacy"):
        await run(mode)
    journal = "\n".join(p.read_text(encoding="utf-8") for p in (VAULT / "journals").glob("*.md"))
    assert "Zapisane przez SDK (auto)" in journal and "Zapisane przez SDK (legacy)" in journal, journal
    print("interop: ok")


asyncio.run(main())
