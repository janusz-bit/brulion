import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

function Placeholder() {
  return <div>Brulion</div>;
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <Placeholder />
  </StrictMode>,
);
