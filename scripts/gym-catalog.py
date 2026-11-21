#!/usr/bin/env python3
"""Regenerate the gym's exercise catalog (src/core/extensions/builtin/exercises.json and
exercise-instructions.json; docs/SYNTAX.md §5 "Gym", docs/UI.md "Gym").

The catalog is Brulion's 51 hand-made exercises (their ids stay as they are: notes and pages
name them) and every exercise of free-exercise-db (https://github.com/yuhonas/free-exercise-db,
by Yuhonas and contributors, released into the public domain under the Unlicense), pinned to the
commit below. An upstream exercise one of the 51 stands for gives it its instructions, muscles
and level and is not listed twice. About 150 common upstream exercises get Polish names here
(POLISH); the rest keep their English names in both languages. Pictures are not bundled (about
70 MB): an exercise says how many it has (`pictures`, upstream's `<source>/0.jpg`, `1.jpg` …) and
the catalog's `source.pictures` is the address they are under at that commit — the gym shows them
from the device's cache, fetched as pictures from the internet are (docs/SYNTAX.md §2).

    curl -sSL -o /tmp/fedb.json \\
      https://raw.githubusercontent.com/yuhonas/free-exercise-db/<COMMIT>/dist/exercises.json
    python3 -I scripts/gym-catalog.py /tmp/fedb.json

Writes two files next to silownia.luau: exercises.json — {version, source, muscles, equipment,
categories, levels, exercises: [{id, pl, en, aliases, primary, secondary, equipment?,
category, level?, source?, pictures?}]} — and exercise-instructions.json — {id: [step, …]} (English), which
the gym reads only when it shows or writes one exercise. Deterministic: the same input gives
byte-identical files.
"""

import argparse
import hashlib
import json
import os
import re
import sys

COMMIT = "f00c92c7dcf1216a928a52c3706c7ce8e2f71ed5"
SHA256 = "5bb747e3fc658f095a60dcbf6d53c96627acdcc6ffb6fffde86f7e26995d40bf"

# The muscle groups (free-exercise-db's seventeen): id → (Polish, English, other names a page's
# `mięśnie::` / `muscles::` may use, folded by the gym as it reads them).
MUSCLES = {
    "abdominals": ("brzuch", "abdominals", ["mięśnie brzucha", "brzuch", "abs", "abdominal", "skośne brzucha",
                                            "obliques", "core", "prosty brzucha"]),
    "abductors": ("odwodziciele ud", "abductors", ["odwodziciele", "abductor"]),
    "adductors": ("przywodziciele ud", "adductors", ["przywodziciele", "adductor"]),
    "biceps": ("biceps", "biceps", ["dwugłowe ramienia", "bicepsy"]),
    "calves": ("łydki", "calves", ["łydka", "calf", "brzuchate łydki"]),
    "chest": ("klatka piersiowa", "chest", ["klatka", "piersiowe", "pecs", "pectorals"]),
    "forearms": ("przedramiona", "forearms", ["przedramię", "forearm"]),
    "glutes": ("pośladki", "glutes", ["pośladek", "pośladkowe", "glute"]),
    "hamstrings": ("dwugłowe uda", "hamstrings", ["dwugłowy uda", "tył uda", "tylna część uda", "hamstring",
                                                  "hamstringi"]),
    "lats": ("najszersze grzbietu", "lats", ["najszerszy grzbietu", "latissimus", "lat"]),
    "lower-back": ("dolna część pleców", "lower back", ["prostowniki grzbietu", "dół pleców", "dolny grzbiet",
                                                        "lędźwie", "erectors", "lowerback"]),
    "middle-back": ("środkowa część pleców", "middle back", ["środek pleców", "równoległoboczne", "rhomboids",
                                                             "mid back", "upper back", "górna część pleców"]),
    "neck": ("szyja", "neck", ["kark"]),
    "quadriceps": ("czworogłowe uda", "quadriceps", ["czworogłowy uda", "czworogłowe", "przód uda", "quads",
                                                     "quad"]),
    "shoulders": ("barki", "shoulders", ["bark", "naramienne", "przednie aktony barków", "boczne aktony barków",
                                         "tylne aktony barków", "delts", "deltoids", "front delts", "side delts",
                                         "rear delts", "shoulder"]),
    "traps": ("czworoboczne", "traps", ["czworoboczny", "kaptury", "trapezius", "trapezy"]),
    "triceps": ("triceps", "triceps", ["trójgłowe ramienia", "tricepsy"]),
}

# free-exercise-db's equipment → (id, Polish, English).
EQUIPMENT = {
    "body only": ("body-only", "masa ciała", "body only"),
    "machine": ("machine", "maszyna", "machine"),
    "cable": ("cable", "wyciąg", "cable"),
    "barbell": ("barbell", "sztanga", "barbell"),
    "dumbbell": ("dumbbell", "hantle", "dumbbells"),
    "kettlebells": ("kettlebell", "kettlebell", "kettlebell"),
    "e-z curl bar": ("ez-bar", "sztanga łamana", "EZ bar"),
    "bands": ("bands", "gumy oporowe", "bands"),
    "medicine ball": ("medicine-ball", "piłka lekarska", "medicine ball"),
    "exercise ball": ("exercise-ball", "piłka gimnastyczna", "exercise ball"),
    "foam roll": ("foam-roll", "roller", "foam roller"),
    "other": ("other", "inne", "other"),
}

CATEGORIES = {
    "strength": ("strength", "siłowe", "strength"),
    "powerlifting": ("powerlifting", "trójbój siłowy", "powerlifting"),
    "olympic weightlifting": ("olympic", "podnoszenie ciężarów", "olympic weightlifting"),
    "strongman": ("strongman", "strongman", "strongman"),
    "plyometrics": ("plyometrics", "pliometria", "plyometrics"),
    "cardio": ("cardio", "kardio", "cardio"),
    "stretching": ("stretching", "rozciąganie", "stretching"),
}

