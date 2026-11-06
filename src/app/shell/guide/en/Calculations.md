---
tags: [documentation]
brulion: document
---
# Calculations

Brulion has its own calculator that computes right in your notes. The file keeps only what you typed; results are calculated and shown next to it as you go, in the format of the app's language (in English `42.5`, in Polish `42,5`).

## `calc` blocks
A block with the language `calc` computes line by line, with shared variables:

```calc
price = 12.50
quantity = 4
price * quantity
discount = 15%
ans - discount
5 km to mi
20% of 150
```

- `name = expression` creates a variable (names may contain accented letters), `f(x) = x^2` — a function.
- `ans` is the previous result.
- Percentages: `x + 10%`, `x - 10%`, `20% of x`, `x as % of y`.
- Units: `5 km`, `3 h + 20 min`, `100 km / 2 h`, `20°C to °F`; convert with the word `to` or `in`.
- Currencies are labels that carry through calculations: `$12 + $5`, `€10`, `12 zł`. Brulion never converts between different currencies.
- `#` and `//` start a comment. Text before a colon is a label: `Rent: $1200`.
- Write decimals with a point: `12.50`. A comma between digits followed by exactly three digits groups thousands (`1,250`); otherwise it works as a decimal comma (`12,50` is 12.5). In a function call, separate the arguments with a semicolon or with a comma and a space: `round(2.345; 2)`, `max(1.5, 2)`. A comma between digits without a space separates the arguments (`pow(2,5)` is 2⁵), unless the built-in function would then get too many arguments — then it is a decimal comma (`sqrt(2,25)` is 1.5). When both readings fit, as in `round(2,5)` or `round(net * 1,23)`, Brulion shows an error instead of guessing: add a semicolon or a space, or use a point.
- Functions: `sum`, `avg`/`average`, `min`, `max`, `count`, `median`, `round`, `floor`, `ceil`, `abs`, `sqrt`, `pow`, `exp`, `ln`, `log`, trigonometry, `if`. The Polish names (`suma`, `średnia`, `zaokr`, `jeśli`) and the Polish words `z`, `od`, `jako`, `na`, `w` work too.

### Totals
The word `total` (or `sum`) alone on a line adds up the results above it, up to an empty line, and `average` averages them:

```calc
Rent: $1200
Electricity: $180
Internet: $60
total
```

## Inline calculations
Inline code that starts with `=` shows a result. A year has `= 52 * 7` days in full weeks. Variables from earlier `calc` blocks on the same page are available, and so are the page's numeric properties.

## Tables with formulas
A table cell that starts with `=` is a formula:

| Product | Price | Quantity | Total |
|---|---:|---:|---:|
| Notebook | 8.50 | 3 | =[Price]*[Quantity] |
| Pen | 3 | 10 | =[Price]*[Quantity] |
| Folder | 12 | 2 | =[Price]*[Quantity] |
| **Total** | | | =sum(above) |

References:

| Notation | Meaning |
|---|---|
| `[Price]` | the cell in the same row, in the column with the header “Price” |
| `A1`, `C3` | a cell: the column letter and the number of the data row (1 = the first below the header) |
| `A1:A5`, `B:B` | a range, a whole column |
| `above`, `below`, `left`, `right` | all numbers above, below, to the left, to the right |

Ordinary cells are read the way people write them: `$12.50`, `23%`. An error in a formula shows as `#ERR` (hover over it for the reason), and a loop as `#CYCLE`. You edit the table like a spreadsheet, and Brulion writes it back as Markdown.

You change rows and columns in the cell menu: right-click the cell (on a phone, tap the cell and choose **More** in the toolbar above the keyboard). Under **Row** and **Column** you can insert a new one, move it or delete it, and set a column's alignment. Shortcuts work in a cell too: **Ctrl+Enter** inserts a row below, **Alt+↑/↓** moves the row, and **Alt+←/→** the column. You undo every change with **Ctrl+Z**.

Formulas keep pointing at the same cells: after you insert a row above the cell `B2`, the reference in a formula changes to `B3`. If you delete a row or column a formula refers to, `REF!` appears in its text, and `#ERR` in the cell.

You sort and filter a table from the column menu, exchange it with a spreadsheet via CSV, and show its numbers in a chart — see [[Tables and charts|Tables and charts]].

Insert a table or a calculation block with the `/Table` and `/Calculation` commands. See also [[Tags and properties|properties]], whose values you can calculate with.

Math formulas are something else than calculations: Brulion typesets them in your notes but doesn't compute their value — see [[Math formulas|Math formulas]].
