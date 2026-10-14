import { composeSharedText, parseShareTarget, stripShareParams } from './share-target';

describe('share target', () => {
  it('parses a share-target launch URL', () => {
    expect(parseShareTarget('https://x.pl/app/?share-target&title=Artyku%C5%82&text=Ciekawe&url=https%3A%2F%2Fa.pl')).toEqual({
      title: 'Artykuł',
      text: 'Ciekawe',
      url: 'https://a.pl',
    });
    expect(parseShareTarget('https://x.pl/app/?share-target')).toBeNull();
    expect(parseShareTarget('https://x.pl/app/?title=x')).toBeNull();
    expect(parseShareTarget('not a url')).toBeNull();
  });

  it('strips its parameters and keeps others', () => {
    expect(stripShareParams('https://x.pl/app/?share-target&title=a&text=b&url=c&keep=1#h')).toBe('https://x.pl/app/?keep=1#h');
  });

  it('composes one capture line', () => {
    expect(composeSharedText({ title: 'Przepis na żurek', text: '', url: 'https://kuchnia.pl/zurek' })).toBe(
      '[Przepis na żurek](https://kuchnia.pl/zurek)',
    );
    // Android puts the URL into `text`.
    expect(composeSharedText({ title: 'Wideo', text: 'Zobacz to https://yt.be/abc', url: '' })).toBe('[Wideo](https://yt.be/abc) — Zobacz to');
    expect(composeSharedText({ title: '', text: 'https://a.pl', url: '' })).toBe('<https://a.pl>');
    expect(composeSharedText({ title: 'Notatka', text: 'Notatka', url: '' })).toBe('Notatka');
    expect(composeSharedText({ title: 'Tytuł [x]', text: 'tekst https://b.pl', url: 'https://b.pl' })).toBe('[Tytuł x](https://b.pl) — tekst');
    expect(composeSharedText({ title: '', text: 'Sam tekst', url: '' })).toBe('Sam tekst');
  });
});