LEVELS = {
    "beginner": ("początkujący", "beginner"),
    "intermediate": ("średniozaawansowany", "intermediate"),
    "expert": ("zaawansowany", "expert"),
}

# Brulion's own 51 (their ids, names and order as before): the upstream exercise each stands for
# (`source`, its instructions, muscles and level), the names people write in notes (`aliases`),
# and for those without one their muscles, equipment and category.
BUILTIN = [
    ("back-squat", "Przysiad ze sztangą", "Back squat", "Barbell_Squat",
     ["Przysiad", "Przysiady", "Przysiad tylny", "Squat", "Squats", "Barbell squat"]),
    ("front-squat", "Przysiad przedni", "Front squat", "Front_Barbell_Squat",
     ["Przysiad przedni ze sztangą", "Front barbell squat"]),
    ("goblet-squat", "Przysiad goblet", "Goblet squat", "Goblet_Squat", ["Przysiad z kettlebell"]),
    ("leg-press", "Wypychanie nogami na suwnicy", "Leg press", "Leg_Press",
     ["Suwnica", "Wypychanie nogami", "Prasa nożna"]),
    ("lunge", "Wykroki", "Lunges", "Dumbbell_Lunges", ["Wykrok", "Wykroki z hantlami", "Lunge", "Dumbbell lunges"]),
    ("bulgarian-split-squat", "Przysiad bułgarski", "Bulgarian split squat", "Split_Squat_with_Dumbbells",
     ["Bułgarski przysiad", "Przysiad bułgarski z hantlami", "Bulgarian squat"]),
    ("romanian-deadlift", "Martwy ciąg rumuński", "Romanian deadlift", "Romanian_Deadlift",
     ["Rumuński martwy ciąg", "RDL"]),
    ("leg-curl", "Uginanie nóg na maszynie", "Leg curl", "Lying_Leg_Curls",
     ["Uginanie nóg", "Uginanie nóg leżąc", "Lying leg curl", "Hamstring curl"]),
    ("leg-extension", "Prostowanie nóg na maszynie", "Leg extension", "Leg_Extensions",
     ["Prostowanie nóg", "Leg extensions"]),
    ("hip-thrust", "Hip thrust", "Hip thrust", "Barbell_Hip_Thrust",
     ["Wypychanie bioder", "Unoszenie bioder ze sztangą", "Barbell hip thrust"]),
    ("calf-raise", "Wspięcia na palce", "Calf raise", "Standing_Calf_Raises",
     ["Wspięcia na palce stojąc", "Standing calf raise", "Calf raises"]),
    ("deadlift", "Martwy ciąg", "Deadlift", "Barbell_Deadlift",
     ["Martwy ciąg klasyczny", "Barbell deadlift", "Conventional deadlift"]),
    ("sumo-deadlift", "Martwy ciąg sumo", "Sumo deadlift", "Sumo_Deadlift", []),
    ("pull-up", "Podciąganie nachwytem", "Pull-up", "Pullups",
     ["Podciąganie", "Podciąganie na drążku", "Pull-ups", "Pullup", "Pullups"]),
    ("chin-up", "Podciąganie podchwytem", "Chin-up", "Chin-Up", ["Chin-ups", "Chinup"]),
    ("barbell-row", "Wiosłowanie sztangą", "Barbell row", "Bent_Over_Barbell_Row",
     ["Wiosłowanie", "Wiosłowanie sztangą w opadzie", "Bent over row", "Bent over barbell row"]),
    ("dumbbell-row", "Wiosłowanie hantlem", "Dumbbell row", "One-Arm_Dumbbell_Row",
     ["Wiosłowanie hantlem w opadzie", "Wiosłowanie jednorącz", "One-arm dumbbell row"]),
    ("lat-pulldown", "Ściąganie drążka wyciągu górnego", "Lat pulldown", "Wide-Grip_Lat_Pulldown",
     ["Ściąganie drążka", "Ściąganie drążka do klatki", "Wyciąg górny", "Pulldown"]),
    ("seated-cable-row", "Przyciąganie linki wyciągu siedząc", "Seated cable row", "Seated_Cable_Rows",
     ["Wiosłowanie na wyciągu dolnym", "Wyciąg dolny", "Cable row"]),
    ("face-pull", "Face pull", "Face pull", "Face_Pull", ["Przyciąganie liny do twarzy", "Face pulls"]),
    ("bench-press", "Wyciskanie sztangi leżąc", "Bench press", "Barbell_Bench_Press_-_Medium_Grip",
     ["Wyciskanie leżąc", "Wyciskanie na ławce płaskiej", "Ławka", "Bench", "Barbell bench press"]),
    ("incline-bench-press", "Wyciskanie sztangi na ławce skośnej", "Incline bench press",
     "Barbell_Incline_Bench_Press_-_Medium_Grip", ["Wyciskanie skośne", "Wyciskanie na skosie", "Incline bench"]),
    ("dumbbell-bench-press", "Wyciskanie hantli leżąc", "Dumbbell bench press", "Dumbbell_Bench_Press",
     ["Wyciskanie hantli", "Wyciskanie hantli na ławce płaskiej", "DB bench press"]),
    ("incline-dumbbell-press", "Wyciskanie hantli na ławce skośnej", "Incline dumbbell press", "Incline_Dumbbell_Press",
     ["Wyciskanie hantli skośne", "Wyciskanie hantli na skosie", "Incline dumbbell bench press"]),
    ("dumbbell-fly", "Rozpiętki z hantlami", "Dumbbell fly", "Dumbbell_Flyes",
     ["Rozpiętki", "Rozpiętki na ławce płaskiej", "Dumbbell flyes", "Chest fly"]),
    ("dip", "Pompki na poręczach", "Dip", "Dips_-_Chest_Version", ["Dipy", "Dips", "Parallel bar dip"]),
    ("push-up", "Pompki", "Push-up", "Pushups", ["Pompka", "Pompki klasyczne", "Push-ups", "Pushups"]),
    ("overhead-press", "Wyciskanie żołnierskie", "Overhead press", "Standing_Military_Press",
     ["OHP", "Wyciskanie nad głowę", "Wyciskanie sztangi nad głowę", "Military press", "Shoulder press"]),
    ("dumbbell-shoulder-press", "Wyciskanie hantli nad głowę", "Dumbbell shoulder press", "Dumbbell_Shoulder_Press",
     ["Wyciskanie hantli nad głowę siedząc", "Dumbbell overhead press"]),
    ("lateral-raise", "Unoszenie hantli bokiem", "Lateral raise", "Side_Lateral_Raise",
     ["Wznosy bokiem", "Unoszenie bokiem", "Wznosy hantli bokiem", "Side lateral raise", "Lateral raises"]),
    ("rear-delt-fly", "Odwrotne rozpiętki", "Rear delt fly", "Reverse_Flyes",
     ["Odwrotne rozpiętki w opadzie", "Reverse fly", "Reverse flyes"]),
    ("shrug", "Szrugsy", "Shrug", "Dumbbell_Shrug",
     ["Szrugi", "Szrugsy z hantlami", "Wzruszanie ramion", "Shrugs", "Dumbbell shrug"]),
    ("barbell-curl", "Uginanie ramion ze sztangą", "Barbell curl", "Barbell_Curl",
     ["Uginanie ramion", "Biceps ze sztangą", "Biceps curl"]),
    ("dumbbell-curl", "Uginanie ramion z hantlami", "Dumbbell curl", "Dumbbell_Bicep_Curl",
     ["Biceps z hantlami", "Dumbbell biceps curl"]),
    ("hammer-curl", "Uginanie młotkowe", "Hammer curl", "Hammer_Curls", ["Młotki", "Hammer curls"]),
    ("triceps-pushdown", "Prostowanie ramion na wyciągu", "Triceps pushdown", "Triceps_Pushdown",
     ["Prostowanie ramion na wyciągu górnym", "Triceps na wyciągu", "Pushdown", "Tricep pushdown"]),
    ("skull-crusher", "Wyciskanie francuskie", "Skull crusher", "Lying_Triceps_Press",
     ["Francuskie wyciskanie", "Wyciskanie francuskie leżąc", "Lying triceps extension", "Skullcrusher"]),
    ("close-grip-bench-press", "Wyciskanie wąskim chwytem", "Close-grip bench press", "Close-Grip_Barbell_Bench_Press",
     ["Wyciskanie wąsko", "Wyciskanie sztangi wąskim chwytem", "CGBP"]),
    ("plank", "Deska", "Plank", "Plank", ["Deska przodem", "Front plank"]),
    ("side-plank", "Deska bokiem", "Side plank", "Side_Bridge", ["Plank bokiem", "Side bridge"]),
    ("hanging-leg-raise", "Unoszenie nóg w zwisie", "Hanging leg raise", "Hanging_Leg_Raise",
     ["Unoszenie nóg na drążku", "Hanging leg raises"]),
    ("crunch", "Spięcia brzucha", "Crunch", "Crunches", ["Brzuszki", "Crunches"]),
    ("russian-twist", "Russian twist", "Russian twist", "Russian_Twist", ["Skręty tułowia siedząc"]),
    ("ab-wheel", "Kółko do brzucha", "Ab wheel rollout", "Ab_Roller", ["Kółko", "Roller do brzucha", "Ab roller", "Ab wheel"]),
    ("kettlebell-swing", "Swing kettlebell", "Kettlebell swing", None,
     ["Swing", "Swingi", "Wymachy kettlebell", "KB swing"],
     {"primary": ["glutes", "hamstrings"], "secondary": ["lower-back", "shoulders", "abdominals"],
      "equipment": "kettlebell", "category": "strength", "level": "beginner"}),
    ("clean", "Zarzut", "Power clean", "Power_Clean", ["Zarzut siłowy", "Power clean", "Clean"]),
    ("burpee", "Burpees", "Burpee", None, ["Burpee", "Padnij-powstań"],
     {"primary": ["quadriceps", "chest"], "secondary": ["shoulders", "triceps", "abdominals", "glutes"],
      "equipment": "body-only", "category": "plyometrics", "level": "beginner"}),
    ("running", "Bieg", "Running", None, ["Bieganie", "Run", "Jogging"],
     {"primary": ["quadriceps"], "secondary": ["calves", "glutes", "hamstrings"], "category": "cardio",
      "level": "beginner"}),
    ("rowing", "Wiosłowanie na ergometrze", "Rowing", "Rowing_Stationary",
     ["Ergometr", "Ergometr wioślarski", "Wioślarz", "Rowing machine", "Erg"]),
    ("cycling", "Jazda na rowerze", "Cycling", "Bicycling", ["Rower", "Rowerek", "Bike", "Bicycling"]),
    ("jump-rope", "Skakanka", "Jump rope", "Rope_Jumping", ["Skakanie na skakance", "Skipping rope", "Rope jumping"]),
]

