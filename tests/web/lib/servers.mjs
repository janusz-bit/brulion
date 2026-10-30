// The servers of the browser tests, all on 127.0.0.1 with ports the system picks (the browsers
// map the host names to 127.0.0.1 themselves: playwright.config.mjs):
//
//   page       https://brulion.test:<p>       the site (/brulion/ → $BRULION_DIST) and /__blank.html
//   pageHttp   http://brulion.test:<p>        the same over http (an insecure context)
//   imgCors    https://img-cors.brulion.test   pictures, Access-Control-Allow-Origin: *
//   imgNoCors  https://img-nocors.brulion.test pictures, no CORS headers
//   imgHttp    http://img-http.brulion.test    pictures over http, Access-Control-Allow-Origin: *
//
// Pictures (paths compared decoded): /łąka.png (64×48, left half #ff00ff, right half #00ff00 —
// colours no theme uses), /galeria-żółw.svg, /strona.html (a page, not a picture),
// /przekierowanie.png (302 to the http server's /łąka.png); anything else 404. Every server keeps
// a log of its requests ({method, path, headers}); the http ones also count TLS hellos (a
// browser that tried https on them). Connection: close everywhere, so the logs are exact.
import { readFile } from 'node:fs/promises';
import http from 'node:http';
import https from 'node:https';
import { extname, join, normalize, resolve, sep } from 'node:path';
import { PNG } from 'pngjs';

export const PICTURE = { width: 64, height: 48, left: [0xff, 0x00, 0xff], right: [0x00, 0xff, 0x00] };

/** The 64×48 PNG of /łąka.png. */
export function picturePng() {
  const png = new PNG({ width: PICTURE.width, height: PICTURE.height });
  for (let y = 0; y < png.height; y++) {
    for (let x = 0; x < png.width; x++) {
      const c = x < png.width / 2 ? PICTURE.left : PICTURE.right;
      const i = (y * png.width + x) * 4;
      png.data[i] = c[0];
      png.data[i + 1] = c[1];
      png.data[i + 2] = c[2];
      png.data[i + 3] = 0xff;
    }
  }
  return PNG.sync.write(png);
}

export const GALLERY_SVG = Buffer.from(
  '<svg xmlns="http://www.w3.org/2000/svg" width="40" height="30" viewBox="0 0 40 30">'
  + '<rect width="40" height="30" fill="#ffd400"/><circle cx="20" cy="15" r="10" fill="#0060ff"/></svg>');

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
  '.webmanifest': 'application/manifest+json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.css': 'text/css',
  '.txt': 'text/plain; charset=utf-8',
};

function listen(server) {
  return new Promise((ok, fail) => {
    server.once('error', fail);
    server.listen(0, '127.0.0.1', () => ok(server.address().port));
  });
}

function decodedPath(url) {
  const path = url.split('?')[0];
  try {
    return decodeURIComponent(path);
  } catch {
    return path;
  }
}

/** The site's files under `dist` at /brulion/, a blank page of the origin at /__blank.html. */
function siteHandler(dist) {
  const root = resolve(dist);
  return async (req, res) => {
    const path = decodedPath(req.url);
    res.setHeader('Cache-Control', 'no-store');
    if (path === '/__blank.html') {
      res.writeHead(200, { 'Content-Type': MIME['.html'] });
      res.end('<!doctype html><meta charset="utf-8"><title>Brulion — test</title>');
      return;
    }
    if (path === '/brulion') {
      res.writeHead(301, { Location: '/brulion/' });
      res.end();
      return;
    }
    if (!path.startsWith('/brulion/')) {
      res.writeHead(404);
      res.end();
      return;
    }
    let rel = path.slice('/brulion/'.length) || 'index.html';
    if (rel.endsWith('/')) rel += 'index.html';
    const file = normalize(join(root, rel));
    if (file !== root && !file.startsWith(root + sep)) {
      res.writeHead(403);
      res.end();
      return;
    }
    try {
      const body = await readFile(file);
      res.writeHead(200, { 'Content-Type': MIME[extname(file)] || 'application/octet-stream' });
      res.end(req.method === 'HEAD' ? undefined : body);
    } catch {
      res.writeHead(404);
      res.end();
    }
  };
}

