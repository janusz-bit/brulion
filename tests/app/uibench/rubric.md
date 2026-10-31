# UI judging rubric

You compare screenshots of **two note-taking apps showing the same notebook** in the same window
size and theme. Each group shows one screen at one size, in a light and a dark pair; in each pair
the two apps are labelled **A** and **B** (the labels are drawn at random for every pair).

* The UI language is Polish in both apps and is **not** judged.
* Score what is visible at this size. Differences in how content is rendered (for example a date
  shown as a chip or as raw text, a tag as a pill or as coloured text) are part of the UI and are
  judged.
* Judge each image on its own merits against the anchors below and use the whole scale: a typical
  well-made commercial app is about 7.
* Judge both themes on their own: a dark pair is compared with the dark anchors, not with the light
  pair.
* Do not try to identify the apps. If an image cannot be opened or does not show the described
  screen, mark the pair `"invalid": true` and say why in the comments.
* For each image give one sentence of its main strengths and one sentence of its main weaknesses —
  concrete: where on the screen and what.
* **After** all scores, in the separate field `identityGuess`, say which image of each pair you
  believe is Obsidian (`"A"`, `"B"` or `"unsure"`). It is used only to measure how blind the panel
  was and does not change any score.

## Criteria (1–10 each, equal weights)

| # | Criterion | 1–2 | 3–4 | 5–6 | 7–8 | 9–10 |
|---|---|---|---|---|---|---|
| 1 | **Hierarchy** (`hierarchy`) — is the primary content obvious, does visual weight match importance | no focal point; chrome competes with content | focal point exists but toolbars/labels as loud as content; headings weakly set apart | clear main area, some competing or ambiguous groups | content clearly dominant; groups obvious; secondary info subdued | effortless scan path; every element's weight matches its importance |
| 2 | **Typography** (`typography`) — readability, scale, line length, leading, weights | hard to read: tiny, cramped, clashing fonts | readable but inconsistent sizes/weights, awkward leading or line length | decent; a few inconsistencies (too many sizes, heavy headings, tight labels) | comfortable body text, clear scale with few sizes, ~50–80 chars per line, good leading | refined: harmonious scale and rhythm, polished labels and details |
| 3 | **Spacing & alignment** (`spacing`) | cramped or random gaps; edges misaligned everywhere | inconsistent padding; several misalignments | mostly consistent; a few irregular gaps | consistent rhythm, shared alignment lines, generous but not wasteful whitespace | precise grid; spacing alone shows the grouping |
| 4 | **Colour & contrast** (`color`) (both themes judged on their own) | poor contrast or garish/clashing palette | some text hard to read; accent overused; muddy dark mode | acceptable; minor weak spots or dull/noisy | harmonious, restrained palette; purposeful accent; all text readable; comfortable dark mode | distinctive, beautiful palette with excellent legibility |
| 5 | **Consistency & polish** (`consistency`) | visibly broken: clipped text, overlaps, mismatched components | several rough edges: icons, radii, borders, baselines | coherent, some details inconsistent | one component family; consistent icons/radii/borders; no artefacts | meticulous; every detail intentional |
| 6 | **Calm & content focus** (`calm`) | overwhelming: borders, badges, colours everywhere | busy; chrome takes too much room | moderately calm; some noise | calm; chrome recedes; content first | serene yet complete |
| 7 | **Affordance & usability** (`affordance`) (for the device: mouse on desktop, finger on phone) | can't tell what is clickable; tiny targets | unclear unlabeled icons, small targets | mostly clear; some ambiguity | controls recognisable, current/selected state clear, comfortable targets | obvious and comfortable everywhere; location crystal clear |
| 8 | **Overall appeal** (`appeal`) | unappealing, dated | plain or amateurish | average | attractive, modern, pleasant to use | delightful; would choose it on looks alone |

## Output

One JSON object (integers 1–10 for every criterion of every image); `batch` is the `batch` of your
batch.json and `gid` the groups' own ids — the values below only show the shape:

```json
{"batch": "bNN", "judge": 2,
 "groups": [{"gid": "gNN", "pairs": [
   {"theme": "light", "invalid": false,
    "A": {"hierarchy": 7, "typography": 6, "spacing": 7, "color": 8, "consistency": 7, "calm": 6, "affordance": 7, "appeal": 7},
    "B": {"hierarchy": 8, "typography": 7, "spacing": 7, "color": 7, "consistency": 8, "calm": 7, "affordance": 7, "appeal": 8},
    "preferred": "B", "strengthsA": "…", "weaknessesA": "…", "strengthsB": "…", "weaknessesB": "…"},
   {"theme": "dark", "invalid": false, "A": {}, "B": {}, "preferred": "A", "strengthsA": "…", "weaknessesA": "…", "strengthsB": "…", "weaknessesB": "…"}]}],
 "identityGuess": [{"gid": "gNN", "theme": "light", "obsidian": "A"}]}
```
