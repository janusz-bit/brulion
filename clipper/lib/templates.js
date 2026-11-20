// The templates a clip can use (docs/CLIPPER.md "Templates"): Brulion's built-in ones (the host
// lists them: brulion, obsidian, joplin, link), the notebook's template pages, and the user's own
// from the options. Pure (tests: clipper/test/).

export const BUILTIN_IDS = ['brulion', 'obsidian', 'joplin', 'link'];

/**
 * The options of a template list: [{value, label, group}] — group "builtin", "notebook" or "custom".
 * `hostTemplates`: hello's templates ([{id, name, builtin} | {page, name, builtin: false}]); without a
 * host the built-ins alone. `t(key)`: a translation (built-ins are named by "template_<id>").
 */
export function templateOptions(hostTemplates, customTemplates, t) {
  const out = [];
  const builtins = (hostTemplates ?? []).filter((x) => x?.builtin && typeof x.id === 'string');
  const ids = builtins.length > 0 ? builtins.map((x) => x.id) : BUILTIN_IDS;
  for (const id of ids) {
    const key = `template_${id}`;
    const translated = t(key);
    const fallback = builtins.find((x) => x.id === id)?.name ?? id;
    out.push({ value: `builtin:${id}`, label: translated && translated !== key ? translated : fallback, group: 'builtin' });
  }
  for (const page of hostTemplates ?? []) {
    if (page?.builtin || typeof page?.page !== 'string') continue;
    out.push({ value: `page:${page.page}`, label: page.name || page.page, group: 'notebook' });
  }
  for (const custom of customTemplates ?? []) out.push({ value: `custom:${custom.id}`, label: custom.name, group: 'custom' });
  return out;
}

/** `value` when the list has it, else the list's built-in default for the place. */
export function pickTemplate(options, value, place) {
  if (options.some((o) => o.value === value)) return value;
  const fallback = place === 'page' ? 'builtin:brulion' : 'builtin:link';
  return options.some((o) => o.value === fallback) ? fallback : options[0]?.value ?? fallback;
}
