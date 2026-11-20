---
tags: [rozszerzenie]
opis: Funkcje, których używa kilka rozszerzeń tego notatnika — każde bierze je przez b.require("Wspólne funkcje").
---
# Wspólne funkcje

Ta strona nie dodaje poleceń ani widoków: jej kod kończy się przez `return M` — tabelę funkcji —, a
rozszerzenia, które ich potrzebują, biorą ją przez `b.require("Wspólne funkcje")`, zamiast nosić
każde własną kopię. Każde rozszerzenie dostaje własny egzemplarz tych funkcji i wykonuje je ze
swoimi uprawnieniami: ta strona sama nie potrzebuje żadnych. Gdy się zmieni, rozszerzenia, które
jej używają, wczytują się od nowa; zmieniona gdzie indziej czeka na zatwierdzenie jak każdy kod — a
one razem z nią.

Używają jej: [[Przegląd zadań]], [[Indeks projektów]], [[Licznik słów]], [[Fiszki]], [[Wydatki]],
[[Pomodoro]], [[Quick entries]].

## Tekst

```luau
local M = {}

-- Bez spacji na początku i na końcu.
function M.trim(s: string): string
    return (s:match("^%s*(.-)%s*$"))
end

-- "1 słowo", "3 słowa", "5 słów", "22 słowa", "12 słów": polska odmiana po liczbie.
function M.plural(n: number, one: string, few: string, many: string): string
    local last, lastTwo = n % 10, n % 100
    local word = many
    if n == 1 then
        word = one
    elseif last >= 2 and last <= 4 and (lastTwo < 12 or lastTwo > 14) then
        word = few
    end
    return `{n} {word}`
end

-- Pierwsza litera wielka, także polska.
local UPPER = { ["ą"] = "Ą", ["ć"] = "Ć", ["ę"] = "Ę", ["ł"] = "Ł", ["ń"] = "Ń", ["ó"] = "Ó", ["ś"] = "Ś", ["ź"] = "Ź", ["ż"] = "Ż" }
function M.capitalized(s: string): string
    local stop = utf8.offset(s, 2) or #s + 1
    local first = s:sub(1, stop - 1)
    return (UPPER[first] or first:upper()) .. s:sub(stop)
end
```

## Zadania i zapytania

```luau
-- Pola dat zadania (Obsidian Tasks): bez nich zostaje to, co czyta człowiek.
local FIELDS = { "📅", "⏳", "🛫", "➕", "✅", "❌" }

-- Tekst zadania bez jego dat ("📅 2026-11-14").
function M.withoutDates(text: string): string
    for _, field in FIELDS do
        text = text:gsub("%s*" .. field .. " %d%d%d%d%-%d%d%-%d%d", "")
    end
    return text
end

-- Nazwa strony w zapytaniu: w cudzysłowie, który zna język zapytań (nazwa może mieć spacje).
function M.quoted(name: string): string
    return if name:find('"', 1, true) then `„{name}”` else `"{name}"`
end

return M
```
