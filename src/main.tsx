/**
 * Bootstrap: styles → <App/> (splash) → platform → device settings → vault (see
 * src/app/services/boot.ts). Rendering starts at once so the splash shows while IndexedDB
 * and the platform initialise.
 */
import './app/styles/theme.css';
import './app/styles/components.css';
import './app/styles/editor.css';
import './app/styles/app.css';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './app/App';
import { bootApp } from './app/services/boot';

const root = document.getElementById('root');
if (!root) throw new Error('index.html has no #root');

createRoot(root).render(
  <StrictMode>
    <App />
  </StrictMode>,
);

void bootApp().catch((e: unknown) => {
  console.error('[boot] failed', e);
});
