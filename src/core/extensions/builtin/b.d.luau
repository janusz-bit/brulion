-- The extension API `b` for Luau's type checker (src/core/extensions/typecheck.h, docs/EXTENSIONS.md
-- "Types"): what an extension page is checked against when it is saved. Loose where the API is
-- loose (a node of b.ui, a block's properties) and where working code is loose: a number argument
-- takes `number?` — what `tonumber` gives —, as the engine checks it when the line runs. A warning
-- must point at a mistake, never at working code. Keep it in step with engine.cpp — tst_extensions_types fails for a function of
-- `b` this file does not declare.

type Block = {
    ref: string,
    page: string,
    path: string,
    line: number,
    date: string?,
    text: string,
    plain: string,
    task: string?,
    tags: { string },
    props: { [string]: string },
    children: { Block },
    due: string?,
    scheduled: string?,
    start: string?,
    reminder: string?,
}

type Page = {
    name: string,
    path: string,
    tags: { string },
    props: { [string]: string },
    date: string?,
}

type QueryResult = {
    total: number,
    items: { any },
}

type Hit = {
    page: string,
    date: string?,
    excerpt: string,
    line: number?,
    ref: string?,
    text: string?,
    task: string?,
}

type Context = {
    page: string,
    block: Block?,
    args: { [string]: any },
    selection: string,
    messages: { any }?,
    conversation: string?,
    omitted: number?,
}

type Node = { [string]: any }

-- b.command's meta (docs/EXTENSIONS.md "Commands"): the palette's label, a command on the block
-- under the caret, offered by the palette or not, a suggested keyboard shortcut, a chat partner.
type CommandMeta = {
    label: string?,
    block: boolean?,
    palette: boolean?,
    key: string?,
    chat: boolean?,
    [string]: any,
}

-- What a macro's or a template variable's function gets (docs/EXTENSIONS.md "Macros").
type MacroContext = {
    args: { string },
    text: string,
    page: string,
    path: string,
    date: string?,
    template: string?,
}

type Line = {
    text: string,
    ref: string,
}

type Rule = {
    every: string,
    interval: number,
    weekdays: { number }?,
    monthDay: number?,
    lastDay: boolean?,
    workdays: boolean?,
    whenDone: boolean?,
}

type AiReply = {
    text: string,
    model: string,
    stopReason: string?,
    inputTokens: number?,
    outputTokens: number?,
    raw: any,
    toolCalls: { any }?,
    thoughts: string?,
}

declare b: {
    -- registering
    command: (name: string, fn: (ctx: Context) -> ...any, meta: CommandMeta?) -> (),
    view: (name: string, render: (params: { [string]: any }) -> any, actions: { [string]: (args: { [string]: any }) -> ...any }?, meta: { [string]: any }?) -> (),
    on: (event: string, fn: (e: { [string]: any }) -> ...any) -> (),
    settings: (fields: { { [string]: any } }?) -> { [string]: any },
    setting: (key: string) -> any,
    setSetting: (key: string, value: any) -> (),
    require: (name: string) -> any,
    macro: (name: string, fn: (ctx: MacroContext) -> any, meta: { [string]: any }?) -> (),
    variable: (name: string, fn: (ctx: MacroContext) -> any, meta: { [string]: any }?) -> (),

    -- reading
    pages: (filter: { tag: string?, prop: any, paths: boolean? }?) -> { string },
    page: (name: string) -> Page?,
    read: (name: string) -> string?,
    lines: (name: string) -> { Line }?,
    blocks: (query: { [string]: any }) -> { Block },
    search: (text: string, limit: number?) -> { Hit },
    query: (source: string, limit: number?, offset: number?) -> QueryResult,
    attachments: () -> { string },
    revision: () -> number,
    checkExtension: (name: string, text: string) -> { [string]: any },

    -- writing
    write: (name: string, content: string) -> boolean,
    appendToJournal: (date: string, markdown: string) -> (),
    appendToPage: (name: string, markdown: string) -> (),
    replaceBlock: (ref: string, text: string, task: string?) -> string,
    insertBlock: (ref: string, markdown: string, where: string?) -> string,
    removeBlock: (ref: string) -> (),
    setTask: (ref: string, status: string) -> string,
    setProperty: (ref: string, key: string, value: (string | number)?) -> string,
    setProperties: (ref: string, props: { { any } }) -> string,
    replaceLine: (ref: string, text: string) -> string,
    insertLine: (ref: string, text: string, where: string?) -> string,
    removeLine: (ref: string) -> (),

    -- helpers
    today: () -> string,
    now: () -> string,
    toUtc: (datetime: string) -> string,
    fromUtc: (iso: string) -> string?,
    daysBetween: (a: string, b: string) -> number,
    addDays: (date: string, n: number?) -> string,
    addMonths: (date: string, n: number?) -> string,
    weekday: (date: string) -> number,
    weekStart: () -> number,
    weekdayName: (n: number?, form: string?) -> string,
    monthName: (n: number?, form: string?) -> string,
    sorted: (list: { string }) -> { string },
    fold: (text: string) -> string,
    lower: (text: string) -> string,
    plain: (text: string) -> string,
    parseRecurrence: (text: string) -> Rule?,
    parseWeekdays: (text: string) -> { number }?,
    formatRecurrence: (rule: Rule, lang: string?) -> string,
    formatNumber: (n: number?, decimals: number?) -> string,
    lang: () -> string,
    uiLang: () -> string,
    log: (text: string) -> (),
    status: (text: string?) -> (),
    timer: (seconds: number?, options: (string | { label: string?, message: string? })?) -> (),
    stopTimer: () -> (),
    data: (name: string) -> any,

    -- questions and AI (in commands and actions)
    ask: (prompt: string) -> string,
    choose: (prompt: string, options: { any }) -> string?,
    form: (title: string, fields: { { [string]: any } }) -> { [string]: any }?,
    ai: (request: string | { [string]: any }) -> (string, AiReply),

    -- the views' components
    ui: {
        column: (children: { any }) -> Node,
        row: (children: { any }) -> Node,
        card: (children: { any }) -> Node,
        heading: (text: string) -> Node,
        text: (text: string, options: any?) -> Node,
        link: (page: string, label: string?, line: number?) -> Node,
        button: (label: string, action: string, args: { [string]: any }?, options: { [string]: any }?) -> Node,
        markdown: (text: string, options: { [string]: any }?) -> Node,
        -- b.ui.progress(3, 10, "3 of 10") or b.ui.progress({ value = 3, max = 10, label = …, tone = … })
        progress: (value: (number | { [string]: any })?, max: number?, label: string?) -> Node,
        stats: (items: { any }) -> Node,
        divider: () -> Node,
        menu: (items: { any }, label: string?) -> Node,
        check: (checked: any, action: string, args: { [string]: any }?, options: { [string]: any }?) -> Node,
        stepper: (spec: { [string]: any }) -> Node,
        table: (columns: { string }, rows: { any }) -> Node,
        chart: (spec: { [string]: any }) -> Node,
        month: (spec: { [string]: any }) -> Node,
        empty: (title: string, text: string, icon: string?) -> Node,
        open: (label: string, view: string, params: { [string]: any }?, options: { [string]: any }?) -> Node,
        run: (label: string, command: string) -> Node,
        input: (spec: { [string]: any }) -> Node,
        select: (spec: { [string]: any }) -> Node,
        slider: (spec: { [string]: any }) -> Node,
        date: (spec: { [string]: any }) -> Node,
        toggle: (on: any, action: string, args: { [string]: any }?, options: { [string]: any }?) -> Node,
    },
}
