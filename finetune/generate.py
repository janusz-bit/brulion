#!/usr/bin/env python3
"""Made-up notebooks and the assistant's tasks on them (docs/FINETUNE.md "Generated tasks").

Writes notebooks as finetune/notebooks holds them and task files as finetune/tasks does, for
brulion-assistantbench:

    python3 -I finetune/generate.py --out ~/.cache/brulion-finetune/gen --split train --count 20 --seed 1
    build/tests/assistantbench/brulion-assistantbench --solution \
        --tasks ~/.cache/brulion-finetune/gen/tasks --notebooks ~/.cache/brulion-finetune/gen/notebooks

Every notebook is made of facts drawn from the pools below (people, chores, recipes, journal
entries...), so each task knows its outcome: the facts its answer must name, the note and line
it must change. The same seed makes the same notebook. Half the notebooks are Polish, half
English. The split "test" asks in words of its own: of a task's ways of asking (three or more),
the last one is kept for it — training never sees it.

Only the standard library: the output is the same wherever it runs.
"""

import argparse
import datetime as dt
import json
import os
import random
import re
import sys
import unicodedata

# ---- Words of the two languages ---------------------------------------------------------------

PL_MONTHS_GEN = ['stycznia', 'lutego', 'marca', 'kwietnia', 'maja', 'czerwca', 'lipca', 'sierpnia',
                 'września', 'października', 'listopada', 'grudnia']
EN_MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September',
             'October', 'November', 'December']
PL_ON_WEEKDAY = ['w poniedziałek', 'we wtorek', 'w środę', 'w czwartek', 'w piątek', 'w sobotę',
                 'w niedzielę']
PL_EVERY_WEEKDAY = ['w poniedziałki', 'we wtorki', 'w środy', 'w czwartki', 'w piątki', 'w soboty', 'w niedziele']
PL_FOR_WEEKDAY = ['na poniedziałek', 'na wtorek', 'na środę', 'na czwartek', 'na piątek', 'na sobotę',
                  'na niedzielę']
EN_WEEKDAYS = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']

# What an answer says when the notes do not have it ("I searched … but found no matching entries",
# "it doesn't look like you've saved one"): any of these, compared folded.
PL_NOT_FOUND = ['nie znalaz', 'nie ma ', 'nie ma.', 'brak', 'nie widzę', 'nie widze', 'nie mam informacji',
                'nie udało', 'nie mogę znaleźć', 'nie moge znalezc', 'nie zawiera', 'nie występuje',
                'nie posiadam', 'nie wiem', 'nie zapisa', 'nie jest zapisan', 'nie pojawia', 'nie natrafi',
                'nie odnalaz', 'nie znajduj', 'żadn', 'nie wygląda', 'nie widać', 'nie wspomina', 'nigdzie']
EN_NOT_FOUND = ["couldn't find", 'could not find', "can't find", 'cannot find', "didn't find", 'did not find',
                'no information', 'not in your notes', "isn't in your notes", 'no mention', 'nothing about',
                "don't see", 'do not see', 'not found', "doesn't appear", 'does not appear', "isn't recorded",
                'not recorded', "don't have", 'do not have', 'no record', 'found no', 'found nothing',
                'nothing was found', 'nothing came up', 'nothing matching', 'no matching', 'none of',
                "doesn't look like", 'does not look like', "doesn't seem", 'does not seem', "haven't saved",
                'have not saved', 'not saved', "haven't recorded", 'have not recorded', 'no entry', 'no entries',
                'no saved', 'no note', 'not mentioned', "isn't mentioned", 'nowhere']
PL_ASK_BACK = ['?', 'podaj', 'napisz, co', 'doprecyz', 'potrzebuję wiedzieć', 'powiedz', 'co dokładnie',
               'o które', 'o co chodzi', 'które zadanie', 'którą', 'który', 'sprecyz']
EN_ASK_BACK = ['?', 'please specify', 'please tell', 'let me know which', 'let me know what', 'could you clarify',
               'which task', 'which item', 'which one', 'what would you like', 'can you tell me']

# People: (first name, surname) by gender; Polish surnames in both forms.
PL_MEN = ['Marek', 'Tomasz', 'Paweł', 'Krzysztof', 'Piotr', 'Andrzej', 'Michał', 'Grzegorz', 'Jakub', 'Łukasz',
          'Wojciech', 'Rafał']
PL_WOMEN = ['Anna', 'Katarzyna', 'Magdalena', 'Agnieszka', 'Joanna', 'Ewa', 'Monika', 'Barbara', 'Zofia', 'Alicja',
            'Natalia', 'Dorota']
PL_SURNAMES = [('Wiśniewski', 'Wiśniewska'), ('Kowalczyk', 'Kowalczyk'), ('Zieliński', 'Zielińska'),
               ('Wójcik', 'Wójcik'), ('Kamiński', 'Kamińska'), ('Lewandowski', 'Lewandowska'), ('Dąbrowski', 'Dąbrowska'),
               ('Mazur', 'Mazur'), ('Krawczyk', 'Krawczyk'), ('Piotrowski', 'Piotrowska'), ('Grabowski', 'Grabowska'),
               ('Nowakowski', 'Nowakowska'), ('Pawlak', 'Pawlak'), ('Michalski', 'Michalska'), ('Sikora', 'Sikora')]
EN_MEN = ['Mark', 'Tom', 'James', 'David', 'Chris', 'Daniel', 'Peter', 'Sam', 'Oliver', 'Ben', 'Jack', 'Ryan']
EN_WOMEN = ['Anna', 'Kate', 'Emma', 'Sarah', 'Laura', 'Rachel', 'Helen', 'Sophie', 'Lucy', 'Grace', 'Megan', 'Claire']
EN_SURNAMES = ['Smith', 'Taylor', 'Brown', 'Wilson', 'Evans', 'Walker', 'Wright', 'Robinson', 'Thompson', 'Hughes',
               'Edwards', 'Green', 'Hall', 'Clarke', 'Turner']

# Contacts: Polish label (the person's gender: m / f), its genitive ("numer do …"), English.
ROLES = [
    ('Hydraulik', 'm', 'hydraulika', 'plumber'),
    ('Elektryk', 'm', 'elektryka', 'electrician'),
    ('Mechanik', 'm', 'mechanika', 'mechanic'),
    ('Weterynarz', 'm', 'weterynarza', 'vet'),
    ('Dentystka', 'f', 'dentystki', 'dentist'),
    ('Fryzjerka', 'f', 'fryzjerki', 'hairdresser'),
    ('Niania', 'f', 'niani', 'babysitter'),
    ('Księgowa', 'f', 'księgowej', 'accountant'),
    ('Ślusarz', 'm', 'ślusarza', 'locksmith'),
    ('Kominiarz', 'm', 'kominiarza', 'chimney sweep'),
    ('Ogrodnik', 'm', 'ogrodnika', 'gardener'),
    ('Korepetytorka', 'f', 'korepetytorki', 'tutor'),
    ('Krawcowa', 'f', 'krawcowej', 'tailor'),
    ('Masażystka', 'f', 'masażystki', 'massage therapist'),
]

PL_CALL_HOURS = [('dzwonić po 16:00', '16'), ('najlepiej rano, przed 9:00', '9'), ('dzwonić między 12 a 14', '12'),
                 ('nie odbiera po 18:00', '18')]
EN_CALL_HOURS = [('call after 4 pm', '4'), ('best before 9 am', '9'), ('call between 12 and 2 pm', '12'),
                 ('never after 6 pm', '6')]

# Chores and work: (Polish, its keys, English, its keys) — a key is a part of the words the answer
# names it by (compared folded); the first one stands for it in searches.
CHORES = [
    ('Zapłacić za prąd', ['prąd', 'prad'], 'Pay the electricity bill', ['electric']),
    ('Wymienić filtr w okapie', ['filtr'], 'Replace the cooker hood filter', ['filter']),
    ('Umyć okna', ['okna', 'okien'], 'Clean the windows', ['window']),
    ('Odebrać paczkę z paczkomatu', ['paczk'], 'Collect the parcel from the post office', ['parcel']),
    ('Zawieźć kurtkę do pralni', ['kurtk', 'pralni'], 'Take the coat to the dry cleaner', ['coat']),
    ('Kupić prezent dla mamy', ['prezent'], 'Buy a present for Mum', ['present']),
    ('Zarezerwować stolik w restauracji', ['stolik'], 'Book a table at the restaurant', ['table']),
    ('Oddać książki do biblioteki', ['bibliotek'], 'Return the books to the library', ['library']),
    ('Naprawić kran w łazience', ['kran'], 'Fix the bathroom tap', ['tap']),
    ('Zamówić drewno do kominka', ['drewn'], 'Order firewood for the fireplace', ['firewood']),
    ('Zapisać się na basen', ['basen'], 'Sign up for the swimming pool', ['swimming', 'pool']),
    ('Przedłużyć umowę na telefon', ['umow', 'telefon'], 'Renew the phone contract', ['phone contract']),
    ('Wysłać kartkę do babci', ['kartk', 'babci'], 'Send Grandma a card', ['grandma']),
    ('Posadzić tulipany', ['tulipan'], 'Plant the tulips', ['tulip']),
    ('Wywołać zdjęcia z wakacji', ['zdjęci'], 'Print the holiday photos', ['photo']),
    ('Zapłacić rachunek za internet', ['internet'], 'Pay the internet bill', ['internet']),
    ('Umówić przegląd pieca', ['piec'], 'Book the boiler service', ['boiler']),
    ('Wymienić żarówkę w przedpokoju', ['żarówk', 'zarowk'], 'Change the hallway light bulb', ['bulb']),
    ('Odnowić paszport', ['paszport'], 'Renew the passport', ['passport']),
    ('Oddać rower do serwisu', ['rower'], 'Take the bike in for a service', ['bike']),
    ('Wyczyścić rynny', ['rynn'], 'Clean the gutters', ['gutter']),
    ('Pomalować płot', ['płot', 'plot'], 'Paint the fence', ['fence']),
]
WORK = [
    ('Wysłać ofertę do klienta', ['ofert'], 'Send the quote to the client', ['quote']),
    ('Przygotować prezentację dla zarządu', ['prezentacj'], 'Prepare the board presentation', ['presentation']),
    ('Napisać raport kwartalny', ['raport'], 'Write the quarterly report', ['report']),
    ('Zaktualizować umowę z dostawcą', ['dostawc'], 'Update the supplier contract', ['supplier']),
    ('Zamówić nowe laptopy', ['laptop'], 'Order the new laptops', ['laptop']),
    ('Zaplanować szkolenie z bezpieczeństwa', ['szkoleni'], 'Plan the safety training', ['training']),
    ('Rozliczyć delegację', ['delegacj'], 'Submit the travel expenses', ['expenses']),
    ('Przygotować budżet na przyszły rok', ['budżet', 'budzet'], "Draft next year's budget", ['budget']),
    ('Poprawić błędy w aplikacji mobilnej', ['błęd', 'bled', 'aplikacj'], 'Fix the bugs in the mobile app', ['bug']),
    ('Zorganizować spotkanie zespołu', ['spotkani'], 'Organise the team meeting', ['team meeting']),
    ('Uzupełnić dokumentację API', ['dokumentacj'], 'Update the API documentation', ['documentation']),
    ('Przejrzeć CV kandydatów', ['cv', 'kandydat'], "Review the candidates' CVs", ['cv', 'candidate']),
    ('Odnowić licencje programu', ['licencj'], 'Renew the software licences', ['licence', 'license']),
    ('Wysłać fakturę do klienta', ['faktur'], 'Send the invoice to the client', ['invoice']),
    ('Przygotować ankietę dla klientów', ['ankiet'], 'Prepare the customer survey', ['survey']),
    ('Zarchiwizować stare projekty', ['archiw'], 'Archive the old projects', ['archive']),
]
PL_PROJECTS = ['Remont kuchni', 'Przeprowadzka', 'Nowa strona sklepu', 'Wesele Oli', 'Aplikacja dla klubu']
EN_PROJECTS = ['Kitchen renovation', 'Moving house', 'New shop website', "Ola's wedding", 'Club app']
PL_TAGS = ['dom', 'rachunki', 'pilne', 'auto', 'zdrowie', 'ogród']
EN_TAGS = ['home', 'bills', 'urgent', 'car', 'health', 'garden']
PL_WORK_TAGS = ['praca', 'klient', 'pilne']
EN_WORK_TAGS = ['work', 'client', 'urgent']

# Shopping: Polish (its accusative), keys; English, keys.
SHOPPING = [
    ('mleko', 'mleko', ['mlek'], 'milk', ['milk']),
    ('chleb', 'chleb', ['chleb'], 'bread', ['bread']),
    ('masło', 'masło', ['masł', 'masl'], 'butter', ['butter']),
    ('jajka', 'jajka', ['jaj'], 'eggs', ['egg']),
    ('pomidory', 'pomidory', ['pomidor'], 'tomatoes', ['tomato']),
    ('kawa', 'kawę', ['kaw'], 'coffee', ['coffee']),
    ('herbata', 'herbatę', ['herbat'], 'tea', ['tea']),
    ('ryż', 'ryż', ['ryż', 'ryz'], 'rice', ['rice']),
    ('jabłka', 'jabłka', ['jabł', 'jabl'], 'apples', ['apple']),
    ('banany', 'banany', ['banan'], 'bananas', ['banana']),
    ('papier toaletowy', 'papier toaletowy', ['papier'], 'toilet paper', ['toilet']),
    ('płyn do naczyń', 'płyn do naczyń', ['płyn', 'plyn'], 'washing-up liquid', ['washing-up']),
    ('cebula', 'cebulę', ['cebul'], 'onions', ['onion']),
    ('ziemniaki', 'ziemniaki', ['ziemniak'], 'potatoes', ['potato']),
    ('szynka', 'szynkę', ['szynk'], 'ham', ['ham']),
    ('sok pomarańczowy', 'sok pomarańczowy', ['pomarańcz', 'pomarancz'], 'orange juice', ['juice']),
    ('marchew', 'marchew', ['marchew'], 'carrots', ['carrot']),
    ('ogórki', 'ogórki', ['ogórk', 'ogork'], 'cucumbers', ['cucumber']),
    ('jogurt naturalny', 'jogurt naturalny', ['jogurt'], 'yoghurt', ['yoghurt', 'yogurt']),
    ('oliwa', 'oliwę', ['oliw'], 'olive oil', ['olive']),
    ('proszek do prania', 'proszek do prania', ['proszek', 'proszku'], 'washing powder', ['powder']),
    ('czekolada', 'czekoladę', ['czekolad'], 'chocolate', ['chocolate']),
]

