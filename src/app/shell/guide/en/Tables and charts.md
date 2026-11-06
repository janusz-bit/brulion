---
tags: [documentation]
aliases: [Tables, Charts, CSV, Sorting tables]
brulion: document
---
# Tables and charts

A table in a note is an ordinary Markdown table that Brulion shows like a spreadsheet: with formulas (see [[Calculations#Tables with formulas|Calculations]]), sorting, filters, exchange with spreadsheets via CSV, and charts. The file keeps only text, readable in any app.

## Sorting
Right-click a cell or a column header (on a phone, tap the cell and choose **More**), then **Column → Sort ascending** or **Sort descending**. Try it on this table:

| Fruit | Price per kg | Picked |
|---|--:|---|
| Plums | $7.50 | 09/15/2026 |
| Pears | $9 | September 3, 2026 |
| Cranberries | $24 | 2026-10-02 |
| Gooseberries | $12.80 | 2026-07-10 |
| Apricots | $5 | |
| **Total** | =sum(above) | |

- Numbers are ordered by value (`$9` before `$12.80`), dates by day — also ones written in words — and text alphabetically, with numbers in their natural order (`Room 2` before `Room 10`) and Polish letters in Polish order (`Ł` after `L`, `Ż` at the end).
- Empty cells always go last. Rows with the same value keep their previous order.
- A summary row (like **Total**, whose formula sums up the column above) stays at the bottom.
- Sorting rearranges the rows in the file — every row stays exactly as it was written. You undo it with **Ctrl+Z**.

## Filtering
**Filter rows** in the table menu shows a field below the header in every column. Rows that don't match disappear from view — nothing changes in the file, and the filter applies as long as the page is open. A column with a filter has a funnel in its header, and below the table you see how many rows are shown, plus a **Clear filters** button.

| Type | Shows |
|---|---|
| `berr` | cells containing “berr” (ignoring letter case and accents) |
| `!berr` | cells without “berr” |
| `> 10`, `<= 7.50`, `= 9` | numbers greater, smaller or equal |
| `>= 2026-09-01` | dates from September 1, 2026 on |
| `=` / `!=` | empty cells / cells with content |

The summary row always stays visible and counts all rows, hidden ones included.

## CSV: spreadsheets
- **Copy as CSV** in the table menu copies it to the clipboard, and **Save as CSV…** saves it to a file that you open in Excel, LibreOffice or Google Sheets. Formulas go into the file as their results.
- `/Table from CSV…` inserts a table from a CSV file. Brulion recognizes by itself whether the values are separated by commas, semicolons or tabs, and copes with quotes, accented letters (also in files from older Excel versions) and line breaks inside cells. Values come in as text — even those that were formulas in the spreadsheet.

## Charts
A `chart` block draws a chart from the numbers on the page. The easiest way is to type `/Chart` below a table:

| Month | Revenue | Cost |
|---|--:|--:|
| January | 4200 | 3100 |
| February | 3900 | =B1-500 |
| March | 5100 | 3300 |
| Total | =sum(above) | =sum(above) |

```chart
type: bar
title: First quarter
```

The chart takes the table right above it: labels from the first column, values from the columns with numbers (without the summary row), and formulas as their results. Change a number in the table and the chart changes right away. Click the chart to edit its description.

| Key | What it means |
|---|---|
| `type:` | `bar`, `line` or `pie` |
| `table:` | `above` (the default) or the number of the table on the page, e.g. `2` |
| `calc:` | data from a `calc` block: `above` or its number |
| `query:` | data from a [[Queries|query]]: the number of results in each group, e.g. `query: type:tasks tag:work group:status` |
| `labels:` | the column with the labels (header name or letter) |
| `values:` | the columns with the values, separated by commas: `Revenue, Cost` |
| `title:` | the title above the chart |

The Polish keys and values work too (`typ: słupkowy`, `tabela: powyżej`…).

A pie chart of expenses from a calculation block:

```calc
Rent: 1800
Food: 1200
Transport: 350
```

```chart
type: pie
calc: above
```

Brulion draws charts by itself, in the colors of the theme — light and dark — without the internet and without running anything from the note. A screen reader reads out the chart's type, title and all values. In HTML export a chart is a picture, and below it you can unfold its values and source. When something doesn't fit (there is no table, no column with that name or no numbers), Brulion shows the chart's description and, below it, what is missing.

Flowcharts, sequence diagrams and pie charts written on their own, without a table, are described on the [[Diagrams|Diagrams]] page.
