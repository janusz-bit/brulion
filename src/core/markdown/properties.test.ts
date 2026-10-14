import { describe, expect, it } from 'vitest';
import {
  cleanName,
  frontmatterGet,
  frontmatterProperties,
  namesFrom,
  parsePropertyValue,
  refName,
  setProperty,
  splitTopLevelCommas,
  uniqueCaseInsensitive,
} from './properties';
import { STATUS_CHAR, checkboxCharFor, keywordFor, matchTaskMarker, statusFromCheckbox, statusFromKeyword } from './tasks';

describe('property values', () => {
  it('splits on top-level commas only', () => {
    expect(splitTopLevelCommas('[[a, b]], c, "d, e", ((x,y))')).toEqual(['[[a, b]]', ' c', ' "d, e"', ' ((x,y))']);
  });

  it('recognises refs', () => {
    expect(refName('[[Strona]]')).toBe('Strona');
    expect(refName('#[[Dwa słowa]]')).toBe('Dwa słowa');
    expect(refName('#zakupy/spożywcze')).toBe('zakupy/spożywcze');
    expect(refName('#123')).toBeNull();
    expect(refName('tekst')).toBeNull();
  });

  it('parses lists of refs, keeps everything else raw', () => {
    expect(parsePropertyValue('related', '[[A]], [[B C]], #d')).toEqual(['A', 'B C', 'd']);
    expect(parsePropertyValue('related', '[[A]]')).toEqual(['A']);
    expect(parsePropertyValue('note', '[[A]] and text')).toBe('[[A]] and text');
    expect(parsePropertyValue('price', '12,50 zł')).toBe('12,50 zł');
    expect(parsePropertyValue('empty', '   ')).toBe('');
  });

  it('always lists tags and aliases', () => {
    expect(parsePropertyValue('tags', 'a, #b, [[c d]]')).toEqual(['a', 'b', 'c d']);
    expect(parsePropertyValue('alias', 'Jedyny')).toEqual(['Jedyny']);
    expect(parsePropertyValue('aliases', '')).toEqual([]);
  });

  it('cleanName strips decoration', () => {
    expect(cleanName(' "#[[x]]" ')).toBe('x');
    expect(cleanName("'y'")).toBe('y');
  });
});

describe('frontmatter conversion', () => {
  it('keeps scalars and lists, lower-cases keys, drops maps and nulls', () => {
    expect(
      frontmatterProperties({ Title: 'T', count: 3, ok: true, list: ['a', 2, ['Link']], map: { a: 1 }, none: null }),
    ).toEqual({ title: 'T', count: '3', ok: 'true', list: ['a', '2', '[[Link]]'] });
  });

  it('frontmatterGet is case-insensitive and namesFrom handles strings and lists', () => {
    expect(frontmatterGet({ Tags: 'x' }, 'tags')).toBe('x');
    expect(frontmatterGet({}, 'tags')).toBeUndefined();
    expect(namesFrom('a, b c', true)).toEqual(['a', 'b', 'c']);
    expect(namesFrom('a, b c', false)).toEqual(['a', 'b c']);
    expect(namesFrom(['#x', '#y #z', 5, ['Link']], true)).toEqual(['x', 'y', 'z', '5', 'Link']);
    expect(namesFrom(null, true)).toEqual([]);
  });

  it('setProperty is safe for __proto__', () => {
    const props = {};
    setProperty(props, '__proto__', 'x');
    expect(Object.getPrototypeOf(props)).toBe(Object.prototype);
    expect(Object.keys(props)).toEqual(['__proto__']);
  });

  it('uniqueCaseInsensitive keeps the first spelling', () => {
    expect(uniqueCaseInsensitive(['Łódź', 'łódź', 'Kraków'])).toEqual(['Łódź', 'Kraków']);
  });
});

describe('task markers', () => {
  it('maps checkbox characters and keywords', () => {
    expect(statusFromCheckbox(' ')).toBe('todo');
    expect(statusFromCheckbox('X')).toBe('done');
    expect(statusFromCheckbox('?')).toBe('todo');
    expect(statusFromKeyword('IN-PROGRESS')).toBe('doing');
    expect(statusFromKeyword('WAIT')).toBe('deferred');
    expect(statusFromKeyword('UNKNOWN')).toBe('todo');
    expect(STATUS_CHAR.cancelled).toBe('-');
  });

  it('chooses replacement markers', () => {
    expect(checkboxCharFor('done', 'X')).toBe('X');
    expect(checkboxCharFor('done', ' ')).toBe('x');
    expect(keywordFor('cancelled', 'CANCELLED')).toBe('CANCELLED');
    expect(keywordFor('deferred', 'WAIT')).toBe('WAIT');
    expect(keywordFor('todo')).toBe('TODO');
  });

  it('matchTaskMarker finds checkboxes and keywords at the content start', () => {
    expect(matchTaskMarker('- [x] a', '- [x] a', 2)).toMatchObject({ kind: 'checkbox', status: 'done', marker: 'x', from: 2, to: 5, contentStart: 6 });
    expect(matchTaskMarker('- TODO', '- TODO', 2)).toMatchObject({ kind: 'keyword', status: 'todo', contentStart: 6 });
    expect(matchTaskMarker('- TODOs', '- TODOs', 2)).toBeNull();
    expect(matchTaskMarker('- [x](url)', '- [x](url)', 2)).toBeNull();
    expect(matchTaskMarker('- []', '- []', 2)).toBeNull();
  });
});