/** A picture server; `cors`: send Access-Control-Allow-Origin: *. */
function pictureHandler({ cors, png, redirectTo }) {
  return (req, res) => {
    const path = decodedPath(req.url);
    const headers = { Connection: 'close', 'Cache-Control': 'no-store' };
    if (cors) headers['Access-Control-Allow-Origin'] = '*';
    if (req.method === 'OPTIONS') {
      // A preflight (the tests expect none): answered as a permissive server would.
      res.writeHead(204, { ...headers, 'Access-Control-Allow-Headers': '*', 'Access-Control-Allow-Methods': 'GET' });
      res.end();
      return;
    }
    let status = 200;
    let body;
    if (path === '/łąka.png') {
      headers['Content-Type'] = 'image/png';
      body = png;
    } else if (path === '/galeria-żółw.svg') {
      headers['Content-Type'] = 'image/svg+xml';
      body = GALLERY_SVG;
    } else if (path === '/strona.html') {
      headers['Content-Type'] = 'text/html; charset=utf-8';
      body = Buffer.from('<!doctype html><meta charset="utf-8"><title>Strona</title><p>Zażółć gęślą jaźń</p>');
    } else if (path === '/przekierowanie.png' && redirectTo) {
      status = 302;
      headers.Location = redirectTo();
      body = Buffer.alloc(0);
    } else {
      status = 404;
      headers['Content-Type'] = 'text/plain; charset=utf-8';
      body = Buffer.from('Nie ma');
    }
    headers['Content-Length'] = String(body.length);
    res.writeHead(status, headers);
    res.end(req.method === 'HEAD' ? undefined : body);
  };
}

async function start(name, { scheme, host, key, cert, handler }) {
  const entry = { name, scheme, host, port: 0, requests: [], tlsHellos: 0 };
  const logged = (req, res) => {
    entry.requests.push({ method: req.method, path: decodedPath(req.url), url: req.url, headers: { ...req.headers } });
    return handler(req, res);
  };
  const server = scheme === 'https' ? https.createServer({ key, cert }, logged) : http.createServer(logged);
  server.keepAliveTimeout = 0;
  server.on('clientError', (err, socket) => {
    // A TLS ClientHello (0x16 0x03…) on an http server: a browser that tried https here.
    const raw = err && err.rawPacket;
    if (scheme === 'http' && raw && raw[0] === 0x16 && raw[1] === 0x03) entry.tlsHellos++;
    socket.destroy();
  });
  entry.port = await listen(server);
  entry.origin = `${scheme}://${host}:${entry.port}`;
  entry.server = server;
  return entry;
}

/**
 * Starts every server. Returns { page, pageHttp, imgCors, imgNoCors, imgHttp, url(name, path),
 * reset() (clears the logs), stop() }.
 */
export async function startServers({ dist, key, cert }) {
  const png = picturePng();
  const servers = {};
  const site = siteHandler(dist);
  servers.page = await start('page', { scheme: 'https', host: 'brulion.test', key, cert, handler: site });
  servers.pageHttp = await start('pageHttp', { scheme: 'http', host: 'brulion.test', handler: site });
  servers.imgHttp = await start('imgHttp', {
    scheme: 'http', host: 'img-http.brulion.test', handler: pictureHandler({ cors: true, png }),
  });
  servers.imgCors = await start('imgCors', {
    scheme: 'https', host: 'img-cors.brulion.test', key, cert,
    handler: pictureHandler({ cors: true, png, redirectTo: () => `${servers.imgHttp.origin}/%C5%82%C4%85ka.png` }),
  });
  servers.imgNoCors = await start('imgNoCors', {
    scheme: 'https', host: 'img-nocors.brulion.test', key, cert, handler: pictureHandler({ cors: false, png }),
  });
  const pictureServers = [servers.imgCors, servers.imgNoCors, servers.imgHttp];
  return {
    ...servers,
    png,
    pictureServers,
    url: (name, path) => servers[name].origin + path,
    reset() {
      for (const s of Object.values(servers)) {
        s.requests.length = 0;
        s.tlsHellos = 0;
      }
    },
    async stop() {
      await Promise.all(Object.values(servers).map((s) => new Promise((ok) => {
        s.server.closeAllConnections?.();
        s.server.close(() => ok());
      })));
    },
  };
}