# Recipes: name, its genitive ("do …") and accusative ("na …", "robię …"), the people it serves,
# ingredients (amount, unit, the ingredient, its genitive for "how much …", keys), steps.
PL_RECIPES = [
    ('Naleśniki', 'naleśników', 'naleśniki', 4,
     [(250, 'g', 'mąki pszennej', 'mąki', ['mąk', 'mak']), (500, 'ml', 'mleka', 'mleka', ['mlek']),
      (2, '', 'jajka', 'jajek', ['jaj']), (30, 'g', 'masła', 'masła', ['masł', 'masl'])],
     ['Zmiksować mąkę, mleko i jajka.', 'Odstawić ciasto na 15 minut.', 'Smażyć cienkie placki na maśle.']),
    ('Placki ziemniaczane', 'placków ziemniaczanych', 'placki ziemniaczane', 4,
     [(1000, 'g', 'ziemniaków', 'ziemniaków', ['ziemniak']), (1, '', 'cebula', 'cebuli', ['cebul']),
      (2, '', 'jajka', 'jajek', ['jaj']), (60, 'g', 'mąki', 'mąki', ['mąk', 'mak'])],
     ['Zetrzeć ziemniaki i cebulę.', 'Dodać jajka i mąkę, doprawić.', 'Smażyć na rozgrzanym oleju.']),
    ('Zupa pomidorowa', 'zupy pomidorowej', 'zupę pomidorową', 6,
     [(1500, 'ml', 'bulionu', 'bulionu', ['bulion']), (600, 'g', 'pomidorów', 'pomidorów', ['pomidor']),
      (150, 'g', 'ryżu', 'ryżu', ['ryż', 'ryz']), (200, 'ml', 'śmietany', 'śmietany', ['śmietan', 'smietan'])],
     ['Zagotować bulion.', 'Dodać pomidory i gotować 20 minut.', 'Zmiksować, dodać śmietanę i ryż.']),
    ('Sernik', 'sernika', 'sernik', 8,
     [(1000, 'g', 'twarogu', 'twarogu', ['twaróg', 'twarog']), (5, '', 'jajek', 'jajek', ['jaj']),
      (200, 'g', 'cukru', 'cukru', ['cukr', 'cukier']), (100, 'g', 'masła', 'masła', ['masł', 'masl'])],
     ['Utrzeć masło z cukrem.', 'Dodać twaróg i jajka.', 'Piec godzinę w 170°C.']),
    ('Szarlotka', 'szarlotki', 'szarlotkę', 8,
     [(1500, 'g', 'jabłek', 'jabłek', ['jabł', 'jabl']), (300, 'g', 'mąki', 'mąki', ['mąk', 'mak']),
      (200, 'g', 'masła', 'masła', ['masł', 'masl']), (100, 'g', 'cukru', 'cukru', ['cukr', 'cukier'])],
     ['Zagnieść kruche ciasto.', 'Wyłożyć jabłka na spód.', 'Przykryć ciastem i piec 50 minut.']),
    ('Gołąbki', 'gołąbków', 'gołąbki', 6,
     [(1, '', 'główka kapusty', 'kapusty', ['kapust']), (500, 'g', 'mięsa mielonego', 'mięsa', ['mięs', 'mies']),
      (150, 'g', 'ryżu', 'ryżu', ['ryż', 'ryz']), (500, 'ml', 'sosu pomidorowego', 'sosu', ['sos'])],
     ['Sparzyć liście kapusty.', 'Wymieszać mięso z ryżem.', 'Zawijać farsz i dusić w sosie 1 godzinę.']),
]
EN_RECIPES = [
    ('Pancakes', 'pancakes', 'pancakes', 4,
     [(200, 'g', 'plain flour', 'flour', ['flour']), (300, 'ml', 'milk', 'milk', ['milk']),
      (2, '', 'eggs', 'eggs', ['egg']), (25, 'g', 'butter', 'butter', ['butter'])],
     ['Whisk the flour, milk and eggs.', 'Rest the batter for 15 minutes.', 'Fry thin pancakes in butter.']),
    ('Banana bread', 'banana bread', 'banana bread', 8,
     [(3, '', 'ripe bananas', 'bananas', ['banana']), (250, 'g', 'self-raising flour', 'flour', ['flour']),
      (100, 'g', 'brown sugar', 'sugar', ['sugar']), (2, '', 'eggs', 'eggs', ['egg'])],
     ['Mash the bananas.', 'Mix in the eggs, sugar and flour.', 'Bake for an hour at 180°C.']),
    ('Tomato soup', 'tomato soup', 'tomato soup', 6,
     [(1200, 'ml', 'vegetable stock', 'stock', ['stock']), (800, 'g', 'tomatoes', 'tomatoes', ['tomato']),
      (2, '', 'onions', 'onions', ['onion']), (150, 'ml', 'cream', 'cream', ['cream'])],
     ['Soften the onions.', 'Add the tomatoes and stock, simmer for 20 minutes.', 'Blend and stir in the cream.']),
    ('Apple crumble', 'apple crumble', 'apple crumble', 6,
     [(1200, 'g', 'apples', 'apples', ['apple']), (150, 'g', 'plain flour', 'flour', ['flour']),
      (100, 'g', 'butter', 'butter', ['butter']), (90, 'g', 'sugar', 'sugar', ['sugar'])],
     ['Slice the apples into a dish.', 'Rub the butter into the flour and sugar.', 'Bake for 40 minutes.']),
    ('Chicken curry', 'chicken curry', 'chicken curry', 4,
     [(600, 'g', 'chicken thighs', 'chicken', ['chicken']), (400, 'ml', 'coconut milk', 'coconut milk', ['coconut']),
      (2, '', 'onions', 'onions', ['onion']), (200, 'g', 'rice', 'rice', ['rice'])],
     ['Brown the chicken.', 'Add the onions and curry paste.', 'Pour in the coconut milk and simmer for 25 minutes.']),
    ('Shepherd\'s pie', 'shepherd\'s pie', 'shepherd\'s pie', 6,
     [(750, 'g', 'minced lamb', 'lamb', ['lamb']), (1200, 'g', 'potatoes', 'potatoes', ['potato']),
      (2, '', 'carrots', 'carrots', ['carrot']), (300, 'ml', 'stock', 'stock', ['stock'])],
     ['Brown the lamb with the carrots.', 'Add the stock and simmer.', 'Top with mashed potatoes and bake.']),
]

# Books: title, author (real ones — public facts).
PL_BOOKS = [('Lalka', 'Bolesław Prus'), ('Solaris', 'Stanisław Lem'), ('Pan Tadeusz', 'Adam Mickiewicz'),
            ('Ferdydurke', 'Witold Gombrowicz'), ('Chłopi', 'Władysław Reymont'), ('Quo vadis', 'Henryk Sienkiewicz'),
            ('Bieguni', 'Olga Tokarczuk'), ('Cesarz', 'Ryszard Kapuściński'), ('Zły', 'Leopold Tyrmand'),
            ('Wiedźmin', 'Andrzej Sapkowski')]
EN_BOOKS = [('Atomic Habits', 'James Clear'), ('Pride and Prejudice', 'Jane Austen'), ('Dune', 'Frank Herbert'),
            ('The Hobbit', 'J. R. R. Tolkien'), ('Sapiens', 'Yuval Noah Harari'), ('Middlemarch', 'George Eliot'),
            ('Educated', 'Tara Westover'), ('The Road', 'Cormac McCarthy'), ('Rebecca', 'Daphne du Maurier'),
            ('Deep Work', 'Cal Newport')]
PL_FILMS = ['Rejs', 'Seksmisja', 'Miś', 'Ida', 'Boże Ciało', 'Zimna wojna', 'Dzień świra']
EN_FILMS = ['Paddington', 'Notting Hill', 'Amélie', 'The Grand Budapest Hotel', 'Arrival', 'Up', 'Inception']

PL_SHOPS = ['Biedronce', 'Lidlu', 'Żabce', 'Rossmannie', 'Auchanie', 'Carrefourze']
EN_SHOPS = ['Tesco', "Sainsbury's", 'Aldi', 'Lidl', 'Boots', 'Waitrose']
PL_CARS = ['Skoda Fabia', 'Toyota Yaris', 'Opel Astra', 'Kia Ceed', 'Dacia Duster', 'Ford Focus']
EN_CARS = ['Ford Fiesta', 'Vauxhall Corsa', 'VW Golf', 'Toyota Corolla', 'Nissan Qashqai', 'Honda Jazz']
PL_INSURERS = ['PZU', 'Warta', 'Allianz', 'Link4', 'Generali']
EN_INSURERS = ['Aviva', 'Admiral', 'Direct Line', 'LV=', 'Churchill']
WIFI_WORDS = ['zielonyKot', 'sowaNaDrzewie', 'blueHarbor', 'tygrys', 'mapleLeaf', 'koala', 'jasnyDzien', 'oceanView']

# Facts asked for in other words than the note's (training only): the user's words — what a first
# search looks for and finds nothing — for a fact of the home page, the car, a contact by its role.
# Never the test's own ("flat", "kosze", "drzwi na dole"): the test measures whether a retry is
# learnt, not these words.
GAPS = {
    'pl': {
        'domofon': [('Jaki jest szyfr do bramy?', 'szyfr bramy'), ('Podaj mi szyfr, którym otwiera się bramę.', 'szyfr')],
        'wifi': [('Jaki jest klucz do routera?', 'router'), ('Jak się nazywa klucz do sieci w routerze?', 'klucz router')],
        'czynsz': [('Ile co miesiąc płacę za mieszkanie?', 'mieszkanie'), ('Ile wynosi opłata za lokal?', 'opłata lokal')],
        'smieci': [('Kiedy przyjeżdża śmieciarka?', 'śmieciarka'), ('Którego dnia odbierają odpady?', 'odpady')],
        'przeglad': [('Do kiedy jest ważne badanie auta?', 'badanie')],
        'Dentystka': [('Jaki jest numer do stomatologa?', 'stomatolog')],
        'Niania': [('Podaj numer do opiekunki do dzieci.', 'opiekunka')],
        'Mechanik': [('Jaki jest telefon do warsztatu samochodowego?', 'warsztat')],
        'Krawcowa': [('Masz numer do szwaczki?', 'szwaczka')],
        'Weterynarz': [('Jaki jest numer do lekarza od zwierząt?', 'lekarz zwierząt')],
    },
    'en': {
        'door-code': [("What's the keypad combination for the entrance?", 'keypad combination')],
        'wifi': [("What's the network key at home?", 'network key')],
        'rent': [('How much does the apartment cost me each month?', 'apartment'), ("What's my monthly housing payment?", 'housing')],
        'bins': [('When does the rubbish get picked up?', 'rubbish'), ('Which day does the refuse lorry come?', 'refuse lorry')],
        'insurance': [("When does the car's cover run out?", 'cover')],
        'mot': [("When is the car's roadworthiness test due?", 'roadworthiness')],
        'babysitter': [("What's the nanny's number?", 'nanny')],
        'accountant': [("What's the bookkeeper's number?", 'bookkeeper')],
        'mechanic': [("What's the number of the garage that does my car?", 'garage')],
        'hairdresser': [("What's my barber's number?", 'barber')],
        'tailor': [("Do I have the dressmaker's number?", 'dressmaker')],
        'massage therapist': [("What's the masseuse's number?", 'masseuse')],
        'dentist': [("What's the number of my dental clinic?", 'dental clinic')],
    },
}


def edit_distance(a, b, limit):
    """Optimal string alignment distance of `a` and `b` (as the search's typo tolerance counts), or
    more than `limit` when it is larger."""
    if abs(len(a) - len(b)) > limit:
        return limit + 1
    previous2, previous = None, list(range(len(b) + 1))
    for i in range(1, len(a) + 1):
        row = [i] + [0] * len(b)
        for j in range(1, len(b) + 1):
            cost = 0 if a[i - 1] == b[j - 1] else 1
            row[j] = min(previous[j] + 1, row[j - 1] + 1, previous[j - 1] + cost)
            if i > 1 and j > 1 and a[i - 1] == b[j - 2] and a[i - 2] == b[j - 1]:
                row[j] = min(row[j], previous2[j - 2] + 1)
        previous2, previous = previous, row
    return previous[len(b)]


# The test split asks in its own words: the last of three or more ways of asking.
SPLITS = ('train', 'test')


def fold(text):
    """Lower case without diacritics (as the bench's checks compare words)."""
    text = text.replace('ł', 'l').replace('Ł', 'L')
    return ''.join(c for c in unicodedata.normalize('NFKD', text.lower()) if not unicodedata.combining(c))


LOOSE = {'ą': '[ąa]', 'ć': '[ćc]', 'ę': '[ęe]', 'ł': '[łl]', 'ń': '[ńn]', 'ó': '[óo]', 'ś': '[śs]', 'ź': '[źz]',
         'ż': '[żz]'}


def loose(text):
    """A regular expression for `text` with its Polish letters optional (ą or a ...)."""
    return ''.join(LOOSE.get(c, re.escape(c)) for c in text.lower())


