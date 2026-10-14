/**
 * Entry for the component gallery page (gallery.html at the repo root points here).
 * Not imported by src/main.tsx.
 */
import '../styles/theme.css';
import '../styles/components.css';
import { mountGallery } from './Gallery';

const el =
  document.getElementById('root') ??
  (() => {
    const div = document.createElement('div');
    div.id = 'root';
    document.body.appendChild(div);
    return div;
  })();

mountGallery(el);