# Polish names of common upstream exercises (upstream id → name, aliases).
POLISH = {
    # legs
    "Barbell_Full_Squat": ("Przysiad pełny ze sztangą", ["Przysiad głęboki"]),
    "Box_Squat": ("Przysiad do skrzyni", []),
    "Hack_Squat": ("Przysiad hack na maszynie", ["Hack przysiad", "Hack squat"]),
    "Barbell_Hack_Squat": ("Przysiad hack ze sztangą", []),
    "Smith_Machine_Squat": ("Przysiad na maszynie Smitha", ["Przysiad na Smithcie"]),
    "Bodyweight_Squat": ("Przysiad bez obciążenia", ["Przysiady bez obciążenia", "Air squat"]),
    "Dumbbell_Squat": ("Przysiad z hantlami", []),
    "Overhead_Squat": ("Przysiad ze sztangą nad głową", ["OHS"]),
    "Zercher_Squats": ("Przysiad Zerchera", ["Zercher squat"]),
    "Wide_Stance_Barbell_Squat": ("Przysiad szeroki ze sztangą", []),
    "Plie_Dumbbell_Squat": ("Przysiad sumo z hantlem", ["Przysiad sumo"]),
    "Kettlebell_Pistol_Squat": ("Pistolet z kettlebell", ["Przysiad na jednej nodze", "Pistol squat"]),
    "Front_Squats_With_Two_Kettlebells": ("Przysiad przedni z dwoma kettlebell", []),
    "Barbell_Lunge": ("Wykroki ze sztangą", []),
    "Barbell_Walking_Lunge": ("Wykroki chodzone ze sztangą", []),
    "Bodyweight_Walking_Lunge": ("Wykroki chodzone", ["Walking lunges"]),
    "Dumbbell_Rear_Lunge": ("Wykroki w tył z hantlami", ["Zakroki"]),
    "Barbell_Step_Ups": ("Wejścia na skrzynię ze sztangą", []),
    "Dumbbell_Step_Ups": ("Wejścia na skrzynię z hantlami", ["Wejścia na skrzynię", "Step-up"]),
    "Seated_Leg_Curl": ("Uginanie nóg siedząc na maszynie", ["Uginanie nóg siedząc"]),
    "Standing_Leg_Curl": ("Uginanie nogi stojąc na maszynie", []),
    "Glute_Ham_Raise": ("Uginanie nóg na ławce GHR", ["GHR"]),
    "Natural_Glute_Ham_Raise": ("Nordyckie uginanie nóg", ["Nordic curl", "Nordyki"]),
    "Stiff-Legged_Barbell_Deadlift": ("Martwy ciąg na prostych nogach", ["Martwy ciąg na sztywnych nogach"]),
    "Stiff-Legged_Dumbbell_Deadlift": ("Martwy ciąg na prostych nogach z hantlami", []),
    "Good_Morning": ("Skłony ze sztangą na karku", ["Good morning", "Dzień dobry"]),
    "Barbell_Glute_Bridge": ("Mostek biodrowy ze sztangą", ["Glute bridge"]),
    "Single_Leg_Glute_Bridge": ("Mostek biodrowy na jednej nodze", []),
    "Butt_Lift_Bridge": ("Mostek biodrowy", ["Unoszenie bioder leżąc"]),
    "Glute_Kickback": ("Wykopy nogą w tył w klęku", ["Kickback nogą"]),
    "One-Legged_Cable_Kickback": ("Odwodzenie nogi w tył na wyciągu", []),
    "Thigh_Abductor": ("Odwodzenie nóg na maszynie", ["Odwodziciele na maszynie"]),
    "Thigh_Adductor": ("Przywodzenie nóg na maszynie", ["Przywodziciele na maszynie"]),
    "Seated_Calf_Raise": ("Wspięcia na palce siedząc", []),
    "Donkey_Calf_Raises": ("Wspięcia na palce w opadzie", ["Osiołki"]),
    "Calf_Press_On_The_Leg_Press_Machine": ("Wypychanie palcami na suwnicy", []),
    "Standing_Dumbbell_Calf_Raise": ("Wspięcia na palce z hantlami", []),
    "Smith_Machine_Calf_Raise": ("Wspięcia na palce na maszynie Smitha", []),
    "Narrow_Stance_Leg_Press": ("Wypychanie nogami wąsko na suwnicy", []),
    "Single-Leg_Leg_Extension": ("Prostowanie nogi na maszynie jednonóż", []),
    "Trap_Bar_Deadlift": ("Martwy ciąg ze sztangą heksagonalną", ["Trap bar deadlift", "Martwy ciąg z trap bar"]),
    "Deficit_Deadlift": ("Martwy ciąg z deficytu", []),
    "Rack_Pulls": ("Martwy ciąg z podwyższenia", ["Rack pull"]),
    "Kettlebell_One-Legged_Deadlift": ("Martwy ciąg na jednej nodze z kettlebell", ["Martwy ciąg na jednej nodze"]),
    "Hyperextensions_Back_Extensions": ("Wyprosty tułowia na ławce rzymskiej", ["Hyperekstensje", "Ławka rzymska",
                                                                                "Back extension"]),
    "Reverse_Hyperextension": ("Odwrotne hyperekstensje", []),
    "Pull_Through": ("Przeciąganie linki między nogami", ["Pull-through"]),
    "Box_Jump_Multiple_Response": ("Wskoki na skrzynię", ["Box jump", "Box jumps"]),
    "Freehand_Jump_Squat": ("Przysiad z wyskokiem", ["Wyskoki z przysiadu", "Jump squat"]),
    "Split_Jump": ("Wykroki z wyskokiem", []),
    "Kettlebell_Thruster": ("Thruster z kettlebell", ["Thruster"]),
    "Sled_Push": ("Pchanie sań", ["Prowler"]),
    # back
    "Close-Grip_Front_Lat_Pulldown": ("Ściąganie drążka wąskim chwytem", []),
    "Underhand_Cable_Pulldowns": ("Ściąganie drążka podchwytem", []),
    "V-Bar_Pulldown": ("Ściąganie uchwytu V do klatki", []),
    "Wide-Grip_Pulldown_Behind_The_Neck": ("Ściąganie drążka za kark", []),
    "One_Arm_Lat_Pulldown": ("Ściąganie linki jednorącz", []),
    "Straight-Arm_Pulldown": ("Ściąganie drążka prostymi rękami", ["Straight-arm pulldown"]),
    "Rope_Straight-Arm_Pulldown": ("Ściąganie liny prostymi rękami", []),
    "Weighted_Pull_Ups": ("Podciąganie z obciążeniem", ["Weighted pull-up"]),
    "Band_Assisted_Pull-Up": ("Podciąganie z gumą", ["Podciąganie z asystą"]),
    "Wide-Grip_Rear_Pull-Up": ("Podciąganie szerokim chwytem za kark", []),
    "V-Bar_Pullup": ("Podciąganie z uchwytem V", []),
    "Muscle_Up": ("Wyjście siłowe", ["Muscle-up"]),
    "Inverted_Row": ("Podciąganie australijskie", ["Wiosłowanie australijskie", "Inverted row"]),
    "T-Bar_Row_with_Handle": ("Wiosłowanie T-bar", ["T-bar", "T-bar row"]),
    "Lying_T-Bar_Row": ("Wiosłowanie T-bar leżąc", []),
    "Bent_Over_Two-Dumbbell_Row": ("Wiosłowanie hantlami w opadzie", []),
    "Reverse_Grip_Bent-Over_Rows": ("Wiosłowanie sztangą podchwytem", ["Wiosłowanie Yatesa"]),
    "Seated_One-arm_Cable_Pulley_Rows": ("Przyciąganie linki jednorącz siedząc", []),
    "Dumbbell_Incline_Row": ("Wiosłowanie hantlami na ławce skośnej", []),
    "Smith_Machine_Bent_Over_Row": ("Wiosłowanie na maszynie Smitha", []),
    "Leverage_High_Row": ("Przyciąganie na maszynie z góry", []),
    "Leverage_Iso_Row": ("Wiosłowanie na maszynie", []),
    "Bent_Over_One-Arm_Long_Bar_Row": ("Wiosłowanie końcem sztangi jednorącz", ["Meadows row"]),
    "Alternating_Renegade_Row": ("Wiosłowanie w podporze", ["Renegade row"]),
    "Barbell_Shrug": ("Szrugsy ze sztangą", []),
    "Upright_Barbell_Row": ("Podciąganie sztangi wzdłuż tułowia", ["Upright row"]),
    "Standing_Dumbbell_Upright_Row": ("Podciąganie hantli wzdłuż tułowia", []),
    "Farmers_Walk": ("Spacer farmera", ["Farmer's walk", "Farmer walk"]),
    "Rope_Climb": ("Wspinaczka po linie", []),
    "Bent-Arm_Dumbbell_Pullover": ("Przenoszenie hantla za głowę", ["Pullover"]),
    "Straight-Arm_Dumbbell_Pullover": ("Przenoszenie hantla za głowę na prostych rękach", []),
    # chest
    "Decline_Barbell_Bench_Press": ("Wyciskanie sztangi na ławce ujemnej", ["Wyciskanie na ławce ujemnej"]),
    "Decline_Dumbbell_Bench_Press": ("Wyciskanie hantli na ławce ujemnej", []),
    "Wide-Grip_Barbell_Bench_Press": ("Wyciskanie sztangi szerokim chwytem", []),
    "Smith_Machine_Bench_Press": ("Wyciskanie leżąc na maszynie Smitha", []),
    "Smith_Machine_Incline_Bench_Press": ("Wyciskanie na ławce skośnej na maszynie Smitha", []),
    "Machine_Bench_Press": ("Wyciskanie na maszynie siedząc", ["Chest press"]),
    "Leverage_Chest_Press": ("Wyciskanie na maszynie dźwigniowej", []),
    "Cable_Crossover": ("Krzyżowanie linek wyciągu", ["Brama", "Cable crossover"]),
    "Low_Cable_Crossover": ("Krzyżowanie linek wyciągu dolnego", []),
    "Incline_Dumbbell_Flyes": ("Rozpiętki na ławce skośnej", []),
    "Decline_Dumbbell_Flyes": ("Rozpiętki na ławce ujemnej", []),
    "Butterfly": ("Rozpiętki na maszynie", ["Butterfly", "Pec deck"]),
    "Flat_Bench_Cable_Flyes": ("Rozpiętki z linkami na ławce płaskiej", []),
    "Incline_Cable_Flye": ("Rozpiętki z linkami na ławce skośnej", []),
    "Dips_-_Triceps_Version": ("Pompki na poręczach na triceps", ["Dipy na triceps"]),
    "Bench_Dips": ("Pompki w podporze tyłem", ["Dipy na ławce", "Bench dips"]),
    "Ring_Dips": ("Pompki na kółkach gimnastycznych", ["Dipy na kółkach"]),
    "Push-Up_Wide": ("Pompki szerokie", []),
    "Push-Ups_-_Close_Triceps_Position": ("Pompki wąskie", ["Pompki diamentowe", "Diamond push-up"]),
    "Decline_Push-Up": ("Pompki z nogami na podwyższeniu", []),
    "Incline_Push-Up": ("Pompki z rękami na podwyższeniu", []),
    "Plyo_Push-up": ("Pompki z klaśnięciem", ["Pompki plyometryczne"]),
    "Handstand_Push-Ups": ("Pompki w staniu na rękach", ["HSPU"]),
    "Dumbbell_Floor_Press": ("Wyciskanie hantli z podłogi", []),
    "Floor_Press": ("Wyciskanie sztangi z podłogi", ["Floor press"]),
    "Dumbbell_Bench_Press_with_Neutral_Grip": ("Wyciskanie hantli chwytem neutralnym", []),
    "Hammer_Grip_Incline_DB_Bench_Press": ("Wyciskanie hantli chwytem neutralnym na ławce skośnej", []),
    # shoulders
    "Seated_Barbell_Military_Press": ("Wyciskanie sztangi nad głowę siedząc", []),
    "Standing_Dumbbell_Press": ("Wyciskanie hantli nad głowę stojąc", []),
    "Arnold_Dumbbell_Press": ("Wyciskanie Arnolda", ["Arnoldki", "Arnold press"]),
    "Push_Press": ("Wyciskanie z podrzutem", ["Push press"]),
    "Machine_Shoulder_Military_Press": ("Wyciskanie nad głowę na maszynie", []),
    "Smith_Machine_Overhead_Shoulder_Press": ("Wyciskanie nad głowę na maszynie Smitha", []),
    "Standing_Barbell_Press_Behind_Neck": ("Wyciskanie sztangi zza karku", []),
    "Front_Dumbbell_Raise": ("Unoszenie hantli w przód", ["Wznosy w przód", "Front raise"]),
    "Front_Plate_Raise": ("Unoszenie talerza w przód", []),
    "Front_Cable_Raise": ("Unoszenie linki wyciągu w przód", []),
    "Cable_Seated_Lateral_Raise": ("Unoszenie bokiem na wyciągu", ["Wznosy bokiem na wyciągu"]),
    "Seated_Side_Lateral_Raise": ("Unoszenie hantli bokiem siedząc", []),
    "Seated_Bent-Over_Rear_Delt_Raise": ("Unoszenie hantli w opadzie siedząc", []),
    "Cable_Rear_Delt_Fly": ("Odwrotne rozpiętki na wyciągu", []),
    "Reverse_Machine_Flyes": ("Odwrotne rozpiętki na maszynie", ["Reverse pec deck"]),
    "Band_Pull_Apart": ("Rozciąganie gumy przed sobą", ["Band pull-apart"]),
    "External_Rotation_with_Cable": ("Rotacja zewnętrzna ramienia na wyciągu", []),
    "External_Rotation": ("Rotacja zewnętrzna ramienia z hantlem", ["Rotacja zewnętrzna"]),
    "Kettlebell_Turkish_Get-Up_Lunge_style": ("Tureckie wstawanie", ["TGU", "Turkish get-up"]),
    # arms
    "EZ-Bar_Curl": ("Uginanie ramion ze sztangą łamaną", []),
    "Preacher_Curl": ("Uginanie ramion na modlitewniku", ["Modlitewnik"]),
    "Machine_Preacher_Curls": ("Uginanie ramion na modlitewniku na maszynie", []),
    "Machine_Bicep_Curl": ("Uginanie ramion na maszynie", []),
    "Concentration_Curls": ("Uginanie ramienia w podporze o udo", ["Uginanie koncentryczne"]),
    "Incline_Dumbbell_Curl": ("Uginanie ramion z hantlami na ławce skośnej", []),
    "Dumbbell_Alternate_Bicep_Curl": ("Uginanie ramion z hantlami naprzemiennie", []),
    "Cable_Hammer_Curls_-_Rope_Attachment": ("Uginanie młotkowe z liną na wyciągu", []),
    "Standing_Biceps_Cable_Curl": ("Uginanie ramion na wyciągu dolnym", []),
    "Spider_Curl": ("Uginanie ramion w leżeniu przodem na ławce skośnej", ["Spider curl"]),
    "Reverse_Barbell_Curl": ("Uginanie ramion nachwytem ze sztangą", []),
    "Zottman_Curl": ("Uginanie Zottmana", []),
    "Triceps_Pushdown_-_Rope_Attachment": ("Prostowanie ramion z liną na wyciągu", ["Triceps z liną"]),
    "Triceps_Pushdown_-_V-Bar_Attachment": ("Prostowanie ramion z uchwytem V na wyciągu", []),
    "Reverse_Grip_Triceps_Pushdown": ("Prostowanie ramion podchwytem na wyciągu", []),
    "Triceps_Overhead_Extension_with_Rope": ("Prostowanie ramion nad głową z liną", []),
    "Standing_Dumbbell_Triceps_Extension": ("Wyciskanie francuskie hantla stojąc", []),
    "Seated_Triceps_Press": ("Wyciskanie francuskie hantla siedząc", []),
    "EZ-Bar_Skullcrusher": ("Wyciskanie francuskie sztangą łamaną", []),
    "Lying_Dumbbell_Tricep_Extension": ("Wyciskanie francuskie hantlami leżąc", []),
    "Tricep_Dumbbell_Kickback": ("Prostowanie ramienia z hantlem w opadzie", ["Kickback"]),
    "JM_Press": ("Wyciskanie JM", ["JM press"]),
    "Machine_Triceps_Extension": ("Prostowanie ramion na maszynie", []),
    "Dip_Machine": ("Pompki na poręczach na maszynie", ["Dipy na maszynie"]),
    "Palms-Up_Barbell_Wrist_Curl_Over_A_Bench": ("Uginanie nadgarstków podchwytem", []),
    "Palms-Down_Wrist_Curl_Over_A_Bench": ("Uginanie nadgarstków nachwytem", []),
    "Wrist_Roller": ("Zwijanie linki na wałku", ["Wrist roller"]),
    "Plate_Pinch": ("Trzymanie talerzy w chwycie szczypcowym", []),
    # core
    "Cable_Crunch": ("Spięcia brzucha z linką wyciągu", ["Allahy", "Cable crunch"]),
    "Decline_Crunch": ("Spięcia brzucha na ławce skośnej", []),
    "Reverse_Crunch": ("Odwrotne spięcia brzucha", []),
    "Sit-Up": ("Siady z leżenia", ["Siady", "Sit-ups"]),
    "Oblique_Crunches": ("Skośne spięcia brzucha", []),
    "Exercise_Ball_Crunch": ("Spięcia brzucha na piłce", []),
    "Ab_Crunch_Machine": ("Spięcia brzucha na maszynie", []),
    "Air_Bike": ("Rowerek leżąc", ["Rowerek na brzuch", "Bicycle crunch"]),
    "Dead_Bug": ("Martwy robak", ["Dead bug"]),
    "Flutter_Kicks": ("Nożyce", ["Nożyczki"]),
    "Flat_Bench_Lying_Leg_Raise": ("Unoszenie nóg leżąc na ławce", ["Unoszenie nóg leżąc"]),
    "Knee_Hip_Raise_On_Parallel_Bars": ("Unoszenie kolan w podporze na poręczach", ["Unoszenie kolan"]),
    "Mountain_Climbers": ("Wspinaczka górska", ["Mountain climbers"]),
    "Pallof_Press": ("Wypychanie Pallofa", ["Pallof press"]),
    "Standing_Cable_Wood_Chop": ("Drwal na wyciągu", ["Wood chop"]),
    "Dumbbell_Side_Bend": ("Skłony boczne z hantlem", []),
    "Jackknife_Sit-Up": ("Scyzoryki", ["Scyzoryk"]),
    "Barbell_Ab_Rollout": ("Rollout ze sztangą", []),
    "Superman": ("Superman", ["Unoszenie rąk i nóg leżąc przodem"]),
    # olympic, full body, conditioning
    "Clean": ("Zarzut pełny", ["Zarzut z przysiadem", "Squat clean"]),
    "Hang_Clean": ("Zarzut ze zwisu", []),
    "Clean_and_Jerk": ("Podrzut", ["Zarzut i podrzut"]),
    "Snatch": ("Rwanie", []),
    "Power_Snatch": ("Rwanie siłowe", []),
    "Hang_Snatch": ("Rwanie ze zwisu", []),
    "Clean_and_Press": ("Zarzut i wyciskanie", []),
    "Split_Jerk": ("Podrzut nożycami", []),
    "One-Arm_Kettlebell_Swings": ("Swing kettlebell jednorącz", []),
    "One-Arm_Kettlebell_Snatch": ("Rwanie kettlebell jednorącz", []),
    "Kettlebell_Windmill": ("Wiatrak z kettlebell", []),
    "Two-Arm_Kettlebell_Clean": ("Zarzut dwóch kettlebell", []),
    "Atlas_Stones": ("Kamienie atlasa", []),
    "Tire_Flip": ("Przewracanie opony", []),
    "Battling_Ropes": ("Liny bojowe", ["Battle ropes"]),
    "Sledgehammer_Swings": ("Uderzenia młotem w oponę", []),
    "Overhead_Slam": ("Rzuty piłką lekarską o ziemię", ["Slam ball"]),
    "Medicine_Ball_Chest_Pass": ("Podanie piłki lekarskiej sprzed klatki", []),
    "Sled_Drag_-_Harness": ("Ciągnięcie sań w uprzęży", []),
    # cardio
    "Running_Treadmill": ("Bieg na bieżni", ["Bieżnia"]),
    "Walking_Treadmill": ("Marsz na bieżni", ["Chód na bieżni"]),
    "Jogging_Treadmill": ("Trucht na bieżni", []),
    "Elliptical_Trainer": ("Orbitrek", ["Elliptical"]),
    "Stairmaster": ("Schody ruchome", ["Stepper", "Stairmaster"]),
    "Recumbent_Bike": ("Rower poziomy", []),
    "Bicycling_Stationary": ("Rower stacjonarny", ["Rowerek stacjonarny", "Spinning"]),
    "Trail_Running_Walking": ("Bieg w terenie", ["Trail"]),
    # stretching and rolling
    "Childs_Pose": ("Pozycja dziecka", ["Child's pose"]),
    "Cat_Stretch": ("Koci grzbiet", []),
    "Hamstring_Stretch": ("Rozciąganie tylnej części uda", []),
    "Quad_Stretch": ("Rozciąganie czworogłowego uda", []),
    "Calf_Stretch_Hands_Against_Wall": ("Rozciąganie łydki przy ścianie", []),
    "Kneeling_Hip_Flexor": ("Rozciąganie zginaczy biodra w klęku", []),
    "Worlds_Greatest_Stretch": ("Najlepsze rozciąganie świata", ["World's greatest stretch"]),
    "Triceps_Stretch": ("Rozciąganie tricepsa", []),
    "Shoulder_Stretch": ("Rozciąganie barku", []),
    "Chest_And_Front_Of_Shoulder_Stretch": ("Rozciąganie klatki i przodu barku", []),
    "Arm_Circles": ("Krążenia ramion", []),
    "Standing_Toe_Touches": ("Skłon do palców stojąc", []),
    "Inchworm": ("Gąsienica", ["Inchworm"]),
    "Hug_Knees_To_Chest": ("Przyciąganie kolan do klatki", []),
    "Quadriceps-SMR": ("Rolowanie czworogłowego uda", []),
    "Iliotibial_Tract-SMR": ("Rolowanie pasma biodrowo-piszczelowego", []),
    "Piriformis-SMR": ("Rolowanie mięśnia gruszkowatego", []),
    "Upper_Back_Stretch": ("Rozciąganie górnej części pleców", []),
}


