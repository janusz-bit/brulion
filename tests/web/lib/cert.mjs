// A self-signed certificate for the test servers' host names (brulion.test and *.brulion.test,
// mapped to 127.0.0.1 inside the browsers): made by openssl for each run, valid for two days.
// The browsers accept it through Playwright's ignoreHTTPSErrors.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

/** { key, cert } (PEM) in `dir` (a new temporary folder by default). */
export function makeCertificate(dir = mkdtempSync(join(tmpdir(), 'brulion-cert-'))) {
  const key = join(dir, 'key.pem');
  const cert = join(dir, 'cert.pem');
  try {
    execFileSync('openssl', [
      'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
      '-subj', '/CN=brulion.test',
      '-addext', 'subjectAltName=DNS:brulion.test,DNS:*.brulion.test',
      '-keyout', key, '-out', cert,
    ], { stdio: ['ignore', 'ignore', 'pipe'] });
  } catch (e) {
    if (e.code === 'ENOENT') throw new Error('openssl is needed for the test certificate (not found on PATH)');
    throw new Error(`openssl failed: ${e.stderr || e.message}`);
  }
  return { key: readFileSync(key), cert: readFileSync(cert) };
}
