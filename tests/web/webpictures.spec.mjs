// Pictures from the internet in the web build, in Chromium and Firefox (docs/WEB.md "Obrazy
// z internetu", "Testy w przeglądarkach"): the real editor fetches them (the notebook's setting
// "show without asking" is on), the app reports what it got (?selftest=webpictures:
// src/app/diag/webselftest.h) and exports the notebook with copies; the picture servers log what
// the browser really sent.
//
//  1. a page over https: a picture from a server with CORS is shown (and drawn), one without
//     CORS fails with a message naming CORS, http:// on the https page fails with the
//     mixed-content message and is never sent, a 404 and a page that is no picture fail with
//     their own messages; no preflight (OPTIONS) reaches any server and the GET carries only
//     safe-listed headers; the export with copies includes the CORS picture and one it fetches
//     itself, keeps the others' addresses with a warning each, and the browser gets the file;
//  2. a page over http (an insecure context: the app still starts): the http:// picture works,
//     the one without CORS names CORS, the export arrives as a download.
import { GALLERY_SVG, PICTURE } from './lib/servers.mjs';
import { expect, test } from './lib/fixtures.mjs';
import { countColour, openApp, png, recordSaves, requireWebGl2, savedFile, TODAY, waitGlobal } from './lib/brulion.mjs';
import { seed, settingsIni } from './lib/idbfs.mjs';

const JOURNAL = `journals/${TODAY}.md`;
const ROUTE = { view: 'page', path: JOURNAL };
const ACCEPT = 'image/avif,image/webp,image/png,image/svg+xml,image/*;q=0.8';
const SHOWN = settingsIni('[editor]\nwebPicturesShown=true\n');

function byUrl(report) {
  return Object.fromEntries(report.pictures.map((p) => [p.url, p]));
}

async function attachJson(testInfo, name, value) {
  await testInfo.attach(name, { body: JSON.stringify(value, null, 2), contentType: 'application/json' });
}

