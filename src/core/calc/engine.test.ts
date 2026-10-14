import { describe, expect, it } from 'vitest';
import { evaluateCalc, evaluateInline } from './calc';
import { DISABLED_FUNCTIONS, elementCount, getEngine, GuardedScope, MAX_ELEMENTS } from './engine';
import { CalcError } from './messages';

const inline = (expr: string) => evaluateInline(expr, {}, 'pl');

describe('security: disabled functions', () => {
  it.each([...DISABLED_FUNCTIONS, 'typed'])('%s() fails safely', (name) => {
    const r = inline(`${name}("1")`);
    expect(r.value).toBeUndefined();
    expect(r.error).toBe(`Funkcja ${name} jest wyłączona`);
  });

  it('reports disabled functions in English too', () => {
    expect(evaluateInline('import({a: 1})', {}, 'en').error).toBe('Function import is disabled');
  });

  it('cannot reach disabled functions through variables', () => {
    const r = evaluateCalc('f = evaluate\nf("1 + 1")\ng = config\ng({number: "BigNumber"})');
    expect(r.lines[1]!.error).toMatch(/wyłączona/);
    expect(r.lines[3]!.error).toMatch(/wyłączona/);
  });

  it('cannot assign to disabled names', () => {
    expect(evaluateCalc('parse = 5').lines[0]!.error).toBe('Nie można przypisać do „parse”');
    expect(evaluateCalc('h(x) = x\nconfig(x) = x').lines[1]!.error).toMatch(/config/);
  });

  it('keeps the configuration intact after attempts to change it', () => {
    inline('config({number: "BigNumber", precision: 500})');
    expect(inline('1/3').value).toBe('0,3333333333');
    expect(typeof getEngine().evaluate('1/3', new GuardedScope(new Map()))).toBe('number');
  });

  it('keeps functions working that read the configuration internally', () => {
    // Regression: replacing `config` in the namespace handed lazily created functions the stub
    // instead of the configuration object.
    expect(inline('factorial(bignumber(10))').value).toBe('3\u00a0628\u00a0800');
    expect(inline('eigs([[2, 0], [0, 1]]).values').value).toBe('[1; 2]');
    expect(inline('intersect([0, 0], [1, 1], [1, 0], [0, 1])').value).toBe('[0,5; 0,5]');
    expect(inline('schur([[1, 0], [0, 2]]).U').value).toBe('[[1; 0]; [0; 1]]');
    expect(inline('lyap([[-1, 0], [0, -1]], [[1, 0], [0, 1]])').value).toBe('[[0,5; 0]; [0; 0,5]]');
    expect(inline('config()').error).toBe('Funkcja config jest wyłączona');
  });

  it('does not persist units created by expressions', () => {
    inline('createUnit("foo")');
    expect(inline('5 foo').error).toBe('Nieznana zmienna: foo');
  });

  it('blocks access to constructors and prototypes', () => {
    for (const expr of ['cos.constructor', 'cos["constructor"]', '[].constructor', '"".constructor', 'a = {}; a.__proto__']) {
      const r = inline(expr);
      expect(r.value).toBeUndefined();
      expect(r.error).toBeTruthy();
    }
  });

  it('does not touch the global mathjs instance', async () => {
    const mathjs = await import('mathjs');
    expect(mathjs.evaluate('1 + 1')).toBe(2);
  });
});

describe('security: resource limits', () => {
  it.each(['zeros(10000, 10000)', 'ones([1000, 1000])', 'identity(1000)', '1:1e9', 'range(0, 1e9)', 'range("0:1e9")', 'diag(ones(1000))', 'kron(ones(400), ones(400))', 'random([1000, 1000])', 'matrixFromFunction([1000, 1000], f(i) = 1)'])(
    '%s is rejected',
    (expr) => {
      expect(inline(expr).error).toBe(`Wynik jest za duży (limit ${MAX_ELEMENTS} elementów)`);
    },
  );

  it('allows small allocations', () => {
    expect(inline('zeros(2, 2)').value).toBe('[[0; 0]; [0; 0]]');
    expect(inline('size(1:10)').value).toBe('[10]');
  });

  it('rejects overly long expressions', () => {
    expect(inline('1+'.repeat(3000) + '1').error).toBe('Wyrażenie jest za długie');
  });

  it('survives infinite recursion', () => {
    const r = evaluateCalc('f(x) = f(x)\nf(1)\n2 + 2');
    expect(r.lines[1]!.error).toBeTruthy();
    expect(r.lines[2]!.value).toBe('4');
  });
});

describe('GuardedScope', () => {
  it('resolves names in priority order', () => {
    const vars = new Map<string, unknown>([['x', 1]]);
    const scope = new GuardedScope(vars, new Map([['$$t', 2]]), new Map([['helper', 3]]));
    expect(scope.get('x')).toBe(1);
    expect(scope.get('$$t')).toBe(2);
    expect(scope.get('helper')).toBe(3);
    expect(scope.has('typed')).toBe(true);
    expect(() => (scope.get('typed') as () => unknown)()).toThrow(CalcError);
    expect(scope.has('nothing')).toBe(false);
    expect([...scope.keys()]).toContain('x');
  });

  it('writes assignments to the variables and refuses reserved names', () => {
    const vars = new Map<string, unknown>();
    const scope = new GuardedScope(vars, new Map([['$$t', 2]]));
    scope.set('y', 5);
    expect(vars.get('y')).toBe(5);
    expect(() => scope.set('import', 1)).toThrow(CalcError);
    expect(() => scope.set('$$t', 1)).toThrow(CalcError);
    expect(scope.delete('y')).toBe(true);
    expect(vars.has('y')).toBe(false);
  });
});

describe('elementCount', () => {
  it('counts nested arrays and matrices', () => {
    const math = getEngine().math;
    expect(elementCount(math, 5)).toBe(1);
    expect(elementCount(math, [[1, 2], [3, 4]])).toBe(4);
    expect(elementCount(math, math.zeros(3, 4))).toBe(12);
  });
});
