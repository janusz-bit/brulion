/**
 * Test helpers for editor tests running in jsdom (not used by the app). jsdom lacks layout:
 * CodeMirror's measuring needs Range#getClientRects / getBoundingClientRect.
 */
export function installDomPolyfills(): void {
  if (typeof Range === 'undefined') return;
  const proto = Range.prototype as Range & { getClientRects?: () => DOMRectList; getBoundingClientRect?: () => DOMRect };
  const empty = { top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0, x: 0, y: 0, toJSON: () => ({}) } as DOMRect;
  if (typeof proto.getClientRects !== 'function') {
    proto.getClientRects = () => Object.assign([] as DOMRect[], { item: () => null }) as unknown as DOMRectList;
  }
  if (typeof proto.getBoundingClientRect !== 'function') proto.getBoundingClientRect = () => empty;
}

installDomPolyfills();