def number_regex(value, decimals=0):
    """A number in a text however it is written: 1 250, 1250, 1,250 ... ("6" does not match "16")."""
    whole = int(value)
    digits = str(whole)
    groups = []
    while len(digits) > 3:
        groups.insert(0, digits[-3:])
        digits = digits[:-3]
    groups.insert(0, digits)
    body = '[ .,\u00a0]?'.join(groups)
    if decimals:
        cents = round((value - whole) * 100)
        if cents == 0:
            body += r'([.,]00?)?'
        elif cents % 10 == 0:
            body += r'[.,]%d0?' % (cents // 10)
        else:
            body += r'[.,]%02d' % cents
    return r'(^|[^0-9])%s([^0-9]|$)' % body


def date_regex(lang, day):
    """A day as an answer may write it: 2026-11-20, 20.11, 20 listopada, November 20 ..."""
    forms = [day.isoformat(), r'0?%d\.%02d' % (day.day, day.month), r'0?%d\.0?%d\.%d' % (day.day, day.month, day.year)]
    if lang == 'pl':
        forms.append(r'0?%d %s' % (day.day, PL_MONTHS_GEN[day.month - 1]))
    else:
        month = EN_MONTHS[day.month - 1]
        forms += [r'%s %d(st|nd|rd|th)?\b' % (month, day.day), r'%d(st|nd|rd|th)? (of )?%s' % (day.day, month),
                  r'%s %d\b' % (month[:3], day.day), r'%d %s\b' % (day.day, month[:3])]
    return r'(^|[^0-9])(%s)' % '|'.join(forms)


def pl_amount(value):
    """54,30 zł; 2 350 zł."""
    whole = int(value)
    cents = round((value - whole) * 100)
    text = '{:,}'.format(whole).replace(',', ' ')
    return (text + (',%02d' % cents if cents else '')) + ' zł'


def en_amount(value):
    """£54.30; £1,250."""
    whole = int(value)
    cents = round((value - whole) * 100)
    return '£{:,}'.format(whole) + ('.%02d' % cents if cents else '')


def pl_day(day):
    return '%d %s' % (day.day, PL_MONTHS_GEN[day.month - 1])


def en_day(day):
    return '%s %d' % (EN_MONTHS[day.month - 1], day.day)


def first_line(text):
    return text.strip().split('\n')[0]


def plan(call, args):
    """What the model thinks before calling a tool: what it does and why, in a line."""
    if call == 'search_notes':
        return 'I\'ll search the notes for "%s".' % args['query']
    if call == 'query_notes':
        return 'The tasks are what I need, so I\'ll ask query_notes: %s' % args['query']
    if call == 'read_page':
        return 'The page "%s" should have it: I\'ll read it.' % args['name']
    if call == 'append_to_page':
        return 'I\'ll add it at the end of "%s": %s' % (args['name'], first_line(args['markdown']))
    if call == 'append_to_journal':
        return 'This goes to the journal: I\'ll append %s' % first_line(args['markdown'])
    if call == 'update_block':
        if 'status' in args and 'text' not in args:
            return 'I have the task\'s ref: I\'ll set its status to "%s" and keep its text.' % args['status']
        return 'I have the line\'s ref: I\'ll change it with update_block.'
    if call == 'write_page':
        return 'I\'ll write the page "%s".' % args['name']
    return 'I\'ll call %s.' % call


def with_thoughts(category, ask, solution, page=None):
    """The solution with a short thought before each step, as the model writes them (in English):
    the material for teaching the tasks the model never solves on its own (docs/FINETUNE.md)."""
    steps = []
    for i, step in enumerate(solution):
        step = dict(step)
        if 'thoughts' not in step:
            if 'call' in step:
                step['thoughts'] = (('The user asks: "%s"\n' % ask) if i == 0 else '') + plan(step['call'], step['input'])
            elif category == 'clarify':
                step['thoughts'] = ('The user says "%s", but nothing says which task or note they mean. I should not '
                                    'guess or change anything: I\'ll ask which one.' % ask)
            elif category == 'decline':
                step['thoughts'] = 'The user declined the change, so I won\'t make it or try again.'
            elif i == 0 and page:
                step['thoughts'] = ('The user asks: "%s"\nThe open page "%s" has the answer, so I need no tool.'
                                    % (ask, page))
            elif i == 0:
                step['thoughts'] = ('The user asks: "%s"\nThis is not about the notes, so I need no tool: I can '
                                    'answer directly.' % ask)
            else:
                step['thoughts'] = 'I have what I need, so I can answer.'
        steps.append(step)
    return steps


class Page:
    """A note being written: its lines, and what stands on which (for the tasks' refs)."""

    def __init__(self, path, title=None):
        self.path = path
        self.lines = []
        if title is not None:
            self.lines += ['# ' + title, '']

    def add(self, line):
        self.lines.append(line)
        return len(self.lines)  # its line, from 1

    @property
    def name(self):
        return os.path.splitext(os.path.basename(self.path))[0]

    def text(self):
        return '\n'.join(self.lines) + '\n'


class Item:
    """A task line of a page."""

    def __init__(self, page, line, text, keys, status, due=None, tag=None, done=None, tag_first=False):
        self.page, self.line, self.text, self.keys = page, line, text, keys
        self.status, self.due, self.tag, self.done, self.tag_first = status, due, tag, done, tag_first

    def body(self, due=None):
        """The line's text after its checkbox, with `due` instead of its own."""
        due = due if due is not None else self.due
        parts = [self.text]
        tag = ('#' + self.tag) if self.tag else None
        if tag and self.tag_first:
            parts.append(tag)
        if due:
            parts.append('📅 ' + due.isoformat())
        if tag and not self.tag_first:
            parts.append(tag)
        if self.done:
            parts.append('✅ ' + self.done.isoformat())
        return ' '.join(parts)

    def markdown(self):
        return '- [%s] %s' % (self.status, self.body())


class Generator:
    def __init__(self, lang, seed, split):
        self.lang, self.seed, self.split = lang, seed, split
        self.rng = random.Random('%s-%s-%d' % (split, lang, seed))
        self.name = 'gen-%s-%s-%04d' % (split, lang, seed)
        start = dt.date(2026, 9, 7)
        self.today = start + dt.timedelta(days=self.rng.randrange(0, 260))
        self.now = dt.datetime.combine(self.today, dt.time(8 + self.rng.randrange(0, 12), self.rng.choice([0, 30])))
        self.female = self.rng.random() < 0.5
        self.pages = {}
        self.items = []
        self.tasks = []
        self.counts = {}
        self.used_keys = set()

    # ---- helpers ----
    def pl(self):
        return self.lang == 'pl'

    def g(self, female, male):
        """The user's own past-tense form (Polish verbs follow the speaker's gender)."""
        return female if self.female else male

    def say(self, options):
        """One way of asking: the test split keeps the last of three or more for itself."""
        if len(options) >= 3:
            return options[-1] if self.split == 'test' else self.rng.choice(options[:-1])
        return self.rng.choice(options)

    def page(self, path, title=None):
        if path not in self.pages:
            self.pages[path] = Page(path, title)
        return self.pages[path]

    def day(self, offset):
        return self.today + dt.timedelta(days=offset)

    def journal(self, day):
        return self.page('journals/%s.md' % day.isoformat())

    def with_day(self, expect, day, also=()):
        """`expect` with — in the split train only (the test keeps its checks) — a check of the day the
        answer confirms, one the solution works out ("pojutrze", "o tydzień"): the answer names it, in
        any form date_regex() knows, and no other date written as one but today's and `also` (the day a
        task moves from)."""
        if self.split == 'train':
            allowed = '|'.join(re.escape(d.isoformat()) for d in (day, self.today) + tuple(also))
            expect['answer'] = {'matches': [date_regex(self.lang, day)],
                                'notMatches': [r'(?<![0-9])(?!(?:%s)(?![0-9]))[0-9]{4}-[0-9]{2}-[0-9]{2}(?![0-9])' % allowed]}
        return expect

    def free(self, keys):
        """Keys no other fact of the notebook uses (so that a check names one thing)."""
        folded = [fold(k) for k in keys]
        for k in folded:
            for used in self.used_keys:
                if k in used or used in k:
                    return False
        return True

    def take(self, keys):
        for k in keys:
            self.used_keys.add(fold(k))

    def pick(self, pool, key_of, count):
        """`count` of `pool` whose keys are free, taken."""
        choices = list(pool)
        self.rng.shuffle(choices)
        out = []
        for c in choices:
            if len(out) == count:
                break
            if self.free(key_of(c)):
                self.take(key_of(c))
                out.append(c)
        return out

    def task(self, kind, category, ask=None, messages=None, expect=None, solution=None, answers=None, page=None,
             note=None):
        n = self.counts.get(kind, 0) + 1
        self.counts[kind] = n
        task = {'id': '%s/%s-%d' % (self.name, kind, n), 'category': category}
        if messages is not None:
            task['messages'] = messages
        else:
            task['ask'] = ask
        if page:
            task['page'] = page
        if answers:
            task['answers'] = answers
        task['expect'] = expect or {}
        last = ask if messages is None else [m['content'] for m in messages if m['role'] == 'user'][-1]
        task['solution'] = with_thoughts(category, last, solution, page)
        if note:
            task['note'] = note
        self.tasks.append(task)

    def phone(self):
        r = self.rng
        if self.pl():
            if r.random() < 0.7:
                return '%d%02d %03d %03d' % (r.choice([5, 6, 7, 8]), r.randrange(0, 100), r.randrange(0, 1000),
                                             r.randrange(0, 1000))
            return '%d %03d %02d %02d' % (r.choice([12, 22, 42, 58, 61, 71]), r.randrange(0, 1000), r.randrange(0, 100),
                                         r.randrange(0, 100))
        return '07700 9%05d' % r.randrange(0, 100000)

    @staticmethod
    def phone_forms(phone):
        digits = re.sub(r'\D', '', phone)
        return [phone, digits, phone.replace(' ', '-')]

    def person(self, gender=None):
        r = self.rng
        female = (gender == 'f') if gender else r.random() < 0.5
        if self.pl():
            first = r.choice(PL_WOMEN if female else PL_MEN)
            surname = r.choice(PL_SURNAMES)[1 if female else 0]
        else:
            first = r.choice(EN_WOMEN if female else EN_MEN)
            surname = r.choice(EN_SURNAMES)
        return first, surname, female

    # ---- the notebook ----
    def build(self):
        self.contacts()
        self.home_facts()
        self.car()
        self.task_pages()
        self.shopping()
        self.recipes()
        self.books()
        self.journals()
        self.reminders()
        self.write_page()
        self.no_tool()
        self.clarify()
        self.decline()
        self.rng.shuffle(self.tasks)
        if self.split == 'train':
            self.train_only()
        return self

    # ---- tasks for training only ----
    # Drawn by a random generator of their own after the rest: the notebook and the other tasks of
    # a seed stay what they were, and the test split never has them.
    def train_only(self):
        self.xrng = random.Random('%s-%s-%d-train-only' % (self.split, self.lang, self.seed))
        self.other_words()
        self.more_status_words()
        self.more_list_words()
        self.book_among_tasks()

    def finds_nothing(self, query):
        """Does the notes' search find nothing for `query`? As VaultIndex::search matches its terms
        (folded words): exactly, the last one also as a prefix, and with one typo from 4 letters, two
        from 8 — any of them (a query of several words that finds nothing with all of them is tried
        with each)."""
        words = set()
        for page in self.pages.values():
            words.update(re.findall(r'[^\W_]+', fold(page.name + '\n' + page.text())))
        terms = list(dict.fromkeys(re.findall(r'[^\W_]+', fold(query))))
        for n, term in enumerate(terms):
            typos = 2 if len(term) >= 8 else 1 if len(term) >= 4 else 0
            for word in words:
                if word == term or (n == len(terms) - 1 and word.startswith(term)):
                    return False
                if typos and edit_distance(term, word, typos) <= typos:
                    return False
        return True

    def other_words(self):
        """A fact asked for in other words than the note's: the search for the user's words finds
        nothing, the solution searches again with the word a note would use, or lists the pages and
        reads the one the fact is on (the home page, the car's, the contacts) — what a model trained on
        solutions whose first search always finds never learns. Either retry is right: the step names
        the other one in "alternatives" (a reward takes both, finetune/grpo.py)."""
        r = self.xrng
        pl = self.pl()
        gaps = GAPS[self.lang]
        chances = []  # (ask, the user's words, the note's word, check, answer, the fact's page, a person's page)
        for kind, (check, query, answer) in list(self.home_facts.items()) + list(self.car_facts.items()):
            page = self.home if kind in self.home_facts else self.pages['Samochód.md' if pl else 'Car.md']
            for ask, words in gaps.get(kind, []):
                chances.append((ask, words, query, check, answer, page, None))
        for role, first, surname, phone, hours, where in self.people:
            label_pl, _gender, _gen, label_en = role
            for ask, words in gaps.get(label_pl if pl else label_en, []):
                answer = ('%s: %s %s, tel. %s ([[%s]]).' % (label_pl, first, surname, phone, where) if pl else
                          'Your %s is %s %s, phone %s ([[%s]]).' % (label_en, first, surname, phone, where))
                # (Each person a page of their own: no list of pages says which one is the role's.)
                chances.append((ask, words, label_pl.lower() if pl else label_en, {'anyOf': [self.phone_forms(phone)]},
                                answer, None if self.per_page else self.pages[where + '.md'],
                                (where, phone) if self.per_page else None))
        chances = [c for c in chances if self.finds_nothing(c[1])]
        r.shuffle(chances)
        for ask, words, query, check, answer, page, person in chances[:3]:
            missed = ('Nothing found for "%s". The notes may name it otherwise: ' % words)
            steps = [{'call': 'search_notes', 'input': {'query': words}}]
            search = {'call': 'search_notes', 'input': {'query': query}}
            listing = {'call': 'list_pages', 'input': {}}
            if page and r.random() < 0.35:
                steps.append(dict(listing, alternatives=[search],
                                  thoughts=missed + 'I\'ll see which pages there are, then read the one it would be on.'))
                steps.append({'call': 'read_page', 'input': {'name': page.name}})
            else:
                steps.append(dict(search, thoughts=missed + 'I\'ll search once more, for "%s".' % query))
                if page:
                    steps[-1]['alternatives'] = [listing]
                if person:
                    steps.append({'call': 'read_page', 'input': {'name': person[0]}, 'unlessShown': person[1],
                                  'thoughts': 'The search found the page "%s", but not the number: I\'ll read it.' % person[0]})
            self.task('inne-slowa' if pl else 'other-words', 'search', ask, expect={'answer': check},
                      note='The user\'s words are in no note: the first search finds nothing, another word (or the '
                           'page it is on) does.',
                      solution=steps + [{'answer': answer}])

    def more_status_words(self):
        """A thing bought and a task started, in more words than the tasks above use."""
        r = self.xrng
        pl = self.pl()
        page = self.shopping_page
        s = self.to_buy[0]
        what, acc = (s[0], s[1]) if pl else (s[3], s[3])
        ask = r.choice(['Kupione: %s. Odhacz na liście.' % what, '%s mam już w domu, odhacz to w zakupach.' % (what[0].upper() + what[1:]),
                        '%s %s, skreśl z listy zakupów.' % (self.g('Kupiłam', 'Kupiłem'), acc)] if pl else
                       ['Picked up the %s — cross it off.' % what, 'Bought the %s, update the shopping list.' % what,
                        'Got %s today, mark it on the list.' % what])
        self.task('kupione' if pl else 'bought', 'update', ask,
                  expect={'files': [{'path': page.path, 'matches': [r'^- \[x\] %s' % re.escape(what)]}]},
                  solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks "%s"' % what}},
                            {'call': 'update_block', 'input': {'ref': '{{ref %s#L%d}}' % (page.path, self.shopping_lines[what]),
                                                                'status': 'done'}},
                            {'answer': ('Odhaczyłam %s w [[%s]].' if pl else 'Ticked off %s in [[%s]].') % (what, page.name)}])
        open_items = [it for it in self.items if it.status == ' ' and it.role != 'none']
        if open_items and r.random() < 0.7:
            it = r.choice(open_items)
            quoted = it.text[0].lower() + it.text[1:]
            ask = r.choice(['Biorę się za „%s”, oznacz to jako w toku.' % quoted, '„%s” — już nad tym pracuję, zmień status.' % it.text,
                            'Jestem w trakcie: „%s”. Zaktualizuj zadanie.' % quoted] if pl else
                           ['I\'m working on "%s" now, mark it.' % quoted, 'Started on "%s" — set its status.' % quoted,
                            'Begun: "%s". Please update the task.' % quoted])
            self.task('w-toku-zmiana' if pl else 'started', 'update', ask,
                      expect={'files': [{'path': it.page.path, 'matches': [r'^- \[/\] %s' % re.escape(it.text)]}]},
                      solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks "%s"' % it.text}},
                                {'call': 'update_block', 'input': {'ref': '{{ref %s#L%d}}' % (it.page.path, it.line),
                                                                    'status': 'doing'}},
                                {'answer': ('Oznaczyłam „%s” jako w toku ([[%s]]).' if pl else
                                            'Marked "%s" as in progress ([[%s]]).') % (it.text, it.page.name)}])

    def more_list_words(self):
        """A book onto the reading list, the list called otherwise ("lista lektur", "to-read list")."""
        r = self.xrng
        pl = self.pl()
        page = self.books_page
        left = [b for b in (PL_BOOKS if pl else EN_BOOKS) if b not in self.books_chosen][1:]
        if not left:
            return
        title, author = r.choice(left)
        ask = r.choice(['Dopisz „%s” do mojej listy lektur.' % title, 'Dodaj na listę do przeczytania: „%s” (%s).' % (title, author),
                        'Wpisz „%s” na moją listę książek.' % title] if pl else
                       ['Add "%s" to the books I want to read.' % title, 'Put "%s" by %s on my to-read list.' % (title, author),
                        'I\'d like to read "%s" — note it with my books.' % title])
        self.task('lista-lektur' if pl else 'to-read', 'append', ask,
                  expect={'files': [{'path': page.path, 'contains': [title, self.books_chosen[3][0]]}],
                          'toolsNotCalled': ['write_page']},
                  solution=[{'call': 'append_to_page', 'input': {'name': page.name, 'markdown':
                                                                 ('- [ ] „%s” — %s' if pl else '- [ ] "%s" by %s') % (title, author)}},
                            {'answer': ('Dopisałam „%s” do [[%s]].' if pl else 'Added "%s" to [[%s]].') % (title, page.name)}])

    def book_among_tasks(self):
        """The book being read, asked so that the tasks in progress answer it: a work task in progress
        among them, the answer names the book."""
        r = self.xrng
        pl = self.pl()
        title, author = self.reading
        ask = r.choice([self.g('Którą książkę zaczęłam czytać?', 'Którą książkę zacząłem czytać?'),
                        'Która z rzeczy w toku to książka?', 'Jaką lekturę mam rozpoczętą?'] if pl else
                       ['Which book have I started?', 'Which of the things in progress is a book?',
                        'What book is on the go right now?'])
        self.task('ksiazka-w-toku' if pl else 'book-started', 'read', ask, expect={'answer': {'contains': [title]}},
                  solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks status:doing'}},
                            {'answer': ('„%s” (%s) — to jedyna książka w toku ([[%s]]).' if pl else
                                        '"%s" by %s: the one book in progress ([[%s]]).') % (title, author, self.books_page.name)}])

    def contacts(self):
        r = self.rng
        roles = self.pick(ROLES, lambda role: [role[0] if self.pl() else role[3]], 6)
        per_page = r.random() < 0.4
        contacts_page = None if per_page else self.page('Kontakty.md' if self.pl() else 'Contacts.md',
                                                        'Kontakty' if self.pl() else 'Contacts')
        people = []
        for role in roles:
            label_pl, gender, gen_pl, label_en = role
            first, surname, female = self.person(gender if self.pl() else None)
            phone = self.phone()
            hours = r.choice(PL_CALL_HOURS if self.pl() else EN_CALL_HOURS) if r.random() < 0.5 else None
            if per_page:
                folder = 'Ludzie' if self.pl() else 'People'
                p = self.page('%s/%s %s.md' % (folder, first, surname), '%s %s' % (first, surname))
                p.add('- ' + (label_pl if self.pl() else label_en.capitalize()))
                p.add(('- Telefon: ' if self.pl() else '- Phone: ') + phone)
                if hours:
                    p.add('- ' + hours[0].capitalize())
                where = p.name
            else:
                who = ('%s %s %s' % ('pani' if female else 'pan', first, surname)) if self.pl() else '%s %s' % (first, surname)
                line = '- %s: %s, %s %s' % (label_pl if self.pl() else label_en.capitalize(), who,
                                            'tel.' if self.pl() else 'phone', phone)
                if hours:
                    line += ', ' + hours[0]
                contacts_page.add(line)
                where = contacts_page.name
            people.append((role, first, surname, phone, hours, where))
        self.people, self.per_page = people, per_page

        def found(query, where, fact):
            """The search, and — when each person has a page of their own — the page, unless the search
            showed `fact` already: the line it finds is the role's, the phone and the hours are on
            the next ones (an answer gives only what a tool's result showed)."""
            steps = [{'call': 'search_notes', 'input': {'query': query}}]
            if per_page:
                steps.append({'call': 'read_page', 'input': {'name': where}, 'unlessShown': fact,
                              'thoughts': 'The search found the page "%s", but not what is asked: I\'ll read it.' % where})
            return steps

        # The phone of two of them.
        for role, first, surname, phone, hours, where in r.sample(people, 2):
            label_pl, gender, gen_pl, label_en = role
            if self.pl():
                ask = self.say(['Jaki jest numer do %s?' % gen_pl, 'Podaj mi telefon do %s.' % gen_pl,
                                'Masz gdzieś numer %s?' % gen_pl, 'Muszę zadzwonić do %s. Jaki ma numer?' % gen_pl])
                answer = '%s: %s %s, tel. %s ([[%s]]).' % (label_pl, first, surname, phone, where)
            else:
                ask = self.say(["What's the %s's phone number?" % label_en, 'Give me the number of the %s.' % label_en,
                                "Do I have the %s's number somewhere?" % label_en,
                                'I need to ring the %s, what number do I call?' % label_en])
                answer = 'Your %s is %s %s, phone %s ([[%s]]).' % (label_en, first, surname, phone, where)
            self.task('telefon' if self.pl() else 'phone', 'search', ask,
                      expect={'answer': {'anyOf': [self.phone_forms(phone)]}},
                      solution=found(label_pl.lower() if self.pl() else label_en, where, phone) + [{'answer': answer}])
        # When to call one of them.
        timed = [p for p in people if p[4]]
        if timed:
            role, first, surname, phone, hours, where = r.choice(timed)
            label_pl, gender, gen_pl, label_en = role
            if self.pl():
                ask = self.say(['Kiedy najlepiej dzwonić do %s?' % gen_pl, 'O której mogę dzwonić do %s?' % gen_pl,
                                'Czy %s ma jakieś godziny, w których woli telefony?' % label_pl.lower()])
                answer = '%s %s: %s ([[%s]]).' % (first, surname, hours[0], where)
            else:
                ask = self.say(["When's the best time to call the %s?" % label_en,
                                'At what time can I ring the %s?' % label_en,
                                'Does the %s prefer calls at some hours?' % label_en])
                answer = '%s %s: %s ([[%s]]).' % (first, surname, hours[0], where)
            self.task('godziny' if self.pl() else 'hours', 'search', ask,
                      expect={'answer': {'matches': [r'(^|[^0-9])%s([^0-9]|$)' % hours[1]]}},
                      solution=found(label_pl.lower() if self.pl() else label_en, where, hours[0]) + [{'answer': answer}])
        # Someone who is not in the notes.
        absent = self.pick(ROLES, lambda role: [role[0] if self.pl() else role[3]], 1)
        if absent:
            label_pl, gender, gen_pl, label_en = absent[0]
            if self.pl():
                ask = self.say(['Jaki jest numer do %s?' % gen_pl, 'Podaj mi telefon do %s.' % gen_pl,
                                'Masz zapisany kontakt do %s?' % gen_pl])
                answer = 'Nie znalazłam w notatkach numeru do %s.' % gen_pl
                not_found = PL_NOT_FOUND
            else:
                ask = self.say(["What's the %s's phone number?" % label_en, 'Give me the number of the %s.' % label_en,
                                'Have I saved a contact for a %s?' % label_en])
                answer = "I couldn't find a %s's number in your notes." % label_en
                not_found = EN_NOT_FOUND
            self.task('brak-kontaktu' if self.pl() else 'no-contact', 'search', ask,
                      expect={'answer': {'anyOf': [not_found]}},
                      note='Not in the notes: the answer says so and makes no number up.',
                      solution=[{'call': 'search_notes', 'input': {'query': label_pl.lower() if self.pl() else label_en}},
                                {'answer': answer}])

    def home_facts(self):
        r = self.rng
        home = self.page('Dom.md' if self.pl() else 'Home.md', 'Dom' if self.pl() else 'Home')
        self.home = home
        code = '%d%d%d%d' % tuple(r.randrange(1, 10) for _ in range(4))
        facts = []
        if self.pl():
            home.add('- Kod do domofonu: %s' % code)
            facts.append(('domofon', ['Jaki jest kod do domofonu?', 'Podaj kod do domofonu.', 'Jak brzmi kod do drzwi na dole?'],
                          {'matches': [r'(^|[^0-9])%s([^0-9]|$)' % code]}, 'domofon', 'Kod do domofonu to %s.' % code))
        else:
            home.add('- Door code: %s' % code)
            facts.append(('door-code', ["What's the door code?", 'Tell me the code for the front door.',
                                        'Which code opens the building door?'],
                          {'matches': [r'(^|[^0-9])%s([^0-9]|$)' % code]}, 'door code', 'The door code is %s.' % code))
        has_wifi = r.random() < 0.6
        if has_wifi:
            password = r.choice(WIFI_WORDS) + str(r.randrange(10, 100))
            if self.pl():
                home.add('- Hasło do Wi-Fi: %s' % password)
                facts.append(('wifi', ['Jakie jest hasło do Wi-Fi?', 'Podaj hasło do internetu w domu.',
                                       'Goście pytają o hasło do Wi-Fi, jakie jest?'],
                              {'contains': [password]}, 'Wi-Fi', 'Hasło do Wi-Fi: %s.' % password))
            else:
                home.add('- Wi-Fi password: %s' % password)
                facts.append(('wifi', ["What's the Wi-Fi password?", 'Give me the home internet password.',
                                       'Guests are asking for the Wi-Fi password, what is it?'],
                              {'contains': [password]}, 'Wi-Fi', 'The Wi-Fi password is %s.' % password))
        rent = r.randrange(18, 40) * 50 if self.pl() else r.randrange(16, 36) * 50
        rent_day = r.choice([1, 5, 10, 15])
        if self.pl():
            home.add('- Czynsz: %s, płatny do %d. dnia miesiąca' % (pl_amount(rent), rent_day))
            facts.append(('czynsz', ['Ile wynosi czynsz?', 'Ile płacę czynszu?', 'Jaki mam czynsz za mieszkanie?'],
                          {'matches': [number_regex(rent)]}, 'czynsz', 'Czynsz to %s, płatny do %d. dnia miesiąca.' % (pl_amount(rent), rent_day)))
        else:
            home.add('- Rent: %s a month, due on day %d' % (en_amount(rent), rent_day))
            facts.append(('rent', ['How much is the rent?', 'What rent do I pay?', 'How much do I pay for the flat?'],
                          {'matches': [number_regex(rent)]}, 'rent', 'The rent is %s a month, due on day %d.' % (en_amount(rent), rent_day)))
        bins = r.randrange(0, 7)
        if self.pl():
            home.add('- Wywóz śmieci: %s' % PL_EVERY_WEEKDAY[bins])
            stem = ['poniedział', 'wtor', 'środ', 'czwart', 'piąt', 'sobot', 'niedziel'][bins]
            facts.append(('smieci', ['Kiedy jest wywóz śmieci?', 'W jaki dzień wywożą śmieci?',
                                     'Którego dnia mam wystawić kosze?'],
                          {'matches': [loose(stem)]}, 'śmieci', 'Śmieci są wywożone %s.' % PL_EVERY_WEEKDAY[bins]))
        else:
            home.add('- Bin collection: every %s' % EN_WEEKDAYS[bins])
            facts.append(('bins', ['When are the bins collected?', 'Which day is bin collection?',
                                   'What day do I put the bins out?'],
                          {'contains': [EN_WEEKDAYS[bins]]}, 'bin', 'The bins are collected every %s.' % EN_WEEKDAYS[bins]))
        self.home_facts = {kind: (check, query, answer + ' ([[%s]])' % home.name) for kind, _a, check, query, answer in facts}
        for kind, asks, check, query, answer in r.sample(facts, min(2, len(facts))):
            self.task(kind, 'search', self.say(asks), expect={'answer': check},
                      solution=[{'call': 'search_notes', 'input': {'query': query}},
                                {'answer': answer + ' ([[%s]])' % home.name}])
        if not has_wifi and r.random() < 0.7:
            asks = (['Jakie jest hasło do Wi-Fi?', 'Podaj hasło do internetu w domu.', 'Goście pytają o hasło do Wi-Fi, jakie jest?']
                    if self.pl() else
                    ["What's the Wi-Fi password?", 'Give me the home internet password.', 'Guests are asking for the Wi-Fi password, what is it?'])
            self.task('brak-wifi' if self.pl() else 'no-wifi', 'search', self.say(asks),
                      expect={'answer': {'anyOf': [PL_NOT_FOUND if self.pl() else EN_NOT_FOUND]}},
                      note='Not in the notes: the answer says so and makes nothing up.',
                      solution=[{'call': 'search_notes', 'input': {'query': 'Wi-Fi'}},
                                {'answer': 'Nie ma w notatkach hasła do Wi-Fi.' if self.pl() else
                                 "I couldn't find the Wi-Fi password in your notes."}])

    def car(self):
        r = self.rng
        self.car_facts = {}
        if r.random() < 0.25:
            return
        car = self.page('Samochód.md' if self.pl() else 'Car.md', 'Samochód' if self.pl() else 'Car')
        model = r.choice(PL_CARS if self.pl() else EN_CARS)
        year = r.randrange(2012, 2025)
        insurer = r.choice(PL_INSURERS if self.pl() else EN_INSURERS)
        policy = '%04d-%02d-%04d' % (r.randrange(1000, 10000), r.randrange(10, 100), r.randrange(1000, 10000))
        until = self.day(r.randrange(20, 300))
        inspection = self.day(r.randrange(30, 400))
        oil = self.day(-r.randrange(30, 200))
        km = r.randrange(40, 180) * 1000 + r.randrange(0, 10) * 100
        if self.pl():
            plate = 'W%s %04d%s' % (r.choice('XAWYZ'), r.randrange(1000, 10000), r.choice('KACEM'))
            car.add('- %s, rok %d, rejestracja %s' % (model, year, plate))
            car.add('- Ubezpieczenie OC: %s, polisa nr %s, ważne do %s' % (insurer, policy, until.isoformat()))
            car.add('- Przegląd techniczny ważny do %s' % inspection.isoformat())
            car.add('- Ostatnia wymiana oleju: %s przy %s km' % (oil.isoformat(), '{:,}'.format(km).replace(',', ' ')))
            facts = [('oc', ['Do kiedy mam ważne OC?', 'Kiedy kończy się ubezpieczenie samochodu?', 'Do kiedy jest ważna polisa OC?'],
                      {'matches': [date_regex('pl', until)]}, 'OC', 'OC w %s jest ważne do %s.' % (insurer, until.isoformat())),
                     ('polisa', ['Jaki jest numer polisy OC?', 'Podaj numer polisy samochodu.', 'Potrzebuję numeru polisy, jaki to?'],
                      {'contains': [policy]}, 'polisa', 'Numer polisy OC (%s): %s.' % (insurer, policy)),
                     ('przeglad', ['Do kiedy jest ważny przegląd auta?', 'Kiedy mam następny przegląd techniczny?',
                                   'Do kiedy mam przegląd samochodu?'],
                      {'matches': [date_regex('pl', inspection)]}, 'przegląd', 'Przegląd techniczny jest ważny do %s.' % inspection.isoformat()),
                     ('rejestracja', ['Jaki mam numer rejestracyjny?', 'Podaj numer rejestracji auta.', 'Jakie są moje tablice rejestracyjne?'],
                      {'contains': [plate]}, 'rejestracja', 'Numer rejestracyjny: %s.' % plate),
                     ('olej', ['Kiedy ostatnio wymieniałem olej?' if not self.female else 'Kiedy ostatnio wymieniałam olej?',
                               'Kiedy była ostatnia wymiana oleju?', 'Przy jakim przebiegu wymieniano olej i kiedy?'],
                      {'matches': [date_regex('pl', oil)]}, 'olej', 'Ostatnia wymiana oleju: %s, przy %s km.' % (oil.isoformat(), km))]
        else:
            plate = '%s%02d %s' % (''.join(r.choice('ABCDEFGHKLMNOPRSVWY') for _ in range(2)), r.randrange(10, 75),
                                   ''.join(r.choice('ABCDEFGHJKLMNPRSTUVWXYZ') for _ in range(3)))
            car.add('- %s, %d, registration %s' % (model, year, plate))
            car.add('- Insurance: %s, policy %s, valid until %s' % (insurer, policy, until.isoformat()))
            car.add('- MOT valid until %s' % inspection.isoformat())
            car.add('- Last oil change: %s at %s miles' % (oil.isoformat(), '{:,}'.format(km)))
            facts = [('insurance', ['When does the car insurance run out?', 'Until when is my car insured?',
                                    'What date does the insurance policy end?'],
                      {'matches': [date_regex('en', until)]}, 'insurance', 'Your %s insurance is valid until %s.' % (insurer, until.isoformat())),
                     ('policy', ["What's my car insurance policy number?", 'Give me the policy number of the car.',
                                 'I need the insurance policy number, which is it?'],
                      {'contains': [policy]}, 'policy', 'The policy number (%s) is %s.' % (insurer, policy)),
                     ('mot', ['When is the MOT due?', 'Until when is the MOT valid?', 'When do I need the next MOT?'],
                      {'matches': [date_regex('en', inspection)]}, 'MOT', 'The MOT is valid until %s.' % inspection.isoformat()),
                     ('plate', ["What's my registration number?", "Give me the car's number plate.",
                                'Which number plate does the car have?'],
                      {'contains': [plate]}, 'registration', 'The registration is %s.' % plate),
                     ('oil', ['When did I last change the oil?', 'When was the last oil change?',
                              'At what mileage was the oil last changed, and when?'],
                      {'matches': [date_regex('en', oil)]}, 'oil', 'The last oil change was on %s, at %s miles.' % (oil.isoformat(), km))]
        self.car_facts = {kind: (check, query, answer + ' ([[%s]])' % car.name) for kind, _a, check, query, answer in facts}
        for kind, asks, check, query, answer in r.sample(facts, 2):
            self.task(kind, 'search', self.say(asks), expect={'answer': check},
                      solution=[{'call': 'search_notes', 'input': {'query': query}},
                                {'answer': answer + ' ([[%s]])' % car.name}])

    def task_pages(self):
        r = self.rng
        pl = self.pl()
        # Chores on the home page, work on a project's: when each is due, as the queries need.
        chores = self.pick(CHORES, lambda c: c[1] if pl else c[3], 6)
        work = self.pick(WORK, lambda c: c[1] if pl else c[3], 5)
        project = r.choice(PL_PROJECTS if pl else EN_PROJECTS)
        folder = 'Projekty' if pl else 'Projects'
        proj = self.page('%s/%s.md' % (folder, project), project)
        proj.add(('Termin całości: %s' if pl else 'Whole project due: %s') % self.day(r.randrange(30, 90)).isoformat())
        proj.add('')
        roles_home = ['overdue', 'today', 'soon', 'later', 'done', 'none']
        roles_work = ['doing', 'soon', 'later', 'overdue', 'done']
        r.shuffle(roles_home)
        r.shuffle(roles_work)
        tags = PL_TAGS if pl else EN_TAGS
        work_tags = PL_WORK_TAGS if pl else EN_WORK_TAGS
        home_tag = r.choice(tags)
        home = self.home
        home.add('')
        for (role, chore) in zip(roles_home, chores):
            text, keys = (chore[0], chore[1]) if pl else (chore[2], chore[3])
            self.items.append(self.item(home, text, keys, role, home_tag if r.random() < 0.5 else r.choice(tags)))
        for (role, job) in zip(roles_work, work):
            text, keys = (job[0], job[1]) if pl else (job[2], job[3])
            self.items.append(self.item(proj, text, keys, role, r.choice(work_tags) if r.random() < 0.6 else None))
        self.queries(home_tag)
        self.updates()

    def item(self, page, text, keys, role, tag):
        r = self.rng
        due = None
        done = None
        status = ' '
        if role == 'overdue':
            due = self.day(-r.randrange(1, 10))
        elif role == 'today':
            due = self.today
        elif role == 'soon':
            due = self.day(r.randrange(1, 7))
        elif role == 'later':
            due = self.day(r.randrange(12, 40))
        elif role == 'done':
            status = 'x'
            done = self.day(-r.randrange(1, 6))
            due = self.day(-r.randrange(0, 8)) if r.random() < 0.5 else None
        elif role == 'doing':
            status = '/'
            due = self.day(r.randrange(3, 20)) if r.random() < 0.5 else None
        item = Item(page, 0, text, keys, status, due, tag, done, tag_first=r.random() < 0.3)
        item.role = role
        item.line = page.add(item.markdown())
        return item

    def keys_answer(self, items):
        return [[k for k in it.keys] for it in items]

    def queries(self, home_tag):
        r = self.rng
        pl = self.pl()
        by = {}
        for it in self.items:
            by.setdefault(it.role, []).append(it)
        open_items = [it for it in self.items if it.status in (' ', '/')]
        week = [it for it in open_items if it.due and self.today <= it.due <= self.day(7)]
        later = [it for it in open_items if it.due and it.due > self.day(7)]
        overdue = [it for it in open_items if it.due and it.due < self.today]
        done = [it for it in self.items if it.status == 'x']

        def listing(items):
            return '\n'.join('- %s%s ([[%s]])' % (it.text, (' (%s)' % it.due.isoformat()) if it.due else '', it.page.name)
                             for it in items)

        choices = []
        if week and later:
            asks = (['Co mam do zrobienia w ciągu najbliższych 7 dni?', 'Jakie zadania mają termin w najbliższym tygodniu?',
                     'Co muszę zrobić w ciągu siedmiu dni?', 'Pokaż mi wszystko, co ma termin w ciągu tygodnia od dziś.']
                    if pl else
                    ["What's due in the next 7 days?", 'Which tasks are due within a week?', 'What do I have to do in the next seven days?',
                     'Show me everything due in the coming week, starting today.'])
            choices.append(('tydzien' if pl else 'week', asks,
                            {'anyOf': self.keys_answer(week), 'notContains': [later[0].keys[0]] + [d.keys[0] for d in done[:1]]},
                            'type:tasks status:open due:today..+7d',
                            ('W ciągu 7 dni:\n' if pl else 'Due in the next 7 days:\n') + listing(week)))
        if overdue and later:
            asks = (['Czy coś jest zaległe?', 'Jakie zadania mają minięty termin?', 'Co mi się przeterminowało?',
                     self.g('Czego nie zrobiłam na czas?', 'Czego nie zrobiłem na czas?')]
                    if pl else
                    ['Is anything overdue?', 'Which tasks are past their due date?', 'What have I let slip past its deadline?',
                     "What didn't I get done in time?"])
            choices.append(('zalegle' if pl else 'overdue', asks,
                            {'anyOf': self.keys_answer(overdue), 'notContains': [later[0].keys[0]]},
                            'type:tasks due:overdue',
                            ('Zaległe:\n' if pl else 'Overdue:\n') + listing(overdue)))
        doing = by.get('doing', [])
        if doing:
            asks = (['Nad czym teraz pracuję według zadań?', 'Które zadania są w toku?', 'Co mam rozpoczęte?',
                     'Pokaż zadania, które są w trakcie.']
                    if pl else
                    ['What am I working on right now, according to my tasks?', 'Which tasks are in progress?',
                     'What have I started?', 'Show me the tasks that are under way.'])
            choices.append(('w-toku' if pl else 'doing', asks, {'anyOf': self.keys_answer(doing)},
                            'type:tasks status:doing', ('W toku:\n' if pl else 'In progress:\n') + listing(doing)))
        tagged = [it for it in open_items if it.tag == home_tag]
        untagged = [it for it in open_items if it.tag != home_tag]
        if tagged and untagged:
            asks = (['Jakie mam otwarte zadania z tagiem #%s?' % home_tag, 'Pokaż niezrobione zadania oznaczone #%s.' % home_tag,
                     'Co zostało do zrobienia w #%s?' % home_tag]
                    if pl else
                    ['Which open tasks are tagged #%s?' % home_tag, 'Show the unfinished tasks marked #%s.' % home_tag,
                     "What's left to do under #%s?" % home_tag])
            choices.append(('tag', asks, {'anyOf': self.keys_answer(tagged), 'notContains': [untagged[0].keys[0]]},
                            'type:tasks status:open tag:%s' % home_tag,
                            ('Otwarte z #%s:\n' if pl else 'Open, tagged #%s:\n') % home_tag + listing(tagged)))
        today = by.get('today', [])
        if today and later:
            asks = (['Co mam dziś do zrobienia?', 'Jakie zadania mają termin na dziś?', 'Co muszę dzisiaj załatwić?']
                    if pl else
                    ['What do I have to do today?', 'Which tasks are due today?', 'What needs sorting out today?'])
            choices.append(('dzis' if pl else 'today', asks,
                            {'anyOf': self.keys_answer(today), 'notContains': [later[0].keys[0]]},
                            'type:tasks status:open due:today', ('Na dziś:\n' if pl else 'Due today:\n') + listing(today)))
        if done:
            asks = (['Co już zrobiłam w ostatnich dniach?' if self.female else 'Co już zrobiłem w ostatnich dniach?',
                     'Które zadania odhaczyłam w ostatnim tygodniu?' if self.female else 'Które zadania odhaczyłem w ostatnim tygodniu?',
                     'Pokaż zadania zakończone w ciągu ostatnich 7 dni.']
                    if pl else
                    ['What have I finished in the last few days?', 'Which tasks did I tick off this past week?',
                     'Show the tasks completed in the last 7 days.'])
            choices.append(('zrobione' if pl else 'done', asks, {'anyOf': self.keys_answer(done)},
                            'type:tasks status:done done:-7d..today', ('Zrobione:\n' if pl else 'Done:\n') + listing(done)))
        for kind, asks, check, query, answer in r.sample(choices, min(4, len(choices))):
            self.task(kind, 'query', self.say(asks), expect={'answer': check},
                      solution=[{'call': 'query_notes', 'input': {'query': query}}, {'answer': answer}])

        # How many open tasks a page has.
        page = r.choice([self.home, self.items[-1].page])
        count = len([it for it in open_items if it.page is page])
        asks = (['Ile mam otwartych zadań na stronie %s?' % page.name, 'Ile zadań zostało do zrobienia w [[%s]]?' % page.name,
                 'Policz niezrobione zadania na stronie %s.' % page.name]
                if pl else
                ['How many open tasks are on the page %s?' % page.name, 'How many tasks are left in [[%s]]?' % page.name,
                 'Count the unfinished tasks on the %s page.' % page.name])
        words = {1: 'one', 2: 'two', 3: 'three', 4: 'four', 5: 'five', 6: 'six'} if not pl else \
            {1: 'jedn', 2: 'dwa', 3: 'trzy', 4: 'cztery', 5: 'pięć', 6: 'sześć'}
        self.task('ile-zadan' if pl else 'count', 'multi', self.say(asks),
                  expect={'answer': {'matches': [r'%s|\b%s' % (number_regex(count), loose(words[count]))]}},
                  solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks status:open page:"%s"' % page.name}},
                            {'answer': ('Na stronie %s jest %d otwartych zadań.' if pl else 'The page %s has %d open tasks.') % (page.name, count)}])

    def updates(self):
        r = self.rng
        pl = self.pl()
        open_items = [it for it in self.items if it.status == ' ' and it.role != 'none']
        r.shuffle(open_items)
        # Done: two of them.
        for it in open_items[:2]:
            quoted = it.text[0].lower() + it.text[1:]
            if pl:
                ask = self.say(['Odhacz „%s”.' % quoted, 'Zadanie „%s” jest zrobione, odhacz je.' % quoted,
                                'Oznacz jako zrobione: %s.' % quoted,
                                'Możesz odhaczyć „%s”? %s' % (quoted, self.g('Już to zrobiłam.', 'Już to zrobiłem.'))])
                answer = 'Odhaczyłam „%s” w [[%s]].' % (it.text, it.page.name)
            else:
                ask = self.say(['Tick off "%s".' % quoted, 'I\'ve done "%s", mark it as done.' % quoted,
                                'Mark this as done: %s.' % quoted, 'Done with "%s" — could you check it off?' % quoted])
                answer = 'Marked "%s" as done in [[%s]].' % (it.text, it.page.name)
            contains = []
            if it.due:
                contains.append('📅 ' + it.due.isoformat())
            if it.tag:
                contains.append('#' + it.tag)
            check = {'path': it.page.path, 'matches': [r'^- \[x\] %s' % re.escape(it.text)]}
            if contains:
                check['contains'] = contains
            self.task('odhacz' if pl else 'tick', 'update', ask, expect={'files': [check]},
                      solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks "%s"' % it.text}},
                                {'call': 'update_block', 'input': {'ref': '{{ref %s#L%d}}' % (it.page.path, it.line),
                                                                    'status': 'done'}},
                                {'answer': answer}])
        # Moved to another day: one of them.
        dated = [it for it in open_items[2:] if it.due]
        if dated:
            it = dated[0]
            way = r.randrange(0, 3)
            if way == 0:
                new = it.due + dt.timedelta(days=7)
                when_pl, when_en = 'o tydzień', 'by a week'
            elif way == 1:
                new = self.day(r.randrange(10, 30))
                when_pl, when_en = 'na ' + pl_day(new), 'to ' + en_day(new)
            else:
                ahead = r.randrange(1, 7)
                new = self.day(ahead)
                when_pl, when_en = PL_FOR_WEEKDAY[new.weekday()], 'to ' + EN_WEEKDAYS[new.weekday()]
            if new == it.due:
                new = it.due + dt.timedelta(days=7)
                when_pl, when_en = 'o tydzień', 'by a week'
            quoted = it.text[0].lower() + it.text[1:]
            if pl:
                ask = self.say(['Przesuń „%s” %s.' % (quoted, when_pl), 'Zmień termin zadania „%s” %s.' % (quoted, when_pl),
                                'Przełóż „%s” %s.' % (quoted, when_pl)])
                answer = 'Przesunęłam „%s” na %s ([[%s]]).' % (it.text, new.isoformat(), it.page.name)
            else:
                ask = self.say(['Move "%s" %s.' % (quoted, when_en), 'Change the due date of "%s" %s.' % (quoted, when_en),
                                'Push "%s" back %s.' % (quoted, when_en)])
                answer = 'Moved "%s" to %s ([[%s]]).' % (it.text, new.isoformat(), it.page.name)
            moved = {'path': it.page.path, 'matches': [r'^- \[ \] %s.*📅 %s' % (re.escape(it.text), new.isoformat())]}
            old = '📅 ' + it.due.isoformat()
            if sum(old in line for line in it.page.lines) > 1:
                # (Another task of the page is due that day too: only the moved line must not keep it.)
                moved['notMatches'] = [r'^- \[.\] %s.*%s' % (re.escape(it.text), re.escape(old))]
            else:
                moved['notContains'] = [old]
            self.task('przesun' if pl else 'move', 'update', ask,
                      expect=self.with_day({'files': [moved]}, new, (it.due,)),
                      solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks "%s"' % it.text}},
                                {'call': 'update_block', 'input': {'ref': '{{ref %s#L%d}}' % (it.page.path, it.line),
                                                                    'text': it.body(due=new)}},
                                {'answer': answer}])
        # Started: one of them, now and then.
        rest = [it for it in open_items[3:]]
        if rest and r.random() < 0.6:
            it = rest[0]
            quoted = it.text[0].lower() + it.text[1:]
            if pl:
                ask = self.say(['%s „%s”, oznacz jako w toku.' % (self.g('Zaczęłam', 'Zacząłem'), quoted),
                                'Ustaw „%s” jako zadanie w trakcie.' % quoted,
                                'Oznacz, że „%s” jest już rozpoczęte.' % quoted])
                answer = 'Oznaczyłam „%s” jako w toku ([[%s]]).' % (it.text, it.page.name)
            else:
                ask = self.say(['I\'ve started "%s", mark it as in progress.' % quoted, 'Set "%s" to in progress.' % quoted,
                                'Show "%s" as under way.' % quoted])
                answer = 'Marked "%s" as in progress ([[%s]]).' % (it.text, it.page.name)
            self.task('w-trakcie' if pl else 'start', 'update', ask,
                      expect={'files': [{'path': it.page.path, 'matches': [r'^- \[/\] %s' % re.escape(it.text)]}]},
                      solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks "%s"' % it.text}},
                                {'call': 'update_block', 'input': {'ref': '{{ref %s#L%d}}' % (it.page.path, it.line),
                                                                    'status': 'doing'}},
                                {'answer': answer}])

    def shopping(self):
        r = self.rng
        pl = self.pl()
        page = self.page('Zakupy.md' if pl else 'Shopping.md', 'Zakupy' if pl else 'Shopping')
        items = self.pick(SHOPPING, lambda s: s[2] if pl else s[4], 11)
        stock, extra = items[:7], items[7:]
        lines = {}
        bought = set(r.sample(range(len(stock)), 2))
        for i, s in enumerate(stock):
            lines[i] = page.add('- [%s] %s' % ('x' if i in bought else ' ', s[0] if pl else s[3]))
        to_buy = [s for i, s in enumerate(stock) if i not in bought]

        def keys(s):
            return s[2] if pl else s[4]

        def name(s):
            return s[0] if pl else s[3]

        kept = [keys(s)[0] for s in to_buy[:2]]
        # One thing added.
        new = extra[0]
        if pl:
            ask = self.say(['Dopisz %s do listy zakupów.' % new[1], 'Dodaj %s do zakupów.' % new[1],
                            'Na liście zakupów brakuje pozycji: %s. Dodaj ją.' % new[0]])
            answer = 'Dopisałam %s do [[Zakupy]].' % new[0]
        else:
            ask = self.say(['Add %s to the shopping list.' % new[3], 'Put %s on my shopping list.' % new[3],
                            "We're out of %s — stick it on the shopping list." % new[3]])
            answer = 'Added %s to [[Shopping]].' % new[3]
        self.task('zakupy-dopisz' if pl else 'shopping-add', 'append', ask,
                  expect={'files': [{'path': page.path, 'contains': [keys(new)[0]] + kept}], 'toolsNotCalled': ['write_page']},
                  solution=[{'call': 'append_to_page', 'input': {'name': page.name, 'markdown': '- [ ] ' + name(new)}},
                            {'answer': answer}])
        # Two things added.
        a, b = extra[1], extra[2]
        if pl:
            ask = self.say(['Dodaj do zakupów %s i %s.' % (a[1], b[1]), 'Dopisz na listę zakupów: %s, %s.' % (a[0], b[0]),
                            'Trzeba kupić %s oraz %s, wpisz to na listę zakupów.' % (a[1], b[1])])
            answer = 'Dopisałam do [[Zakupy]]: %s i %s.' % (a[0], b[0])
        else:
            ask = self.say(['Add %s and %s to the shopping list.' % (a[3], b[3]), 'Put these on the shopping list: %s, %s.' % (a[3], b[3]),
                            'I need %s as well as %s, write them on the shopping list.' % (a[3], b[3])])
            answer = 'Added %s and %s to [[Shopping]].' % (a[3], b[3])
        self.task('zakupy-dwie' if pl else 'shopping-two', 'append', ask,
                  expect={'files': [{'path': page.path, 'contains': [keys(a)[0], keys(b)[0]] + kept}],
                          'toolsNotCalled': ['write_page']},
                  solution=[{'call': 'append_to_page', 'input': {'name': page.name,
                                                                 'markdown': '- [ ] %s\n- [ ] %s' % (name(a), name(b))}},
                            {'answer': answer}])
        # What is left to buy.
        asks = (['Co mam na liście zakupów?', 'Co muszę kupić?', 'Czego jeszcze nie kupiłam z listy?' if self.female else
                 'Czego jeszcze nie kupiłem z listy?'] if pl else
                ["What's on my shopping list?", 'What do I still need to buy?', "What haven't I bought yet from the list?"])
        self.task('zakupy-lista' if pl else 'shopping-list', 'read', self.say(asks),
                  expect={'answer': {'anyOf': [keys(s) for s in to_buy]}},
                  solution=[{'call': 'read_page', 'input': {'name': page.name}},
                            {'answer': ('Do kupienia: ' if pl else 'Still to buy: ') + ', '.join(name(s) for s in to_buy) + '.'}])
        # Bought: ticked off.
        s = to_buy[-1]
        i = stock.index(s)
        if pl:
            ask = self.say(['%s już %s, odhacz to na liście.' % (self.g('Kupiłam', 'Kupiłem'), s[1]),
                            'Odhacz %s na liście zakupów.' % s[1],
                            'Mam już %s — zaznacz jako kupione.' % s[1]])
            answer = 'Odhaczyłam %s w [[Zakupy]].' % s[0]
        else:
            ask = self.say(['I bought the %s, tick it off.' % s[3], 'Tick off %s on the shopping list.' % s[3],
                            'Got the %s — mark it as bought.' % s[3]])
            answer = 'Ticked off %s in [[Shopping]].' % s[3]
        self.task('zakupy-kupione' if pl else 'shopping-bought', 'update', ask,
                  expect={'files': [{'path': page.path, 'matches': [r'^- \[x\] %s' % re.escape(name(s))]}]},
                  solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks "%s"' % name(s)}},
                            {'call': 'update_block', 'input': {'ref': '{{ref %s#L%d}}' % (page.path, lines[i]),
                                                                'status': 'done'}},
                            {'answer': answer}])
        self.shopping_page = page
        self.to_buy = to_buy
        self.extra = extra
        self.shopping_lines = {name(s): lines[i] for i, s in enumerate(stock)}

    def recipes(self):
        r = self.rng
        pl = self.pl()
        pool = PL_RECIPES if pl else EN_RECIPES
        folder = 'Przepisy' if pl else 'Recipes'
        chosen = r.sample(pool, 2)
        for name, gen, acc, serves, ingredients, steps in chosen:
            page = self.page('%s/%s.md' % (folder, name), name)
            page.add(('Porcja: %d osoby' if pl and serves < 5 else 'Porcja: %d osób' if pl else 'Serves %d') % serves)
            page.add('')
            page.add('Składniki:' if pl else 'Ingredients:')
            for amount, unit, what, _gen, _keys in ingredients:
                page.add('- %s%s %s' % (amount, (' ' + unit) if unit else '', what) if unit else '- %d %s' % (amount, what))
            page.add('')
            page.add('Przygotowanie:' if pl else 'Method:')
            for n, step in enumerate(steps, 1):
                page.add('%d. %s' % (n, step))
        name, gen, acc, serves, ingredients, steps = chosen[0]
        asks = (['Jakie składniki są potrzebne do %s?' % gen, 'Co jest potrzebne do %s?' % gen,
                 'Wypisz mi składniki na %s.' % acc]
                if pl else
                ['What do I need for the %s?' % gen, 'Which ingredients go into the %s?' % gen,
                 'List the ingredients for %s.' % gen])
        listing = ', '.join(('%s %s %s' % (a, u, w)).replace('  ', ' ') for a, u, w, _g, _k in ingredients)
        self.task('skladniki' if pl else 'ingredients', 'read', self.say(asks),
                  expect={'answer': {'anyOf': [k for _a, _u, _w, _g, k in ingredients[:3]]}},
                  solution=[{'call': 'read_page', 'input': {'name': name}},
                            {'answer': ('Do %s: %s ([[%s]]).' if pl else 'For the %s: %s ([[%s]]).') % (gen, listing, name)}])
        # How much of one ingredient for more people.
        amount, unit, what, ing_gen, keys = r.choice([i for i in ingredients if i[1]])
        factor = r.choice([f for f in (1.5, 2, 2.5, 3) if (serves * f) == int(serves * f) and (amount * f) == int(amount * f)])
        people = int(serves * factor)
        need = int(amount * factor)
        if pl:
            ask = self.say(['Ile %s potrzebuję do %s dla %d osób?' % (ing_gen, gen, people),
                            'Robię %s dla %d osób. Ile wziąć %s?' % (acc, people, ing_gen),
                            'Na %d osób: ile %s trzeba do %s?' % (people, ing_gen, gen)])
            answer = 'Przepis jest na %d osób (%d %s %s), więc na %d osób potrzebujesz %d %s ([[%s]]).' % (
                serves, amount, unit, what, people, need, unit, name)
        else:
            ask = self.say(['How much %s do I need for the %s for %d people?' % (ing_gen, gen, people),
                            "I'm making %s for %d people. How much %s?" % (gen, people, ing_gen),
                            'For %d people, how much %s goes into the %s?' % (people, ing_gen, gen)])
            answer = 'The recipe serves %d (%d %s of %s), so for %d people you need %d %s ([[%s]]).' % (
                serves, amount, unit, what, people, need, unit, name)
        self.task('przelicz' if pl else 'scale', 'multi', ask,
                  expect={'answer': {'matches': [number_regex(need)]}},
                  solution=[{'call': 'read_page', 'input': {'name': name}}, {'answer': answer}])
        # The same question with the recipe open: its text goes along.
        if r.random() < 0.5:
            name2, _gen2, _acc2, _serves2, ingredients2, _steps2 = chosen[1]
            amount, unit, what, ing_gen, keys = r.choice([i for i in ingredients2 if i[1]])
            asks = (['Ile tu jest %s?' % ing_gen, 'Ile %s idzie do tego przepisu?' % ing_gen,
                     'Jaka ilość %s jest w tym przepisie?' % ing_gen]
                    if pl else
                    ['How much %s is in this?' % ing_gen, 'How much %s does this recipe take?' % ing_gen,
                     'What amount of %s is in this recipe?' % ing_gen])
            self.task('ten-przepis' if pl else 'this-recipe', 'read', self.say(asks), page='%s/%s' % (folder, name2),
                      expect={'answer': {'matches': [number_regex(amount)]}},
                      solution=[{'answer': ('%d %s %s.' % (amount, unit, what)).replace('  ', ' ')}])
        # The conversation goes on: an ingredient onto the shopping list.
        amount, unit, what, ing_gen, keys = ingredients[-1]
        # (Only when the shopping list does not name it already.)
        if r.random() < 0.6 and not any(fold(k) in fold(self.shopping_page.text()) for k in keys):
            if True:
                page = self.shopping_page
                said = ('Do %s potrzebujesz: %s ([[%s]]).' if pl else 'For the %s you need: %s ([[%s]]).') % (gen, listing, name)
                then = self.say(['Dopisz to ostatnie do zakupów.', 'Dodaj ostatni składnik do listy zakupów.',
                                 'Wpisz ostatni z tych składników na listę zakupów.'] if pl else
                                ['Add the last one to the shopping list.', 'Put the last ingredient on my shopping list.',
                                 'Write the last of those on the shopping list.'])
                first = 'Jakie składniki są potrzebne do %s?' % gen if pl else 'What do I need for the %s?' % gen
                self.task('dopisz-skladnik' if pl else 'add-ingredient', 'append',
                          messages=[{'role': 'user', 'content': first}, {'role': 'assistant', 'content': said},
                                    {'role': 'user', 'content': then}],
                          expect={'files': [{'path': page.path, 'contains': [keys[0]] + [self.to_buy[0][2 if pl else 4][0]]}],
                                  'toolsNotCalled': ['write_page']},
                          solution=[{'call': 'append_to_page', 'input': {'name': page.name, 'markdown': '- [ ] %s' % what}},
                                    {'answer': ('Dopisałam %s do [[%s]].' if pl else 'Added %s to [[%s]].') % (what, page.name)}])

    def books(self):
        r = self.rng
        pl = self.pl()
        pool = PL_BOOKS if pl else EN_BOOKS
        page = self.page('Książki.md' if pl else 'Reading list.md', 'Książki' if pl else 'Reading list')
        chosen = r.sample(pool, 6)
        statuses = ['x', 'x', '/', ' ', ' ', ' ']
        lines = []
        for (title, author), status in zip(chosen, statuses):
            lines.append(page.add('- [%s] „%s” — %s' % (status, title, author) if pl else
                                  '- [%s] "%s" by %s' % (status, title, author)))
        reading = chosen[2]
        self.books_page, self.books_chosen, self.reading = page, chosen, reading
        asks = (['Jaką książkę teraz czytam?', 'Co aktualnie czytam?', 'Która książka jest teraz w trakcie czytania?']
                if pl else
                ['Which book am I reading now?', 'What am I currently reading?', 'Which book is in progress at the moment?'])
        self.task('czytam' if pl else 'reading', 'read', self.say(asks),
                  expect={'answer': {'contains': [reading[0]]}},
                  solution=[{'call': 'read_page', 'input': {'name': page.name}},
                            {'answer': ('Teraz czytasz „%s” (%s).' if pl else 'You are reading "%s" by %s.') % reading}])
        title, author = r.choice(chosen)
        surname = author.split()[-1]
        asks = (['Kto napisał „%s”?' % title, 'Jakiego autora jest „%s”?' % title, 'Czyja jest książka „%s”?' % title]
                if pl else
                ['Who wrote "%s"?' % title, 'Who is the author of "%s"?' % title, 'Whose book is "%s"?' % title])
        self.task('autor' if pl else 'author', 'search', self.say(asks), expect={'answer': {'contains': [surname]}},
                  solution=[{'call': 'search_notes', 'input': {'query': title}},
                            {'answer': ('„%s” napisał(a) %s ([[%s]]).' if pl else '"%s" is by %s ([[%s]]).') % (title, author, page.name)}])
        # Finished: the one being read.
        if pl:
            ask = self.say(['%s „%s”, oznacz ją jako przeczytaną.' % (self.g('Skończyłam', 'Skończyłem'), reading[0]),
                            'Odhacz „%s” na liście książek.' % reading[0],
                            '„%s” przeczytana — zaznacz to.' % reading[0]])
        else:
            ask = self.say(['I finished "%s", mark it as read.' % reading[0], 'Tick off "%s" on the reading list.' % reading[0],
                            '"%s" is done — please mark it.' % reading[0]])
        self.task('przeczytana' if pl else 'finished', 'update', ask,
                  expect={'files': [{'path': page.path, 'matches': [r'^- \[x\] .*%s' % re.escape(reading[0])]}]},
                  solution=[{'call': 'query_notes', 'input': {'query': 'type:tasks "%s"' % reading[0]}},
                            {'call': 'update_block', 'input': {'ref': '{{ref %s#L%d}}' % (page.path, lines[2]),
                                                                'status': 'done'}},
                            {'answer': ('Oznaczyłam „%s” jako przeczytaną.' if pl else 'Marked "%s" as read.') % reading[0]}])
        # One more to read.
        new = [b for b in pool if b not in chosen][0]
        if pl:
            ask = self.say(['Dodaj „%s” (%s) do listy książek.' % new, 'Dopisz do książek do przeczytania „%s”.' % new[0],
                            'Chcę przeczytać „%s”, wpisz to na listę.' % new[0]])
        else:
            ask = self.say(['Add "%s" by %s to my reading list.' % new, 'Put "%s" on the reading list.' % new[0],
                            'I want to read "%s", add it to the list.' % new[0]])
        self.task('nowa-ksiazka' if pl else 'new-book', 'append', ask,
                  expect={'files': [{'path': page.path, 'contains': [new[0], chosen[3][0]]}], 'toolsNotCalled': ['write_page']},
                  solution=[{'call': 'append_to_page', 'input': {'name': page.name, 'markdown':
                                                                 ('- [ ] „%s” — %s' if pl else '- [ ] "%s" by %s') % new}},
                            {'answer': ('Dopisałam „%s” do [[%s]].' if pl else 'Added "%s" to [[%s]].') % (new[0], page.name)}])

    def journals(self):
        r = self.rng
        pl = self.pl()
        days = sorted(r.sample(range(1, 7), r.randrange(3, 6)))
        spent = []
        runs = []
        events = []
        film_name, film_by, film_female = None, None, False
        visit = None
        for offset in days:
            day = self.day(-offset)
            j = self.journal(day)
            for _ in range(r.randrange(1, 4)):
                kind = r.choice(['shop', 'shop', 'run', 'film', 'visit', 'note'])
                if kind == 'shop':
                    amount = r.randrange(800, 25000) / 100
                    shop = r.choice(PL_SHOPS if pl else EN_SHOPS)
                    j.add('- Zakupy w %s: %s' % (shop, pl_amount(amount)) if pl else '- Shopping at %s: %s' % (shop, en_amount(amount)))
                    spent.append(amount)
                elif kind == 'run':
                    km = r.randrange(3, 13)
                    minutes = km * r.randrange(5, 8)
                    j.add('- %s %d km w %d minut.' % (self.g('Przebiegłam', 'Przebiegłem'), km, minutes) if pl else
                          '- Ran %d km in %d minutes.' % (km, minutes))
                    runs.append(km)
                elif kind == 'film' and film_name is None:
                    first, _s, female = self.person()
                    film_name = r.choice(PL_FILMS if pl else EN_FILMS)
                    film_by, film_female = first, female
                    j.add('- %s %s mi film „%s”.' % (first, 'poleciła' if female else 'polecił', film_name) if pl else
                          '- %s recommended the film "%s".' % (first, film_name))
                elif kind == 'visit' and visit is None:
                    visit = day
                    j.add('- Wizyta u dentystki: plomba, następna kontrola za pół roku.' if pl else
                          '- Dentist appointment: one filling, next check-up in six months.')
                else:
                    j.add(r.choice(['- Spokojny dzień, dużo czytania.', '- Długi spacer z psem.', '- Porządki w szafie.']
                                   if pl else ['- Quiet day, lots of reading.', '- Long walk with the dog.', '- Tidied the wardrobe.']))
        if len(spent) >= 2:
            total = round(sum(spent), 2)
            asks = (['Ile %s na zakupy w ostatnim tygodniu według dziennika?' % self.g('wydałam', 'wydałem'),
                     'Policz, ile w sumie poszło na zakupy w ostatnich 7 dniach.',
                     'Jaka jest suma moich zakupów z dziennika z ostatniego tygodnia?']
                    if pl else
                    ['How much did I spend on shopping in the last week, according to the journal?',
                     'Add up what went on shopping in the last 7 days.',
                     "What's the total of my shopping in the journal over the past week?"])
            self.task('wydatki' if pl else 'spending', 'multi', self.say(asks),
                      expect={'answer': {'matches': [number_regex(total, 2)]}},
                      solution=[{'call': 'query_notes', 'input': {'query': 'type:blocks date:-7d..today'}},
                                {'answer': ('W ostatnim tygodniu: %s (%s).' if pl else 'In the last week: %s (%s).') % (
                                    pl_amount(total) if pl else en_amount(total),
                                    ' + '.join((pl_amount(s) if pl else en_amount(s)) for s in spent))}])
        if len(runs) >= 2:
            total = sum(runs)
            asks = (['Ile kilometrów %s w ostatnim tygodniu?' % self.g('przebiegłam', 'przebiegłem'),
                     'Policz moje kilometry z biegania z ostatnich 7 dni.', 'Ile w sumie wyszło biegania przez ostatni tydzień?']
                    if pl else
                    ['How many kilometres did I run in the last week?', 'Add up my running distance over the last 7 days.',
                     'What was my total running for the past week?'])
            self.task('bieganie' if pl else 'running', 'multi', self.say(asks),
                      expect={'answer': {'matches': [number_regex(total)]}},
                      solution=[{'call': 'search_notes', 'input': {'query': 'km' if not pl else 'przebiegł km'}},
                                {'answer': ('Razem %d km (%s).' if pl else '%d km in total (%s).') % (total, ' + '.join(map(str, runs)))}])
        if film_name:
            asks = (['Jaki film polecił%s mi %s?' % ('a' if film_female else '', film_by),
                     'Co %s mi %s do obejrzenia?' % (film_by, 'poleciła' if film_female else 'polecił'),
                     'Przypomnij mi tytuł filmu od %s.' % film_by] if pl else
                    ['Which film did %s recommend?' % film_by, 'What did %s tell me to watch?' % film_by,
                     'Remind me of the film %s suggested.' % film_by])
            self.task('film', 'search', self.say(asks), expect={'answer': {'contains': [film_name]}},
                      solution=[{'call': 'search_notes', 'input': {'query': film_by}},
                                {'answer': ('„%s”.' if pl else '"%s".') % film_name}])
        if visit:
            asks = (['Kiedy %s u dentystki?' % self.g('byłam ostatnio', 'byłem ostatnio'), 'Kiedy była ostatnia wizyta u dentystki?',
                     'Którego dnia miałam wizytę u dentystki?' if self.female else 'Którego dnia miałem wizytę u dentystki?']
                    if pl else
                    ['When did I last see the dentist?', 'When was my last dentist appointment?', 'On which day was I at the dentist?'])
            self.task('dentysta' if pl else 'dentist', 'search', self.say(asks),
                      expect={'answer': {'matches': [date_regex(self.lang, visit)]}},
                      solution=[{'call': 'search_notes', 'input': {'query': 'dentystki' if pl else 'dentist'}},
                                {'answer': ('%s ([[%s]]).' % (visit.isoformat(), visit.isoformat()))}])
        # Something noted down today.
        today = 'journals/%s.md' % self.today.isoformat()
        km = r.randrange(3, 15)
        logs = ([(['Zapisz w dzienniku, że %s %d km.' % (self.g('przebiegłam', 'przebiegłem'), km),
                   'Zanotuj na dziś: bieganie %d km.' % km, 'Dopisz do dzisiejszego dziennika %d km biegania.' % km],
                  {'matches': [r'(^|[^0-9])%d ?km' % km]}, '- %s %d km.' % (self.g('Przebiegłam', 'Przebiegłem'), km)),
                 (['Zapisz w dzienniku, że %s na basenie.' % self.g('byłam', 'byłem'), 'Zanotuj na dziś: basen.',
                   'Dopisz do dzisiejszego dziennika, że %s pływać.' % self.g('poszłam', 'poszedłem')],
                  # (Of pływać or płynąć, any form: "Płynęłam." keeps the meaning.)
                  {'anyOf': [['basen', 'pływ', 'płyn']]}, '- Basen.')]
                if pl else
                [(['Note in the journal that I ran %d km.' % km, 'Log a %d km run for today.' % km,
                   "Add %d km of running to today's journal." % km],
                  {'matches': [r'(^|[^0-9])%d ?(km|kilomet)' % km]}, '- Ran %d km.' % km),
                 (['Note in the journal that I went swimming.', 'Log a swim for today.', "Add a swim to today's journal."],
                  {'anyOf': [['swim', 'swam']]}, '- Went swimming.')])
        asks, check, markdown = r.choice(logs)
        self.task('dziennik' if pl else 'journal', 'journal', self.say(asks),
                  expect={'files': [dict(path=today, **check)]},
                  solution=[{'call': 'append_to_journal', 'input': {'markdown': markdown}},
                            {'answer': 'Zapisane w dzisiejszym dzienniku.' if pl else "Noted in today's journal."}])

    def reminders(self):
        r = self.rng
        pl = self.pl()
        today_path = 'journals/%s.md' % self.today.isoformat()
        whats = ([('zadzwonić do banku', 'bank'), ('odebrać garnitur z pralni' if not self.female else 'odebrać sukienkę z pralni', 'pralni'),
                  ('podlać kwiaty', 'kwiat'), ('wystawić śmieci', 'śmieci'), ('zapłacić za przedszkole', 'przedszkol'),
                  ('zamówić taksówkę na lotnisko', 'taksówk'), ('wziąć leki', 'lek')]
                 if pl else
                 [('call the bank', 'bank'), ('pick up the suit from the cleaners', 'suit'), ('water the plants', 'plant'),
                  ('put the bins out', 'bins'), ('pay for nursery', 'nursery'), ('book a taxi to the airport', 'taxi'),
                  ('take my pills', 'pill')])
        for what, key in r.sample(whats, 2):
            way = r.randrange(0, 4)
            hour, minute = r.randrange(7, 21), r.choice([0, 0, 30, 15])
            if way == 0:
                day, when_pl, when_en = self.day(1), 'jutro', 'tomorrow'
            elif way == 1:
                day, when_pl, when_en = self.day(2), 'pojutrze', 'the day after tomorrow'
            elif way == 2:
                ahead = r.randrange(2, 7)
                day = self.day(ahead)
                when_pl, when_en = PL_ON_WEEKDAY[day.weekday()], 'on ' + EN_WEEKDAYS[day.weekday()]
            else:
                ahead = r.randrange(3, 20)
                day = self.day(ahead)
                when_pl, when_en = pl_day(day), 'on ' + en_day(day)
            clock_pl = '%d:%02d' % (hour, minute)
            if minute == 0:
                clock_en = '%d %s' % (hour % 12 or 12, 'am' if hour < 12 else 'pm')
            else:
                clock_en = '%d:%02d %s' % (hour % 12 or 12, minute, 'am' if hour < 12 else 'pm')
            if pl:
                ask = self.say(['Przypomnij mi %s o %s, żeby %s.' % (when_pl, clock_pl, what),
                                'Ustaw przypomnienie: %s, %s o %s.' % (what, when_pl, clock_pl),
                                '%s o %s muszę %s — przypomnij mi.' % (when_pl.capitalize(), clock_pl, what)])
            else:
                ask = self.say(['Remind me %s at %s to %s.' % (when_en, clock_en, what),
                                'Set a reminder to %s %s at %s.' % (what, when_en, clock_en),
                                'I have to %s %s at %s — remind me.' % (what, when_en, clock_en)])
            stamp = '⏰ %s %02d:%02d' % (day.isoformat(), hour, minute)
            paths = [today_path, 'journals/%s.md' % day.isoformat()]
            # (The reminder and what it is for on one line: a bare "⏰ …" line rings with no text.)
            self.task('przypomnij' if pl else 'remind', 'journal', ask,
                      expect=self.with_day({'files': [{'path': paths, 'contains': [key],
                                                       'matches': [r'^(?=.*%s).*%s' % (loose(key), re.escape(stamp))]}]}, day),
                      solution=[{'call': 'append_to_journal', 'input': {'markdown': '- [ ] %s %s' % (what[0].upper() + what[1:], stamp)}},
                                {'answer': ('Ustawiłam przypomnienie na %s %02d:%02d.' if pl else 'Reminder set for %s at %02d:%02d.') % (
                                    day.isoformat(), hour, minute)}])
        # A task with a due date onto a page.
        thing = r.choice([('Zamówić tort na urodziny', 'tort', 'Order the birthday cake', 'cake'),
                          ('Odebrać wyniki badań', 'wynik', 'Collect the test results', 'result'),
                          ('Wymienić baterię w zegarku', 'bateri', 'Replace the watch battery', 'battery'),
                          ('Kupić bilety na koncert', 'bilet', 'Buy the concert tickets', 'ticket')])
        due = self.day(r.randrange(3, 25))
        page = self.home
        text, key = (thing[0], thing[1]) if pl else (thing[2], thing[3])
        quoted = text[0].lower() + text[1:]
        if pl:
            ask = self.say(['Dodaj do strony %s zadanie: %s do %s.' % (page.name, quoted, pl_day(due)),
                            'Dopisz w [[%s]] zadanie „%s” z terminem %s.' % (page.name, quoted, pl_day(due)),
                            'Na stronie %s brakuje zadania: %s, termin %s. Dodaj je.' % (page.name, quoted, due.isoformat())])
        else:
            ask = self.say(['Add a task to the %s page: %s by %s.' % (page.name, quoted, en_day(due)),
                            'Put "%s" on [[%s]], due %s.' % (quoted, page.name, en_day(due)),
                            'The %s page is missing a task: %s, due %s. Add it.' % (page.name, quoted, due.isoformat())])
        self.task('zadanie-na-strone' if pl else 'task-on-page', 'append', ask,
                  expect=self.with_day({'files': [{'path': page.path, 'matches': [r'^- \[ \] (?=.*%s)(?=.*📅 %s)' % (loose(key), due.isoformat())],
                                                   'contains': [self.items[0].keys[0]]}],
                                        'toolsNotCalled': ['write_page']}, due),
                  solution=[{'call': 'append_to_page', 'input': {'name': page.name, 'markdown': '- [ ] %s 📅 %s' % (text, due.isoformat())}},
                            {'answer': ('Dodałam „%s” z terminem %s do [[%s]].' if pl else 'Added "%s", due %s, to [[%s]].') % (
                                text, due.isoformat(), page.name)}])

    def write_page(self):
        r = self.rng
        pl = self.pl()
        # (The things, and a word of each the page must hold.)
        options = ([('Pomysły na prezenty', ['szalik', 'puzzle', 'książka kucharska'], ['szalik', 'puzzle', 'kucharsk']),
                    ('Lista gości', ['Ola', 'Bartek', 'ciocia Ela'], ['Ola', 'Bartek', 'Ela']),
                    ('Plan wycieczki do Gdańska', ['Westerplatte', 'Długi Targ', 'molo w Sopocie'], ['Westerplatte', 'Długi Targ', 'Sopo'])]
                   if pl else
                   [('Gift ideas', ['scarf', 'jigsaw', 'cookbook'], ['scarf', 'jigsaw', 'cookbook']),
                    ('Guest list', ['Ola', 'Ben', 'Aunt Jo'], ['Ola', 'Ben', 'Jo']),
                    ('Edinburgh trip', ['the castle', "Arthur's Seat", 'the Royal Mile'], ['castle', 'Arthur', 'Royal Mile'])])
        title, things, words = r.choice(options)
        if pl:
            ask = self.say(['Utwórz stronę „%s” z listą: %s, %s i %s.' % ((title,) + tuple(things)),
                            'Załóż notatkę %s, a w niej: %s, %s, %s.' % ((title,) + tuple(things)),
                            'Zrób nową stronę „%s” i wpisz na nią %s, %s oraz %s.' % ((title,) + tuple(things))])
        else:
            ask = self.say(['Create a page called "%s" with a list: %s, %s and %s.' % ((title,) + tuple(things)),
                            'Make a note %s with %s, %s, %s in it.' % ((title,) + tuple(things)),
                            'Start a new page "%s" and put %s, %s and %s on it.' % ((title,) + tuple(things))])
        self.task('nowa-strona' if pl else 'new-page', 'write', ask,
                  expect={'files': [{'path': title + '.md', 'contains': words}]},
                  solution=[{'call': 'write_page', 'input': {'name': title, 'content': '# %s\n\n%s\n' % (
                      title, '\n'.join('- ' + t for t in things))}},
                            {'answer': ('Utworzyłam [[%s]].' if pl else 'Created [[%s]].') % title}])

    def no_tool(self):
        r = self.rng
        pl = self.pl()
        a, b = r.randrange(12, 40), r.randrange(3, 13)
        if pl:
            options = [
                (['Przetłumacz na angielski: „Jutro jadę do pracy pociągiem.”', 'Jak po angielsku powiedzieć: „Jutro jadę do pracy pociągiem”?',
                  'Przełóż na angielski zdanie: „Jutro jadę do pracy pociągiem.”'],
                 {'contains': ['train', 'tomorrow']}, 'Tomorrow I am going to work by train.'),
                (['Ile to jest %d razy %d?' % (a, b), 'Policz %d × %d.' % (a, b), 'Pomnóż %d przez %d.' % (a, b)],
                 {'matches': [number_regex(a * b)]}, '%d × %d = %d.' % (a, b, a * b)),
                (['Napisz krótką wiadomość, że spóźnię się 15 minut na spotkanie.', 'Zredaguj SMS-a: będę 15 minut później na spotkaniu.',
                  'Przygotuj krótką informację, że przyjdę na spotkanie 15 minut po czasie.'],
                 {'matches': [r'(^|[^0-9])15([^0-9]|$)|piętnaście']},
                 'Przepraszam, spóźnię się na spotkanie około 15 minut.'),
                (['Ile dni ma luty w roku przestępnym?', 'Ile dni liczy luty w roku przestępnym?', 'Luty w roku przestępnym ma ile dni?'],
                 {'matches': [r'(^|[^0-9])29([^0-9]|$)']}, 'W roku przestępnym luty ma 29 dni.'),
                (['Podaj synonim słowa „szybki”.', 'Jakim słowem zastąpić „szybki”?', 'Wymyśl inne słowo na „szybki”.'],
                 {'anyOf': [['prędk', 'żwaw', 'błyskawiczn', 'chyż', 'rącz', 'ekspresow', 'bystr', 'sprawn', 'pospieszn', 'natychmiast']]},
                 'Na przykład: prędki, żwawy, błyskawiczny.'),
                (['Ile minut ma %d,5 godziny?' % b, 'Zamień %d,5 godziny na minuty.' % b, 'Ile to minut: %d i pół godziny?' % b],
                 {'matches': [number_regex(b * 60 + 30)]}, '%d,5 godziny to %d minut.' % (b, b * 60 + 30)),
            ]
        else:
            options = [
                (['Translate into Spanish: "Good morning, how are you?"', 'How do I say "Good morning, how are you?" in Spanish?',
                  'Put "Good morning, how are you?" into Spanish.'],
                 {'anyOf': [['buenos días', 'buenos dias'], ['cómo está', 'como esta']]}, 'Buenos días, ¿cómo estás?'),
                (['What is %d times %d?' % (a, b), 'Work out %d × %d.' % (a, b), 'Multiply %d by %d.' % (a, b)],
                 {'matches': [number_regex(a * b)]}, '%d × %d = %d.' % (a, b, a * b)),
                (["Write a short message saying I'll be 15 minutes late for the meeting.",
                  "Draft a text: I'm running 15 minutes late for the meeting.",
                  "Compose a quick note that I'll arrive at the meeting 15 minutes late."],
                 {'matches': [r'(^|[^0-9])15([^0-9]|$)|fifteen']}, "Sorry, I'm running about 15 minutes late for the meeting."),
                (['How many days does February have in a leap year?', 'In a leap year, how long is February?',
                  'February in a leap year has how many days?'],
                 {'matches': [r'(^|[^0-9])29([^0-9]|$)|twenty-nine']}, 'February has 29 days in a leap year.'),
                (['Give me a synonym for "quick".', 'What word could I use instead of "quick"?', 'Suggest another word for "quick".'],
                 {'anyOf': [['fast', 'rapid', 'swift', 'speedy', 'brisk', 'prompt', 'hasty', 'nimble', 'fleet']]},
                 'For example: fast, rapid, swift.'),
                (['How many minutes are there in %d.5 hours?' % b, 'Convert %d.5 hours into minutes.' % b,
                  'How many minutes is %d and a half hours?' % b],
                 {'matches': [number_regex(b * 60 + 30)]}, '%d.5 hours is %d minutes.' % (b, b * 60 + 30)),
            ]
        for asks, check, answer in r.sample(options, 3):
            self.task('bez-narzedzi' if pl else 'no-tool', 'no_tool', self.say(asks),
                      expect={'answer': check, 'noTools': True}, solution=[{'answer': answer}])

    def clarify(self):
        r = self.rng
        pl = self.pl()
        asks = (['Przesuń to na jutro.', 'Odhacz to.', 'Usuń to z listy.', 'Zmień termin na piątek.', 'Dopisz to tam, gdzie zwykle.']
                if pl else
                ['Move it to tomorrow.', 'Tick it off.', 'Remove that from the list.', 'Change the date to Friday.',
                 'Put it where it usually goes.'])
        for ask in ([asks[-1]] if self.split == 'test' else r.sample(asks[:-1], 2)):
            self.task('dopytaj' if pl else 'clarify', 'clarify', ask,
                      note='Nothing says what "it" is: ask, change nothing.',
                      expect={'answer': {'anyOf': [PL_ASK_BACK if pl else EN_ASK_BACK]}},
                      solution=[{'answer': 'O które zadanie chodzi?' if pl else 'Which one do you mean?'}])

    def decline(self):
        r = self.rng
        pl = self.pl()
        if r.random() < 0.5:
            what = r.choice(['zadzwonić do księgowej', 'kupić znaczki', 'oddać pudełko sąsiadom'] if pl else
                            ['call the accountant', 'buy stamps', 'return the box to the neighbours'])
            ask = self.say(['Dopisz do dzisiejszego dziennika: %s.' % what, 'Zanotuj na dziś: %s.' % what,
                            'Wpisz w dziennik, że trzeba %s.' % what] if pl else
                           ["Add to today's journal: %s." % what, 'Note for today: %s.' % what,
                            'Write in the journal that I need to %s.' % what])
            tool, call = 'append_to_journal', {'markdown': '- [ ] %s' % (what[0].upper() + what[1:])}
        else:
            s = self.extra[-1]
            ask = self.say(['Dodaj %s do zakupów.' % s[1], 'Dopisz %s do listy zakupów.' % s[1], 'Wpisz %s na listę zakupów.' % s[1]]
                           if pl else
                           ['Add %s to the shopping list.' % s[3], 'Put %s on the shopping list.' % s[3],
                            'Write %s on the shopping list.' % s[3]])
            tool, call = 'append_to_page', {'name': self.shopping_page.name, 'markdown': '- [ ] %s' % (s[0] if pl else s[3])}
        self.task('odmowa' if pl else 'decline', 'decline', ask, answers=['no'],
                  note='The user declines: nothing changes and the assistant does not try again (a second request would be approved).',
                  expect={'toolsCalled': [tool]},
                  solution=[{'call': tool, 'input': call},
                            {'answer': 'Dobrze, nie dopisuję.' if pl else "OK, I won't add it."}])

    # ---- output ----
    def write(self, out):
        base = os.path.join(out, 'notebooks', self.name)
        for page in self.pages.values():
            path = os.path.join(base, page.path)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(page.text())
        tasks = {'notebook': self.name, 'language': self.lang, 'now': self.now.strftime('%Y-%m-%dT%H:%M'),
                 'tasks': self.tasks}
        os.makedirs(os.path.join(out, 'tasks'), exist_ok=True)
        with open(os.path.join(out, 'tasks', self.name + '.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(tasks, f, ensure_ascii=False, indent=1)
            f.write('\n')


def main(argv):
    parser = argparse.ArgumentParser(description='Made-up notebooks and the assistant\'s tasks on them (docs/FINETUNE.md).')
    parser.add_argument('--out', required=True, help='the folder: notebooks/ and tasks/ go into it')
    parser.add_argument('--split', choices=SPLITS, required=True, help='train, or test (its own ways of asking)')
    parser.add_argument('--count', type=int, default=20, help='notebooks, half of them Polish (default 20)')
    parser.add_argument('--seed', type=int, default=1, help='the first notebook\'s seed; the next ones count on (default 1)')
    args = parser.parse_args(argv)
    total = 0
    for n in range(args.count):
        lang = 'pl' if n % 2 == 0 else 'en'
        g = Generator(lang, args.seed + n // 2, args.split).build()
        g.write(args.out)
        total += len(g.tasks)
    print('%d notebooks, %d tasks in %s' % (args.count, total, args.out))


if __name__ == '__main__':
    main(sys.argv[1:])
