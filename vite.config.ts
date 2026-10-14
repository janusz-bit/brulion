import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
import { VitePWA } from 'vite-plugin-pwa';
import { fileURLToPath, URL } from 'node:url';

// TAURI_ENV_PLATFORM is set by the Tauri CLI when it runs the dev server / build.
const isTauri = !!process.env.TAURI_ENV_PLATFORM;
// GitHub Pages serves the web version from /<repo>/ — override with BRULION_BASE.
const base = process.env.BRULION_BASE ?? '/';

export default defineConfig({
  base,
  clearScreen: false,
  resolve: {
    alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) },
  },
  plugins: [
    react(),
    // The service worker only makes sense for the browser (PWA) build.
    !isTauri &&
      VitePWA({
        registerType: 'prompt',
        injectRegister: false,
        includeAssets: ['favicon.svg', 'icons/*.png'],
        manifest: {
          name: 'Brulion',
          short_name: 'Brulion',
          description: 'Offline-first notatki w Markdown',
          lang: 'pl',
          theme_color: '#1f2430',
          background_color: '#1f2430',
          display: 'standalone',
          start_url: '.',
          scope: '.',
          icons: [
            { src: 'icons/icon-192.png', sizes: '192x192', type: 'image/png' },
            { src: 'icons/icon-512.png', sizes: '512x512', type: 'image/png' },
            { src: 'icons/icon-512.png', sizes: '512x512', type: 'image/png', purpose: 'maskable' },
          ],
          share_target: {
            action: './?share-target',
            method: 'GET',
            params: { title: 'title', text: 'text', url: 'url' },
          },
        },
        workbox: {
          globPatterns: ['**/*.{js,css,html,svg,png,woff2,wasm}'],
          maximumFileSizeToCacheInBytes: 8 * 1024 * 1024,
          navigateFallback: 'index.html',
        },
      }),
  ],
  server: {
    port: 1420,
    strictPort: true,
    host: process.env.TAURI_DEV_HOST || false,
  },
  envPrefix: ['VITE_', 'TAURI_ENV_'],
  build: {
    target: isTauri ? ['es2022', 'safari16'] : 'es2022',
    sourcemap: true,
    chunkSizeWarningLimit: 4000,
  },
  test: {
    globals: true,
    environment: 'node',
    include: ['src/**/*.test.ts', 'src/**/*.test.tsx'],
    setupFiles: ['./src/test/setup.ts'],
  },
});
