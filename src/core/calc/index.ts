/**
 * `@/core/calc` — calculation engine for calc blocks, inline `= expr` code and table formulas
 * (docs/SYNTAX.md §9). Built on a sandboxed mathjs instance (dangerous functions disabled).
 * See docs/ARCHITECTURE.md for the contract.
 */
export { evaluateCalc, evaluateInline } from './calc';
export type { CalcLineResult, EvaluateCalcOptions } from './calc';

export { findCalcBlocks, pageScope } from './scope';
export type { CalcBlockInfo } from './scope';

export { displayWidth, findTables, parseDelimiterRow, parseTable, serializeTable, splitTableRow } from './table';
export type { TableAlign, TableLocation, TableModel } from './table';

export { cellName, columnIndex, columnName, CYCLE, ERR, evaluateTable } from './tableEval';
export type { TableEvaluation } from './tableEval';

export { formatMoney, formatNumber, parseLocaleNumber } from './numbers';
export type { CurrencyInfo, NumberFormatOptions, ParsedNumber } from './numbers';

export { CALC_META } from './model';
export type { ValueMeta } from './model';

export { DISABLED_FUNCTIONS, MAX_ELEMENTS, MAX_EXACT_BITS, MAX_STRING_LENGTH, MAX_TOTAL_WORK, MAX_WORK } from './engine';
export { messages as calcMessages } from './messages';