# Upstream ids whose kebab-case one of the 51 has already.
RENAMED = {"Clean": "squat-clean"}


def kebab(upstream_id):
    s = upstream_id.lower().replace("_", "-").replace(" ", "-")
    s = re.sub(r"[^a-z0-9-]", "-", s)
    return re.sub(r"-+", "-", s).strip("-")


def muscle_id(name):
    m = kebab(name)
    if m not in MUSCLES:
        sys.exit(f"unknown muscle {name!r}")
    return m


def pictures(e):
    """How many pictures upstream has of `e`: its images are `<id>/0.jpg`, `<id>/1.jpg` … (checked)."""
    images = e.get("images") or []
    expected = [f"{e['id']}/{i}.jpg" for i in range(len(images))]
    if images != expected:
        sys.exit(f"{e['id']}: pictures {images}, expected {expected}")
    return len(images)


def dumps(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("upstream", help="free-exercise-db's dist/exercises.json at commit " + COMMIT)
    parser.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "core",
                                                      "extensions", "builtin"))
    parser.add_argument("--any-version", action="store_true", help="skip the SHA-256 check (a newer upstream)")
    args = parser.parse_args()
    raw = open(args.upstream, "rb").read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SHA256 and not args.any_version:
        sys.exit(f"{args.upstream}: SHA-256 {digest}, expected {SHA256} (commit {COMMIT}); --any-version to go on")
    upstream = {e["id"]: e for e in json.loads(raw)}
    for key in POLISH:
        if key not in upstream:
            sys.exit(f"POLISH names {key!r}, which free-exercise-db has not")

    exercises = []
    instructions = {}
    taken = set()
    used = {}

    def details(e):
        out = {"primary": [muscle_id(m) for m in e.get("primaryMuscles") or []],
               "secondary": [muscle_id(m) for m in e.get("secondaryMuscles") or []]}
        if e.get("equipment"):
            out["equipment"] = EQUIPMENT[e["equipment"]][0]
        out["category"] = CATEGORIES[e["category"]][0]
        if e.get("level"):
            out["level"] = e["level"]
        return out

    for row in BUILTIN:
        ident, pl, en, source, aliases = row[:5]
        item = {"id": ident, "pl": pl, "en": en, "aliases": aliases}
        if source:
            if source in used:
                sys.exit(f"{source} stands for both {used[source]} and {ident}")
            used[source] = ident
            e = upstream[source]
            item.update(details(e))
            item["source"] = source
            if pictures(e):
                item["pictures"] = pictures(e)
            if upstream[source]["name"].lower() not in {a.lower() for a in aliases + [en]}:
                item["aliases"] = aliases + [upstream[source]["name"]]
            steps = [s for s in e.get("instructions") or [] if s]
            if steps:
                instructions[ident] = steps
        else:
            item.update(row[5])
        taken.add(ident)
        exercises.append(item)

    for e in sorted(upstream.values(), key=lambda e: (e["name"].lower(), e["id"])):
        if e["id"] in used:
            continue
        ident = RENAMED.get(e["id"]) or kebab(e["id"])
        if ident in taken:
            sys.exit(f"id {ident} twice")
        taken.add(ident)
        pl, aliases = POLISH.get(e["id"], (None, []))
        item = {"id": ident, "pl": pl or e["name"], "en": e["name"], "aliases": aliases}
        item.update(details(e))
        item["source"] = e["id"]
        if pictures(e):
            item["pictures"] = pictures(e)
        steps = [s for s in e.get("instructions") or [] if s]
        if steps:
            instructions[ident] = steps
        exercises.append(item)

    catalog = {
        "version": 2,
        "source": {"name": "free-exercise-db", "url": "https://github.com/yuhonas/free-exercise-db",
                   "commit": COMMIT, "license": "Unlicense",
                   "pictures": f"https://raw.githubusercontent.com/yuhonas/free-exercise-db/{COMMIT}/exercises/"},
        "muscles": {k: {"pl": v[0], "en": v[1], "names": v[2]} for k, v in MUSCLES.items()},
        "equipment": {v[0]: {"pl": v[1], "en": v[2]} for v in EQUIPMENT.values()},
        "categories": {v[0]: {"pl": v[1], "en": v[2]} for v in CATEGORIES.values()},
        "levels": {k: {"pl": v[0], "en": v[1]} for k, v in LEVELS.items()},
    }
    # One exercise a line: a change to the catalog is a readable diff.
    head = dumps(catalog)[:-1]
    text = head + ',"exercises":[\n' + ",\n".join(dumps(x) for x in exercises) + "\n]}\n"
    with open(os.path.join(args.out, "exercises.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    steps = "{\n" + ",\n".join(dumps(k)[:] + ":" + dumps(v) for k, v in instructions.items()) + "\n}\n"
    with open(os.path.join(args.out, "exercise-instructions.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(steps)
    polish = sum(1 for x in exercises if x["pl"] != x["en"] or x["id"] in {b[0] for b in BUILTIN})
    print(f"{len(exercises)} exercises ({len(BUILTIN)} Brulion's, {polish} with Polish names), "
          f"{len(instructions)} with instructions, {sum(1 for x in exercises if x.get('pictures'))} with pictures; "
          f"exercises.json {len(text.encode())} bytes, "
          f"exercise-instructions.json {len(steps.encode())} bytes")


if __name__ == "__main__":
    main()