test.describe('Obrazy z internetu w przeglądarce', () => {
  test('strona przez https: CORS, mieszana treść, brak preflight, eksport z kopiami',
    async ({ page, servers, watched, browserName }, testInfo) => {
      const { page: site, imgCors, imgNoCors, imgHttp } = servers;
      await requireWebGl2(page, site.origin, browserName);
      servers.reset();
      const rows = {
        cors: `${imgCors.origin}/łąka.png`, // raw Polish in the address
        nocors: `${imgNoCors.origin}/%C5%82%C4%85ka.png`, // percent-encoded
        http: `${imgHttp.origin}/łąka.png`,
        missing: `${imgCors.origin}/brak-zdjęcia.png`,
        notPicture: `${imgCors.origin}/strona.html`,
        redirect: `${imgCors.origin}/przekierowanie.png`,
      };
      const gallery = `${imgCors.origin}/galeria-żółw.svg`;
      const journal = [
        `- Łąka z serwera z CORS ![łąka](${rows.cors})`,
        `- Zdjęcie z serwera bez CORS ![zdjęcie](${rows.nocors})`,
        `- Obraz przez http:// ![obraz](${rows.http})`,
        `- Brak zdjęcia ![brak](${rows.missing})`,
        `- Strona zamiast obrazu ![strona](${rows.notPicture})`,
        `- Przekierowanie na http:// ![przekierowanie](${rows.redirect})`,
        '- Zażółć gęślą jaźń',
        '',
      ].join('\n');
      await seed(page, site.origin, {
        files: { [JOURNAL]: journal, 'Galeria.md': `- Żółw tylko w eksporcie ![żółw](${gallery})\n` },
        settings: SHOWN,
      });
      if (browserName === 'chromium') await recordSaves(page);
      await openApp(page, site.origin, { selftest: 'webpictures', route: ROUTE });

      // The pictures as the editor got them.
      const report = await waitGlobal(page, 'brulionWebPictureSelfTest', (v) => v.done, 150_000);
      await attachJson(testInfo, 'brulionWebPictureSelfTest.json', report);
      expect(report.pageScheme).toBe('https');
      expect(report.refused).toBe(false);
      expect(report.timedOut).toBe(false);
      const p = byUrl(report);
      expect(Object.keys(p).sort()).toEqual(Object.values(rows).sort());
      expect(p[rows.cors]).toMatchObject({ state: 'ready', format: 'png', width: PICTURE.width,
        height: PICTURE.height, uiFetched: true });
      expect(p[rows.nocors].state).toBe('failed');
      expect(p[rows.nocors].error).toContain('img-nocors.brulion.test');
      expect(p[rows.nocors].error).toContain('(CORS)');
      expect(p[rows.http].state).toBe('failed');
      expect(p[rows.http].error).toContain('http://');
      expect(p[rows.http].error).toContain('https://');
      expect(p[rows.http].error).not.toContain('(CORS)');
      expect(p[rows.missing].state).toBe('failed');
      expect(p[rows.missing].error).toContain('404');
      expect(p[rows.notPicture].state).toBe('failed');
      expect(p[rows.notPicture].error).toContain('img-cors.brulion.test');
      expect(p[rows.notPicture].error).not.toContain('(CORS)');
      // A redirect to http:// the browser follows and blocks itself, telling the page nothing.
      expect(p[rows.redirect].state).toBe('failed');
      testInfo.annotations.push({ type: 'redirect https → http', description: p[rows.redirect].error });
      // The editor shows the messages.
      expect(report.shownTexts).toContain(p[rows.nocors].error);
      expect(report.shownTexts).toContain(p[rows.http].error);

      // The CORS picture is drawn, not only fetched.
      let shot;
      await expect.poll(async () => {
        shot = png(await page.screenshot());
        return Math.min(countColour(shot, PICTURE.left), countColour(shot, PICTURE.right));
      }, { message: 'pixels of the picture from the CORS server on screen', timeout: 30_000 }).toBeGreaterThanOrEqual(600);
      await testInfo.attach('dziennik.png', { body: await page.screenshot(), contentType: 'image/png' });

      // What reached the servers.
      for (const s of servers.pictureServers) {
        expect(s.requests.filter((r) => r.method === 'OPTIONS'), `preflight on ${s.name}`).toEqual([]);
      }
      const get = imgCors.requests.find((r) => r.method === 'GET' && r.path === '/łąka.png');
      expect(get, 'GET /łąka.png on the CORS server').toBeTruthy();
      expect(get.headers.origin).toBe(site.origin);
      expect(get.headers.accept).toBe(ACCEPT);
      expect(get.headers['accept-language']).not.toBe('*');
      expect(get.headers['user-agent']).not.toBe('Mozilla/5.0');
      expect(get.headers.cookie).toBeUndefined();
      expect(imgNoCors.requests.filter((r) => r.method === 'GET').length).toBeGreaterThan(0);
      // Mixed content is blocked before it is sent (also after the redirect).
      expect(imgHttp.requests, 'requests to the http server from the https page').toEqual([]);
      testInfo.annotations.push({ type: 'TLS hellos on the http server', description: String(imgHttp.tlsHellos) });
      const consoleSays = (re) => watched.log.some((l) => re.test(l));
      testInfo.annotations.push({
        type: 'console',
        description: `mixed content: ${consoleSays(/mixed/i)}, CORS: ${consoleSays(/CORS|Cross-Origin/i)}`,
      });

      // The export with copies.
      const exported = await waitGlobal(page, 'brulionExportSelfTest', (v) => !!v.state, 150_000);
      await attachJson(testInfo, 'brulionExportSelfTest.json', exported);
      expect(exported).toMatchObject({ state: 'saved', saved: true, format: 'html-single', scope: 'vault', errorText: '' });
      expect(exported.fileName).toMatch(/\.html$/);
      // The CORS picture from the cache and the gallery's, fetched by the export itself.
      expect(exported.dataPictures).toBe(2);
      expect(imgCors.requests.filter((r) => r.method === 'GET' && r.path === '/galeria-żółw.svg')).toHaveLength(1);
      const hosts = exported.warnings.map((w) => /img-[a-z]+\.brulion\.test/.exec(w)?.[0]);
      expect(hosts.sort()).toEqual(['img-cors.brulion.test', 'img-cors.brulion.test', 'img-cors.brulion.test',
        'img-http.brulion.test', 'img-nocors.brulion.test']);
      for (const s of servers.pictureServers) {
        expect(s.requests.filter((r) => r.method === 'OPTIONS'), `preflight on ${s.name}`).toEqual([]);
      }
      expect(imgHttp.requests).toEqual([]);

      // The file the browser received.
      const file = await savedFile(page, watched);
      testInfo.annotations.push({ type: 'saved by', description: `${file.how}: ${file.name}` });
      console.log(`[${browserName}] https page: redirect → "${p[rows.redirect].error}"; `
        + `TLS hellos on the http server: ${imgHttp.tlsHellos}; export ${exported.bytes} B by ${file.how}`);
      expect(file.name).toMatch(/\.html$/);
      expect(file.bytes.length).toBe(exported.bytes);
      const html = file.bytes.toString('utf8');
      expect(html).toContain(`data:image/png;base64,${servers.png.toString('base64')}`);
      expect(html).toContain(`data:image/svg+xml;base64,${GALLERY_SVG.toString('base64')}`);
      expect(html).toContain(`img-nocors.brulion.test:${imgNoCors.port}`);
      expect(html).toContain(`img-http.brulion.test:${imgHttp.port}`);
    });

  test('strona przez http: obraz przez http działa, bez CORS komunikat nazywa CORS',
    async ({ page, servers, watched, browserName }, testInfo) => {
      const { pageHttp: site, imgNoCors, imgHttp } = servers;
      await requireWebGl2(page, site.origin, browserName);
      servers.reset();
      const rows = {
        nocors: `${imgNoCors.origin}/łąka.png`,
        http: `${imgHttp.origin}/%C5%82%C4%85ka.png`,
      };
      await seed(page, site.origin, {
        files: { [JOURNAL]: `- Bez CORS ![zdjęcie](${rows.nocors})\n- Przez http:// ![obraz](${rows.http})\n` },
        settings: SHOWN,
      });
      // An insecure context: no Web Locks, no File System Access, no service worker.
      expect(await page.evaluate(() => window.isSecureContext)).toBe(false);
      await openApp(page, site.origin, { selftest: 'webpictures', route: ROUTE });

      const report = await waitGlobal(page, 'brulionWebPictureSelfTest', (v) => v.done, 150_000);
      await attachJson(testInfo, 'brulionWebPictureSelfTest.json', report);
      expect(report.pageScheme).toBe('http');
      expect(report.refused).toBe(false);
      expect(report.timedOut).toBe(false);
      const p = byUrl(report);
      expect(p[rows.http]).toMatchObject({ state: 'ready', format: 'png', width: PICTURE.width,
        height: PICTURE.height, uiFetched: true });
      expect(p[rows.nocors].state).toBe('failed');
      expect(p[rows.nocors].error).toContain('(CORS)');
      expect(p[rows.nocors].error).toContain('img-nocors.brulion.test');
      expect(report.shownTexts).toContain(p[rows.nocors].error);

      const get = imgHttp.requests.find((r) => r.method === 'GET' && r.path === '/łąka.png');
      expect(get, 'GET /łąka.png on the http server').toBeTruthy();
      expect(get.headers.origin).toBe(site.origin);
      expect(get.headers.accept).toBe(ACCEPT);
      for (const s of servers.pictureServers) {
        expect(s.requests.filter((r) => r.method === 'OPTIONS'), `preflight on ${s.name}`).toEqual([]);
      }

      const exported = await waitGlobal(page, 'brulionExportSelfTest', (v) => !!v.state, 150_000);
      await attachJson(testInfo, 'brulionExportSelfTest.json', exported);
      expect(exported).toMatchObject({ state: 'saved', saved: true, dataPictures: 1 });
      expect(exported.warnings).toHaveLength(1);
      expect(exported.warnings[0]).toContain('img-nocors.brulion.test');
      const file = await savedFile(page, watched);
      expect(file.how).toBe('download');
      expect(file.name).toMatch(/\.html$/);
      const html = file.bytes.toString('utf8');
      expect(html).toContain(`data:image/png;base64,${servers.png.toString('base64')}`);
      expect(html).toContain(`img-nocors.brulion.test:${imgNoCors.port}`);
    });
});
