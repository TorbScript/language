# The TorbScript Brand

**Status: decided, assets in `brand/`** — the owner's decisions of 2026-09-28: the Orb as the mark, with a cut heavier
than the first draft's; carmine `#D4002D` with the palette of section 4; Chivo and Chivo Mono; the depth gradient as
the primary form of the mark; a red button only for the main call to action. The logo files, the icons, the social card
and the tokens are in `brand/` (`brand/README.md` lists every file). The VS Code extension carries the icons and the
walkthrough, the released Windows `torb.exe` the Orb (section 11), and `torb` colours its output (section 10); the
extension's two colour themes, the website, the registry and the forge's theme are the slices of section 13 still to
come.

**One mark, one red, one type family, and every value a named token.** The identity has to look like the language it
stands for: one clear shape instead of several, exact about where things change, confident without effects.

```text
   the Orb ─────────────► a carmine disc cut by one T: the mark, the favicon, the app and file icons (section 3)
   Torb Red #D4002D ────► the only brand colour; a warm ink and a warm paper around it (section 4)
   Chivo + Chivo Mono ──► one type family for prose, headings and code (section 5)
   hairlines, no glow ──► surfaces are steps of the neutral scale, split by 1 px lines (section 6)
   brand/tokens.json ───► every value above, and brand/tokens.css for the site, the docs and the registry
```

- **[1. Positioning](#1-positioning)**
- **[2. Personality](#2-personality)**
- **[3. The Orb](#3-the-orb)** — idea, construction, the weight of the cut, lockups, clear space, sizes, don'ts
- **[4. Colour](#4-colour)** — why red, the scales, roles, contrast, states, gradients
- **[5. Type](#5-type)**
- **[6. Surfaces](#6-surfaces)** — hairlines over shadows
- **[7. Motion](#7-motion)**
- **[8. Buttons](#8-buttons)** — and the rule for the red one
- **[9. Code](#9-code)** — syntax colours
- **[10. The terminal](#10-the-terminal)** — how `torb` colours its output
- **[11. Applications](#11-applications)** — website, docs, registry, forge, VS Code, binary icons
- **[12. The files](#12-the-files)**
- **[13. Slices](#13-slices)**
- **[14. Open](#14-open)**

---

## 1. Positioning

TorbScript is **a single-language ecosystem**: one functional-first language with value semantics for every layer of
a system. The same language writes scripts, configuration and build files, servers and tools. The same program runs
interpreted while it is written and compiles to a native binary when it ships, and nothing observable differs between
the two. The line that says it is **One language, every layer.**

## 2. Personality

**Exact. Whole. Direct. Vivid.**

- **Exact.** A mutation happens where it is written and nowhere else. In the identity every form is a straight cut or
  a true circle, and every value is a token.
- **Whole.** A value is never aliased, and one language covers the stack. The mark is one closed disc; the cut divides
  it, and nothing is added to it.
- **Direct.** Commands without parentheses, one binary. The identity says its name and stops: no mascot, no ornament,
  no second accent colour.
- **Vivid.** One saturated carmine on warm ink and paper. The energy comes from contrast, not from effects.

The shape language follows from it:

- **Two primitives: the straight cut and the full circle.** Solid fills only; an outline is never a style. Negative
  space is made by cutting through a form, never by drawing on top of it.
- **Sharp corners.** The only curves are true circles and the letterforms. UI radii stop at 4 px (section 6).
- **One cut width per mark**, kept at every size and snapped to whole pixels where the mark is small.
- **Horizontal means layer, vertical means the program going through them.** The top rule of every page and the
  strata band (section 4.7) are layers; the Orb's stem runs through them.
- **Never:** perspective, 3D, bevels, facets, glow, soft shadows on marks, outlined logos, mascots.

## 3. The Orb

### 3.1 The idea

The name, drawn: Torb is a **T** and an **orb**. A solid disc is cut by one T-shaped gap. The cap above the bar is the
one program. Below it the disc splits into two identical halves, the interpreter and the native binary, mirror images
of each other because nothing observable may differ between them. The disc stays whole in outline: the cut divides
it, and nothing is added.

It is a single closed silhouette, so it survives one colour, embroidery and a 16 px browser tab. No language uses a
solid disc this way: Julia's mark is three separate dots, and Odin's O is a thin ring split on a diagonal.

### 3.2 Construction

The mark is drawn on a grid of 96 units, and 96 = 32 × 3: at 32 px and its multiples every edge of the cut lands on
a whole pixel. The sizes below 64 px are drawn on their own pixel grids (section 3.7).

```text
   y =  0 ┌──────────────────────────────┐   the grid of 96; the disc is inscribed in it
          │             cap              │   0 to 24: the cap, a segment of the disc
   y = 24 ├──────────────────────────────┤   the bar's top edge: a quarter of the diameter
          │         bar, 18 high         │   24 to 42: the bar, across the whole disc
   y = 42 ├───────────┬──────┬───────────┤
          │ left half │ stem │ right half│   42 to 96: the stem, 18 wide, and the two halves
          │           │  18  │           │
   y = 96 └───────────┴──────┴───────────┘
          x = 0      39     57          96
```

1. **The disc** has a diameter of 96 units.
2. **The cut is 18 units wide** (3/16 of the diameter). It is the one number that sets the weight of the mark, and it is
   the width of the stem and the height of the bar alike: one cut width, the rule of the shape language. `cut` in
   `brand/tokens.json` holds it.
3. **The bar's top edge is at 24 units**, a quarter of the diameter, so the bar runs from 24 to 42 and spans the whole
   disc. The stem is centred, from x = 39 to 57, and runs from the bar down through the foot of the disc.
4. **The result is one path of three closed parts**: the cap, the left half and the right half. The mark is always
   that path; it is never assembled from a circle with shapes laid over it.

### 3.3 The weight of the cut

The first draft cut the T at 10 units. At that width the disc read as a beetle: a head segment on top and two wing
halves split by a seam down the middle. The cut was studied at 10, 14, 17, 18, 20, 22 and 24 units, with the bar
higher and lower and heavier and lighter than the stem, at 256, 64, 32 and 16 px, flat and in the gradient, on paper
and on ink.

| cut | what it reads as | at small sizes |
|-----|------------------|----------------|
| 10 | a seam: the beetle | the stem is 1.7 px at 16 px and blurs shut |
| 14 | still a split more than a letter | the stem is a grey line at 16 px |
| 17 | a T | off the pixel grid at every icon size |
| **18** | **a letter T cut out of a disc, and the disc still one form** | **6 px at 32, 12 at 64, 24 at 128, 48 at 256: every edge of the cut on a whole pixel** |
| 20 | a heavy T; the halves begin to read as separate blocks | off the grid as well |
| 22 | heavier still; the cap shrinks to a lid | off the grid as well |
| 24 | the T outweighs the disc: the halves become quarter wedges | on the grid everywhere, but the orb is gone |

- **The bar's position.** With the bar's centre at 30 the cap is a thin sliver that breaks up at 16 px; at 38 and 42
  the cap grows into a dome and the stem shortens until the mark reads as a dome on two legs. The top edge at 24 keeps
  the stem longer than the cap is high, which is what makes a T.
- **The bar against the stem.** A bar of 0.8 of the stem is letter-like but gives the mark two cut widths; a bar of 1.2
  makes the cap read as a separate lid. Equal widths keep the rule of one cut.

### 3.4 Forms of the mark

| form | file | where |
|------|------|-------|
| **depth gradient** (primary) | `brand/logo/mark.svg` | light grounds, from 48 px up: the site's hero, the app icon, social cards, print with a spot-colour proof |
| depth gradient for dark grounds | `brand/logo/mark-on-dark.svg` | dark grounds, from 48 px up |
| flat Torb Red | `brand/logo/mark-flat.svg` | below 48 px on any ground, favicons, one-colour reproduction in red |
| black, white | `brand/logo/mark-mono-black.svg`, `mark-mono-white.svg` | one-colour print, engraving, embroidery, on photographs |
| pixel art | `brand/logo/favicon.svg`, `brand/icons/app-icon-16.png` to `-48.png` | 16 to 48 px, each drawn on its own pixel grid (section 3.7) |

The depth gradient runs from red-500 `#F33F49` at the top of the disc to red-800 `#8E1018` at its foot. On a dark
ground the foot is lifted to red-700 `#B00921`, because red-800 on ink turns brown; with it, the upper three quarters
of the disc keep at least 3:1 against ink.

### 3.5 Wordmark and lockups

The wordmark is "TorbScript" in **Chivo 700**, tracking −1.4%, outlined. It is always one word with a capital T and a
capital S, never "Torb Script", never "TORBSCRIPT", and never set again in another font.

- **Horizontal lockup** (`lockup.svg` and its variants): the mark is **two cap heights** tall and centred on the caps.
  So the cap line of the wordmark runs along the top edge of the cut bar, and the baseline sits at three quarters of
  the disc. The gap between mark and word is a quarter of the mark's diameter. This is the default logo.
- **Stacked lockup** (`lockup-stacked.svg`): the mark centred above the wordmark, the mark as wide as three fifths of
  the word, with a gap of a quarter of the mark. It is for square spaces: title slides, stickers, the forge's
  organisation avatar.
- **The wordmark alone** stands where the mark already appears nearby (the social card puts the mark on the right and
  the wordmark top left) and nowhere else.

### 3.6 Clear space and minimum sizes

**Clear space is a quarter of the mark's diameter on every side** (24 of 96 units, the height of the cap), for the mark
and for both lockups, measured with the mark's diameter in the lockup. Nothing else enters it: no text, no edge of the
page, no other logo.

| use | minimum |
|-----|---------|
| the mark in the depth gradient | 48 px; below it the flat mark |
| the mark as a vector | 32 px; below it the pixel art of 16 and 24 px |
| the horizontal lockup | 24 px tall on screen (the wordmark's caps are then 12 px) |
| print, mark or lockup | 6 mm for the mark's diameter |

### 3.7 Small sizes

Below 64 px the mark is drawn for its size, not scaled. The rule that draws it:

- **the stem is centred**, so on an even canvas it takes an even number of pixels: the nearest to the exact width, and
  the wider one on a tie, because a gap loses a little of its width to antialiasing at each edge;
- **the bar's top and height are rounded** to whole pixels;
- **favicons and icons up to 48 px are flat.** A gradient over 16 pixels is noise.

| size | stem | bar | where |
|------|------|-----|-------|
| 16 px | 4 px | rows 4 to 7 | `favicon.svg`, `app-icon-16.png`, inside `torb.ico` |
| 24 px | 4 px | rows 6 to 11 | `app-icon-24.png`, inside `torb.ico` |
| 32 px | 6 px | rows 8 to 14 | `app-icon-32.png`, inside `torb.ico`: the vector itself, already on the grid |
| 48 px | 10 px | rows 12 to 21 | `app-icon-48.png`, inside `torb.ico` |
| 64 px and up | exact | exact | the vector: 18/96 lands on whole pixels at 64, 128, 256 and 512 |

### 3.8 Don'ts

1. **Don't change the cut.** Not thinner, not thicker, not different for the bar and the stem. At small sizes use the
   pixel art, which is drawn from the same rule.
2. **Don't move, rotate or mirror the T.** Upside down it is ⊥, and a tilted T is a different mark.
3. **Don't add effects**: no outline, stroke, glow, drop shadow, bevel, 3D, texture, radial gradient or animation other
   than the one of section 7.
4. **Don't recolour.** The mark is Torb Red, one of the two depth gradients, black or white, and paper on the macOS
   tile. No other hue, no gradient that leaves the red family, no two colours in one mark.
5. **Don't fill the cut.** The ground shows through it; nothing is placed inside it.
6. **Don't enclose the mark** in a ring, a square or a badge, except the macOS tile, which the platform imposes.
7. **Don't place the mark on red or on a busy ground.** On a red ground use the white mark; on a photograph use black or
   white where the photograph is calm.
8. **Don't crop the disc, don't use the mark as a letter** inside a word, and don't rebuild the lockup with other
   spacing or proportions.

## 4. Colour

### 4.1 Why red, and which red

Four design arguments make a red the right primary:

- **Distinctness among language brands.** The field is crowded in blue (TypeScript, Go, Python, Dart, Odin), violet
  (Kotlin, Elixir, Roc) and orange (Zig, Swift, Rust's Ferris, OCaml, Mojo). Among the reds, three languages share a
  warm tomato (Ruby, Scala, Julia) and the web frameworks sit in crimson (Angular, NestJS). A carmine, cooler than
  tomato and less pink than crimson, belongs to no language.
- **Recognisability.** A red mark is found first in a row of blue and grey tabs, docks and editor icons: a small red
  area draws the eye before any other hue. One colour plus one shape is remembered better than a combination, and it
  reproduces in one colour for print, embroidery and a terminal.
- **Contrast.** At OKLCH lightness 0.55 the carmine reaches 5.4:1 against paper, as text on paper and as the ground
  of a paper label, so the brand red can carry a button label, not only a logo. Against ink it holds 3.4:1, above the
  3:1 WCAG asks of graphics.
- **Range.** A red at the gamut edge gives a scale from a pale tint to a deep oxblood without leaving its hue, which is
  what the gradients and the strata band are made of.

**Torb Red = `#D4002D`** = oklch(0.549 0.221 22.6), red-600 of the scale, at the sRGB gamut edge for its lightness and
hue. The nearest spot colour is Pantone 199 C (section 14).

### 4.2 The red scale

Built in OKLCH: even lightness steps, chroma capped by the gamut. The light steps stay at hue 21 or above, so a tint
never drifts into crimson; the dark steps warm slightly (hue 24 to 27), so they stay red instead of turning maroon.
"Paper" is `#FFFDFC` and "ink" is `#14110F`; every ratio is WCAG 2 contrast.

| step | hex | OKLCH (L C h) | on paper | on ink | use |
|------|-----|---------------|----------|--------|-----|
| 50 | `#FFF2F2` | 0.971 0.014 17.4 | 1.08 | 17.22 | error surface (light) |
| 100 | `#FFE3E2` | 0.938 0.031 20.2 | 1.19 | 15.52 | text selection (light) |
| 200 | `#FFCBC8` | 0.887 0.059 22.2 | 1.42 | 13.10 | decorative tints |
| 300 | `#FFA6A3` | 0.812 0.106 21.8 | 1.84 | 10.06 | error text (dark), link underline hover (dark) |
| 400 | `#FE7373` | 0.722 0.170 22.3 | 2.62 | 7.09 | error accent (dark), the strata band's top |
| 500 | `#F33F49` | 0.640 0.215 23.1 | 3.69 | 5.03 | link underline (dark), the gradients' top |
| **600** | **`#D4002D`** | **0.549 0.221 22.6** | **5.40** | **3.44** | **Torb Red: the mark, the top rule, the call-to-action button, the link underline** |
| 700 | `#B00921` | 0.480 0.190 23.9 | 7.12 | 2.60 | keywords and error text (light), button hover, the dark gradient's foot |
| 800 | `#8E1018` | 0.415 0.158 25.4 | 9.26 | 2.00 | the depth gradient's foot, button pressed |
| 900 | `#6F1414` | 0.355 0.125 26.4 | 11.59 | 1.60 | deep accents on light |
| 950 | `#430B0A` | 0.254 0.085 26.5 | 16.04 | 1.16 | red-tinted dark surfaces |

### 4.3 Ink and paper

The neutrals carry a faint warm tint (OKLCH hue 40, chroma at most 0.009): they sit next to carmine without looking
cold, and they avoid the blue-grey slate most developer brands use.

| step | hex | on paper | on ink | use |
|------|-----|----------|--------|-----|
| 0 | `#FFFDFC` | 1.00 | 18.54 | **paper**: the light background |
| 50 | `#F9F6F5` | 1.06 | 17.49 | light surface and code blocks; text on dark |
| 100 | `#F2EDEC` | 1.14 | 16.21 | hover surface (light) |
| 200 | `#E3DEDD` | 1.31 | 14.11 | hairline (light), pressed surface (light) |
| 300 | `#CFC9C7` | 1.61 | 11.49 | strong border (light), parameters (dark) |
| 400 | `#A9A3A1` | 2.45 | 7.56 | muted text and comments (dark) |
| 500 | `#857E7C` | 3.93 | 4.72 | control borders (both), subtle text (dark) |
| 600 | `#68625F` | 5.91 | 3.13 | muted text (light) |
| 700 | `#4F4947` | 8.71 | 2.13 | parameters (light), strong border (dark) |
| 800 | `#37312F` | 12.61 | 1.47 | hairline (dark) |
| 850 | `#292422` | 15.12 | 1.23 | raised and hover surface (dark) |
| 900 | `#1F1B19` | 16.85 | 1.10 | dark surface and code blocks |
| 950 | `#14110F` | 18.54 | 1.00 | **ink**: text on light, the dark background |
| 1000 | `#0C0908` | 19.57 | 1.06 | the ink gradient's foot |

### 4.4 Roles

`brand/tokens.css` names each role `--torb-<role>` and switches it with the theme.

| role | light | dark | for |
|------|-------|------|-----|
| `background` | `#FFFDFC` | `#14110F` | the page |
| `surface` | `#F9F6F5` | `#1F1B19` | sidebars, cards, table heads |
| `surface-raised` | `#FFFFFF` | `#292422` | menus, dialogs, a sticky header |
| `surface-hover` / `surface-active` | `#F2EDEC` / `#E3DEDD` | `#292422` / `#37312F` | rows, neutral buttons |
| `code` | `#F9F6F5` | `#1F1B19` | code blocks |
| `border` | `#E3DEDD` | `#37312F` | decorative hairlines |
| `border-strong` | `#CFC9C7` | `#4F4947` | table rules, dividers that carry structure |
| `border-control` | `#857E7C` | `#857E7C` | inputs, checkboxes, the secondary button (WCAG 1.4.11) |
| `text` / `text-muted` / `text-subtle` | `#14110F` / `#68625F` / `#78716F` | `#F9F6F5` / `#A9A3A1` / `#857E7C` | body, secondary, never essential |
| `brand` | `#D4002D` | `#D4002D` | the mark, the top rule, the call-to-action button |
| `link` / `link-underline` / `link-underline-hover` | ink / `#D4002D` / `#B00921` | paper / `#F33F49` / `#FFA6A3` | links |
| `focus` | `#14110F` | `#F9F6F5` | the focus outline |
| `selection` | `#FFE3E2` | `#5E2120` | selected text |

There is no brand text colour and no brand surface. Red text is an error or a keyword, and a red tint is an error
(section 4.6); a link is ink or paper with a carmine underline, and hovering deepens the underline, not the text.

### 4.5 Contrast

Computed from the tokens, WCAG 2. AA asks 4.5:1 for text, 3:1 for large text and for graphics that carry meaning.

| pair | light | WCAG 2 | dark | WCAG 2 |
|------|-------|--------|------|--------|
| `text` on `background` | 18.54:1 | AAA | 17.49:1 | AAA |
| `text-muted` on `background` | 5.91:1 | AA | 7.56:1 | AAA |
| `text-subtle` on `background` | 4.72:1 | AA | 4.72:1 | AA |
| `text` on `surface` | 17.49:1 | AAA | 15.89:1 | AAA |
| `text-muted` on `surface` | 5.58:1 | AA | 6.87:1 | AA |
| `text` on `selection` | 15.52:1 | AAA | 11.39:1 | AAA |
| `border-control` on `background` | 3.93:1 | graphic | 4.72:1 | graphic |
| `link-underline` on `background` | 5.40:1 | graphic | 5.03:1 | graphic |
| `focus` on `background` | 18.54:1 | graphic | 17.49:1 | graphic |

| brand pair | ratio | WCAG 2 |
|------------|-------|--------|
| Torb Red on paper | 5.40:1 | AA text |
| Torb Red on ink | 3.44:1 | graphic (3:1); never text |
| paper on Torb Red: the call-to-action label | 5.40:1 | AA |
| paper on red-700: its hover | 7.12:1 | AAA |
| paper on red-800: pressed | 9.26:1 | AAA |
| red-800, the depth gradient's foot, on paper | 9.26:1 | graphic |
| red-700, the dark gradient's foot, on ink | 2.60:1 | the silhouette holds: the upper three quarters stay above 3:1 |

| state | light text on background / on its surface | light accent on its surface | dark text on background / on its surface | dark accent on its surface |
|-------|-------------------------------------------|-----------------------------|------------------------------------------|----------------------------|
| error | `#B00921` 7.12 / 6.62 | `#B00921` 6.62 | `#FFA6A3` 10.06 / 8.71 | `#FE7373` 6.14 |
| warning | `#7D5000` 6.86 / 6.25 | `#9F6700` 4.28 | `#EEB976` 10.60 / 9.04 | `#D6963B` 6.33 |
| success | `#006E30` 6.33 / 5.85 | `#1D8B44` 3.96 | `#92D7A0` 11.15 / 9.41 | `#62BB78` 6.73 |
| info | `#125CA1` 6.74 / 6.20 | `#2876C5` 4.24 | `#98C8FF` 10.78 / 9.13 | `#64A9F3` 6.46 |

Every text pair of the theme, the states, the syntax (section 9) and the terminal (section 10) reaches 4.5:1.

### 4.6 States, and errors next to a red brand

| state | text | surface | border | accent (icon, leading edge) |
|-------|------|---------|--------|-----------------------------|
| error | `#B00921` / `#FFA6A3` | `#FFF2F2` / `#3A1413` | `#FFA6A3` / `#8E1018` | `#B00921` / `#FE7373` |
| warning | `#7D5000` / `#EEB976` | `#FDF1E4` / `#2C1F0E` | `#DDB686` / `#704D1E` | `#9F6700` / `#D6963B` |
| success | `#006E30` / `#92D7A0` | `#EAF8EC` / `#152619` | `#9ACCA3` / `#32613E` | `#1D8B44` / `#62BB78` |
| info | `#125CA1` / `#98C8FF` | `#EBF5FF` / `#152332` | `#99C2EF` / `#33577F` | `#2876C5` / `#64A9F3` |

(light / dark in every cell.)

**Decision: errors stay red, and the brand red never marks a state.** Every terminal, editor, CI log and browser
agrees that red means error; moving errors to another hue would cost every reader. And no hue shift makes two reds
safely distinguishable: shifted far enough for everyone to tell them apart, the error no longer looks like an error,
and red-green colour vision deficiency (about 8% of men) takes the difference away. So the error is kept apart from
the brand by value, form and place:

1. **By value.** No error token is the brand's `#D4002D`. On light grounds error text is red-700, one step darker
   (lightness 0.48 against 0.55, 1.32:1 between the two); on dark grounds it is red-300, three steps lighter
   (lightness 0.81 against 0.55, 2.93:1 between the two). The error accent follows its text, not the brand.
2. **By form.** The brand is only ever a solid fill: the mark, the top rule, the strata band, the call-to-action button.
   An error is always all four of: a tinted surface, a 3 px leading border in the accent, the word ("Error",
   `error:`), and an icon, a filled circle with a cross cut out of it (the Orb's cut language). An error never uses a
   solid red fill, and red text never stands alone without its word.
3. **By place.** The brand appears in the chrome (header, hero, footer) and on one button; an error appears where the
   problem is (the field, the diagnostic, the gutter). In the terminal there is no brand at all (section 10).
4. **Never by colour alone** (WCAG 1.4.1). Each error carries a word, a shape (the caret line `^^^`, a wavy underline,
   the icon) and a place, so a colour-blind reader, a `NO_COLOR` terminal and a greyscale print lose nothing.

The same holds for the other states: badges, the current page in the navigation, focus rings, selected tabs and
validation use ink, the state colours or shape, never the brand red.

### 4.7 Gradients

Gradients are welcome where they are deliberate: linear, crisp, inside the red family, on large surfaces.

| token | definition | for |
|-------|------------|-----|
| `gradient.depth` | `linear-gradient(180deg, #F33F49 0%, #8E1018 100%)` | the mark on light grounds, the macOS tile |
| `gradient.depth-on-dark` | `linear-gradient(180deg, #F33F49 0%, #B00921 100%)` | the mark on dark grounds |
| `gradient.strata` | `linear-gradient(180deg, #FE7373 0 20%, #F33F49 20% 40%, #D4002D 40% 60%, #B00921 60% 80%, #8E1018 80% 100%)` | the strata band: five hard steps, every layer |
| `gradient.ink` | `linear-gradient(180deg, #1F1B19 0%, #0C0908 100%)` | dark hero surfaces, the social card |

`--torb-gradient-mark` in `tokens.css` is the depth gradient of the current theme.

- **They appear on:** the mark from 48 px up, the macOS tile, the site's hero and social cards, the strata band (8 to
  40 px tall) under heroes and cards, release visuals.
- **They never appear on:** text (no gradient text), buttons, badges, inputs, tabs, code, syntax, diagnostics, charts,
  state indicators, favicons, icons up to 48 px, the terminal, or behind running text.
- **No radial gradient** (it reads as glow), **no animated gradient**, no mesh, no blur, and no gradient that leaves
  the red family: red into violet and red into orange are other brands' signatures.

## 5. Type

| role | face | weights | licence |
|------|------|---------|---------|
| display, headings, the wordmark | **Chivo** | 700, 800; 900 for the largest hero | SIL OFL 1.1, github.com/Omnibus-Type/Chivo |
| text and UI | **Chivo** | 400 body, 500 UI labels, 600 strong | same |
| code, identifiers, commands | **Chivo Mono** | 400 code, 500 labels, 700 emphasis | SIL OFL 1.1, github.com/Omnibus-Type/ChivoMono |

**Why Chivo.** One family for prose and code: the text face and the code face share a skeleton, so a page that mixes
them has one voice. Its horizontal terminals match the straight cuts of the mark, its heavy weights hold at billboard
size, and its 400 is plain enough for long documentation. And it is free of other developer brands: Inter, Fira,
Source, Lato, Roboto, DM, Geist, JetBrains, Space, Lexend and Outfit each belong to a language or tool already, and
Archivo with Martian Mono is Bun's pair.

| token | face | size | line height | weight | tracking |
|-------|------|------|-------------|--------|----------|
| `display` | Chivo | 64 px (4rem) | 1.05 | 800 | −0.02em |
| `heading-1` | Chivo | 44 px | 1.1 | 800 | −0.015em |
| `heading-2` | Chivo | 32 px | 1.2 | 700 | −0.01em |
| `heading-3` | Chivo | 24 px | 1.3 | 700 | −0.005em |
| `heading-4` | Chivo | 20 px | 1.35 | 600 | 0 |
| `body` | Chivo | 17 px | 1.6 | 400 | 0 |
| `ui` | Chivo | 15 px | 1.45 | 500 | 0 |
| `small` | Chivo | 13 px | 1.45 | 400 | 0.005em |
| `code` | Chivo Mono | 15 px | 1.6 | 400 | 0 |
| `code-dense` | Chivo Mono | 14 px | 1.5 | 400 | 0 |

- **Measure:** at most 72 characters of body text per line.
- **Code always has the slashed zero** (`font-feature-settings: "zero" 1`, `--torb-font-mono-features`): Chivo Mono's
  default zero is too close to its O. Font files for download and screenshots have the feature frozen in.
- **Identifiers in running text are Chivo Mono.** In Chivo, capital I and lowercase l look alike; in the mono face they
  do not.
- **Self-hosted** WOFF2, subset to Latin and the symbols the docs use, served from torb.dev: no request to a font CDN.
- **Fallbacks:** `Chivo, ui-sans-serif, system-ui, "Segoe UI", sans-serif` and
  `"Chivo Mono", ui-monospace, "Cascadia Mono", Consolas, monospace`.

## 6. Surfaces

**Firm, clear lines; flat surfaces; hairlines over shadows; no glow.**

- **Depth comes from surface steps and hairlines:** background, surface, raised, one step of the neutral scale apart,
  split by 1 px `border` lines. A card at rest has a hairline and no shadow.
- **The top rule:** a 3 px Torb Red line along the top edge of every page of torb.dev, packages.torb.dev and
  git.torb.dev. It is the top layer, and a tab that shows it is recognisable before the logo loads.
- **Radii:** 0 for the mark, the top rule, the strata band and tables; 2 px for buttons, inputs, code blocks and chips;
  4 px for cards, menus and dialogs. Nothing larger, except where a platform imposes it (the macOS tile) and avatars,
  which are circles.
- **Exactly two shadows, both subtle:** `shadow.raised` (`0 1px 2px rgb(20 17 15 / 0.06), 0 1px 1px rgb(20 17 15 /
  0.04)`) for what floats while it is used (a sticky header after scrolling, a dragged item), and `shadow.overlay`
  (`0 4px 12px rgb(20 17 15 / 0.10), 0 1px 3px rgb(20 17 15 / 0.08)`) for menus, popovers and the search dialog. The
  dark theme replaces both with a 1 px ring (`#37312F`, and `#4F4947` plus a dark offset for overlays): a soft shadow on
  near-black reads as a smudge.
- **Focus:** a 2 px solid outline in `focus`, offset by 2 px, shown at once and never as a blurred halo.
- **Scrims:** ink at 40% behind a dialog, without backdrop blur.
- **Never:** glow (a blurred coloured shadow), coloured shadows, inner shadows, heavy drop shadows, neumorphism,
  glassmorphism, backdrop blur.

## 7. Motion

**Sleek and quick:** a movement starts fast and settles exactly where it stops, with no overshoot and no bounce.

| token | value | for |
|-------|-------|-----|
| `duration.instant` | 80 ms | press feedback, colour on click |
| `duration.quick` | 140 ms | hover, the link underline, borders, an icon swap |
| `duration.standard` | 200 ms | menus, popovers, tabs, tooltips entering |
| `duration.exit` | 120 ms | the same leaving: faster than they came |
| `duration.layout` | 260 ms | panels, disclosures, the docs sidebar on small screens |
| `duration.signature` | 480 ms | the logo assembling on the front page, once per visit |
| `easing.exact` | `cubic-bezier(0.2, 0, 0, 1)` | everything that enters or moves |
| `easing.exit` | `cubic-bezier(0.4, 0, 1, 1)` | everything that leaves |

- **What animates:** a link's underline thickens from 1 to 2 px and deepens to `link-underline-hover` (quick); the
  borders and backgrounds of buttons, inputs and rows on hover (quick); menus and popovers fade in with a 4 px rise
  (standard, exact) and leave faster (exit); the copy button swaps to a check mark for 1.2 s.
- **The logo's one animation**, on the front page's hero, once: the disc is there, the bar cuts across from left to
  right, then the stem drops (signature, exact).
- **Nothing glows or pulses.** No parallax, no scroll-jacking, no springs, no animated gradients, no blur transitions,
  no page transitions, no skeleton shimmer, nothing that loops forever. Text, code and output appear at once.
- **`prefers-reduced-motion: reduce`** sets every duration to 0 except `instant`, which fades keep; the logo is shown
  assembled, and smooth scrolling is off. `tokens.css` does this by itself.

## 8. Buttons

| kind | light | dark | for |
|------|-------|------|-----|
| **call to action** | Torb Red, paper label (5.40:1); hover red-700, pressed red-800 | the same | the main call to action of a page, and nothing else |
| primary | ink, paper label; hover `#37312F`, pressed `#4F4947` | paper, ink label; hover `#E3DEDD`, pressed `#CFC9C7` | the main action of a tool: Run, Copy `torb add`, Publish |
| secondary | transparent, 1 px `border-control`, ink label; hover `surface-hover` | the same in the dark tokens | every other action |
| quiet | no border, hover `surface-hover` | the same | toolbars, dense lists |
| disabled | `#F2EDEC`, label `#A9A3A1` | `#1F1B19`, label `#68625F` | not available |

**The red button is for the main calls to action only**: "Get started", "Install TorbScript 0.2", "Download". At most
one per view, and never in a dialog, a form's error state, a tool's toolbar, the registry's package pages or the
forge. Every other button is neutral. A red button that means "go" next to red that means "error" is the one place the
brand could be mistaken for a state, and this rule keeps them apart.

- **Shape:** 36 px tall, 16 px horizontal padding, radius 2 px, label in the `ui` type (Chivo 500, 15 px), no
  gradient, no shadow.
- **States:** hover and pressed change the background (quick, then instant); focus is the 2 px outline of section 6; a
  pressed button does not move.

## 9. Code

The token kinds are the ones `torb highlight` emits. One lightness per theme keeps the texture even; the keyword is the
only token in the brand's hue.

| token (semantic kinds) | light | on `#F9F6F5` | dark | on `#1F1B19` |
|------------------------|-------|--------------|------|--------------|
| keyword | `#B00921` | 6.72 | `#F97676` | 6.41 |
| type (type, interface, typeParameter, namespace) | `#006C6C` | 5.81 | `#67D2CC` | 9.49 |
| function (function, method) | `#2855AD` | 6.52 | `#8FBCFF` | 8.79 |
| string | `#207029` | 5.73 | `#8CD384` | 9.57 |
| number | `#925000` | 5.80 | `#F6B669` | 9.61 |
| case (enumMember) | `#7C3990` | 6.86 | `#D8A4E9` | 8.47 |
| parameter | `#4F4947` | 8.22 | `#CFC9C7` | 10.44 |
| variable, field | `#14110F` | 17.49 | `#F2EDEC` | 14.73 |
| comment (upright, not faded) | `#756E6C` | 4.65 | `#A9A3A1` | 6.87 |
| punctuation, operators | `#68625F` | 5.58 | `#A9A3A1` | 6.87 |

- **Every token reaches 4.5:1**, comments included: a comment is text somebody wrote to be read.
- **Mutation is an underline, not a colour.** The `mutable` modifier (a `var` binding, a `var` field, a `var fn`) is a
  1.5 px underline in the token's own colour, as the extension already draws it.
- **An error in code** is a wavy underline in the error text colour plus a mark in the gutter. Tokens are never
  recoloured: red text in a code block is always a keyword.

## 10. The terminal

**Status of this section: applied** (2026-09-28). `compiler/src/cli/color.trb` holds the palette and the rules below,
`compiler/src/cli/render.trb` renders the diagnostics, and `runtime/test.c` colours the report of `torb test` by the
same rules; `docs/tooling/the-torb-command.md` ("`--color`") describes it for users. Not yet: a `help` label (no
diagnostic has one), a `skipped` test (the runner has none), OSC 8 hyperlinks and in-place progress lines.

**Decision: `torb` colours by meaning, in the 16 ANSI colours, and never in the brand red.** In a terminal red means
error and nothing else: no red banner, no red `torb`, no red progress. The name and the version are bold.

| element | example | SGR | truecolor light | truecolor dark |
|---------|---------|-----|-----------------|----------------|
| the word `error` | `error: ...` | `1;31` bold red | `#B00921` | `#FFA6A3` |
| the primary caret line | `^^^^^` | `31` red | `#B00921` | `#FFA6A3` |
| the word `warning` (`torb lint`) | `warning: ...` | `1;33` bold yellow | `#7D5000` | `#EEB976` |
| a warning's caret line | `^^^^^` | `33` yellow | `#7D5000` | `#EEB976` |
| a note's marker | `  = ` | `1;36` bold cyan | `#006770` | `#78D6DB` |
| a `help` label, when there is one | `help:` | `1;32` bold green | `#006E30` | `#92D7A0` |
| the message after the word | `` `count` is a `const` ... `` | `1` bold | ink | paper |
| the arrow, the gutter bar, the line numbers | ` --> `, the bar, `3` | `2` faint | `#78716F` | `#857E7C` |
| a clean summary | `no problems`, a finished build | `32` green | `#006E30` | `#92D7A0` |
| a summary with problems | `1 problem in 1 of 1 file` | `1` bold | ink | paper |
| progress verbs, the name `torb` | `Building`, `torb 0.2.0` | `1` bold | ink | paper |
| a test's result | `ok` / `FAILED` / `skipped` | `32` / `1;31` / `33` | as the states | as the states |
| a type in the REPL | `Int` | `2` faint | `#78716F` | `#857E7C` |

```text
error: `count` is a `const`. Only a `var` binding can be changed     "error" bold red, the message bold
 --> src/main.trb:3:3                                                 " --> " faint, the location plain
  |                                                                   faint
3 |   count = 4                                                       "3 |" faint, the source plain
  |   ^^^^^                                                           "|" faint, the carets red
  = `const` is deep: through it nothing is assigned ...               "=" bold cyan, the note plain

1 problem in 1 of 1 file                                              bold
```

- **The gutter is faint, not bright black.** Several popular themes (Solarized among them) make bright black the
  background colour, so a gutter in `90` disappears; a terminal without faint shows it in the default colour, which
  loses nothing.
- **Colour is always redundant.** Every diagnostic starts with its word, carets mark the span, the summary counts
  problems in words. A `NO_COLOR` run loses nothing.

**When colour is on.** The first rule that applies decides:

1. `--color=always|never|auto` on the command line.
2. `FORCE_COLOR` or `CLICOLOR_FORCE` set to a non-empty value other than `0`: on.
3. `NO_COLOR` set to a non-empty value: off (no-color.org), `NO_COLOR=0` included.
4. Otherwise on when the stream is a terminal and `TERM` is not `dumb`. On Windows `torb` turns on virtual-terminal
   processing and prints plain text if that fails.

OSC 8 hyperlinks on the ` --> file:line:col` location and in-place progress lines (at most 10 updates a second) follow
the same rules, and only on a terminal.

**The 16 colours by default, truecolor when the ground is known.** Sixteen colours leave the actual hues to the
terminal's theme, which knows its background and the user's accessibility settings; a 24-bit colour chosen without
knowing the background can vanish on it. So `torb` prints 24-bit SGR (`38;2;r;g;b`) only when both hold: `COLORTERM`
is `truecolor` or `24bit`, and the ground is known, from `TORB_BACKGROUND=light|dark` or from the background field of
`COLORFGBG` (0 to 6 and 8 dark, 7 and 9 to 15 light). The truecolor values are the state text tokens above, the same
colours the web uses; the brand red is not among them.

**The terminal scheme.** For the places that draw terminal output themselves (the playground's run pane, CLI
transcripts in the docs, the terminal colours of the VS Code themes), `terminal.scheme` in the tokens is a complete
16-colour scheme per ground. Every slot a program writes text in reaches 4.5:1 on its background; white and bright
white are ground colours in the light scheme, black in the dark one.

| slot | light | on paper | dark | on ink |
|------|-------|----------|------|--------|
| red / bright red | `#B00921` / `#8E1018` | 7.12 / 9.26 | `#FE7373` / `#FFA6A3` | 7.09 / 10.06 |
| green / bright green | `#006E30` / `#278445` | 6.33 / 4.63 | `#4FB169` / `#92D7A0` | 7.01 / 11.15 |
| yellow / bright yellow | `#7D5000` / `#A16800` | 6.86 / 4.61 | `#D29135` / `#EEB976` | 7.02 / 10.60 |
| blue / bright blue | `#125CA1` / `#2176C9` | 6.74 / 4.60 | `#5DA2EC` / `#98C8FF` | 7.02 / 10.78 |
| magenta / bright magenta | `#7C3990` / `#A056B6` | 7.28 / 4.60 | `#C786DC` / `#D8A4E9` | 7.05 / 9.32 |
| cyan / bright cyan | `#006770` / `#00808B` | 6.52 / 4.64 | `#3CADB3` / `#78D6DB` | 7.00 / 11.14 |
| black / bright black | `#14110F` / `#68625F` | 18.54 / 5.91 | `#292422` / `#857E7C` | 1.23 / 4.72 |
| white / bright white | `#857E7C` / `#A9A3A1` | 3.93 / 2.45 | `#CFC9C7` / `#F9F6F5` | 11.49 / 17.49 |
| foreground, cursor | `#14110F` | 18.54 | `#F9F6F5` | 17.49 |
| background, selection | `#FFFDFC`, `#E3DEDD` | | `#14110F`, `#4F4947` | |

## 11. Applications

### torb.dev

- **Shell:** the top rule, then a header on `background` with the horizontal lockup, the navigation, search and a
  theme toggle. The theme follows `prefers-color-scheme`; the toggle sets `data-theme` on `<html>`.
- **Front page hero:** a `gradient.ink` band with the mark in its dark gradient, the line "One language, every layer."
  in `display`, a category line in `text-muted`, the install one-liner in a code block, the call-to-action button, and
  the strata band at its foot.
- **Body:** paper, ink, Chivo 400 at 17 px, headings in Chivo 700 and 800, links in ink with the carmine underline.
- **Replaces** the stylesheet `torb docs site` writes today (`compiler/src/reference/site.trb`): it includes
  `brand/tokens.css` and uses the roles, never raw hex.
- **Favicons and the manifest:** `favicon.svg` (the 16 px art), `favicon.ico` with 16, 32 and 48 px, an opaque
  `apple-touch-icon.png` at 180 px and a maskable 512 px icon (the paper mark inside the central 80% of a full-bleed
  depth gradient), both drawn in the site's slice; `theme-color` is paper for light and ink for dark, never red.

### The documentation (/learn, /docs, /reference)

- **Navigation:** the same shell; the sidebar on `surface`, the current page marked by a 2 px ink bar (state, so not
  red).
- **Callouts:** a tint, a 3 px leading border in the accent, an icon and a label: note is info, tip is success,
  warning is warning, error is error. `status: planned` and `draft` banners use info and warning.
- **Code blocks:** `code` surface, a hairline, radius 2 px, a copy button, the syntax colours of section 9, generated at
  build time by `torb highlight`. Compiler output is rendered in `terminal.scheme`, never as a screenshot.
- **Search:** a dialog with `shadow.overlay` and a scrim, no blur.

### packages.torb.dev

- **Shell:** the same header; after the lockup a label `packages` in Chivo Mono 500 in `text-muted`. No sub-brand and
  no second logo.
- **Package pages:** package names are code, so Chivo Mono 700; versions in tabular figures. The main action (copy
  `torb add owner/name`) is a primary ink button, not red. The capability summary is a row of neutral chips.
- **States:** a yanked version uses the warning tint and the word "yanked"; a security advisory is the error block of
  section 4.6.

### git.torb.dev (Forgejo)

- **Theme files:** `theme-torb-light.css` and `theme-torb-dark.css` under `custom/public/assets/css/`, each importing
  Forgejo's own light or dark theme and overriding only variables; registered in `app.ini` under
  `[ui] THEMES = torb-auto, torb-light, torb-dark` with `DEFAULT_THEME = torb-auto`.
- **Primary is ink, not red.** `--color-primary` and its steps come from the neutral scale. Forgejo uses red for
  closing issues and deleting branches, and a red primary would make every button look destructive.
- **States:** `--color-red` is the error text token, `--color-green` success, `--color-yellow` warning, `--color-blue`
  info. The navigation bar is `background` with the 3 px top rule. Fonts are Chivo and Chivo Mono.
- **Logo and favicon:** `custom/public/assets/img/logo.svg` is `brand/logo/mark-flat.svg`, `favicon.svg` is
  `brand/logo/favicon.svg`, with the PNG sizes Forgejo expects from `brand/icons/`.
- **Verify the variable names** against Forgejo 16's `web_src/css/themes/` before shipping; they have been renamed
  between releases.

### VS Code

- **Marketplace icon:** `brand/icons/app-icon-256.png`, as `editors/vscode/images/icon.png`; the gallery banner is ink
  (`#14110F`) with the dark theme.
- **Language icon:** `contributes.languages[].icon` with the file icon, `brand/icons/file-icon.svg`, drawn on the 16 px
  grid it is shown at and once per ground: the paper page with its `#A9A3A1` hairline for light themes, a `#292422` page
  with a `#857E7C` hairline for dark ones, the flat mark on both. So `.trb` files carry the mark in the explorer and on
  tabs wherever the file icon theme has no icon of its own.
- **Walkthrough:** a `contributes.walkthroughs` entry ("Get started with TorbScript"): install `torb` (one step per
  system), create a program, run it, where to go next. Each step's media is an SVG drawn with the tokens, given as
  `{ "light": ..., "dark": ..., "hc": ..., "hcLight": ... }` so it follows the editor's theme: flat shapes, hairlines,
  Chivo outlined, the mark flat, at most one red per image, no screenshots of a particular theme. The walkthrough's
  buttons are VS Code's own; the brand adds no red button there. `editors/vscode/CONTRIBUTING.md` ("The brand") says
  where each file plugs in.
- **Two colour themes,** "TorbScript Light" and "TorbScript Dark": the theme roles, the syntax colours of section 9,
  the underline for `mutable`, and `terminal.scheme` for the integrated terminal. The semantic colours the extension
  forces on every theme today move into these two themes.
- **No brand colour in the status bar.**

### Binary icons

- **Windows:** `brand/icons/torb.ico` (16, 24, 32, 48 px as bitmaps, 256 px as PNG) embedded in the released
  `torb.exe` as a resource, with the product name and the version (`tools/windows/torb.rc`, RELEASE.md section 4).
  The installer is to write `brand/icons/file-icon.ico` as the `DefaultIcon` of `.trb`; it does not yet.
- **macOS:** an `.icns` from `brand/icons/app-icon-tile.svg` (16 to 1024 px, @1x and @2x); for Icon Composer two flat
  layers (the gradient square and the paper mark), with a dark variant of ink with the carmine mark. No glass or glow of
  our own.
- **Linux:** hicolor PNGs from `brand/icons/app-icon-*.png` and `scalable/apps/torb.svg` from `app-icon.svg`; the MIME
  type `text/x-torbscript` with the file icon.
- **Programs built by `torb build` get no TorbScript icon.** They belong to their authors.
- **GitHub Linguist:** when TorbScript qualifies, its language colour is `#D4002D`.

### Social cards

`brand/icons/social-card.svg` and `.png`, 1200 × 630 (Open Graph and X's large card): `gradient.ink`, the wordmark top
left, the line in Chivo 800 at 84 px, the category line in Chivo 400, `torb.dev` in Chivo Mono, the mark in its dark
gradient on the right, the strata band along the foot. Per section of the site only the line changes (a page title, a
package with its version, a release number); the forge's repository preview is the same layout at 1280 × 640.

## 12. The files

`brand/README.md` lists every file and what it is for: the logo in `brand/logo/`, the icons and the social card in
`brand/icons/`, and `brand/tokens.json` with `brand/tokens.css`. They are generated, not drawn by hand, by a script
outside the repository (it needs Node, a font outliner and a rasteriser, none of which the repository carries). Every
number that script uses is in this record and in `tokens.json`: a change starts here, and every file is regenerated
from it, never one SVG edited alone.

## 13. Slices

1. **The website and the docs:** `torb docs site` includes `brand/tokens.css`, the shell with the top rule and the
   lockup, the favicons and the manifest, the hero, the syntax colours.
2. **The registry:** the same shell and the package page of section 11.
3. **The forge's theme:** the two CSS files, `app.ini`, logo and favicon on git.torb.dev.
4. **`torb`'s colours:** the rules of section 10, in `compiler/src/cli/render.trb` and the summary lines, with a
   `--color` flag; `torb.ico` as a resource of `torb.exe` (both done).
5. **The VS Code extension:** the icons and the walkthrough (done), the two themes.

## 14. Open

- **A trademark search** in classes 9 and 42 before the launch. T-in-a-roundel marks exist in transit (Boston's MBTA
  "T"); the Orb differs because its T is cut out of a solid disc rather than set in a ring.
- **Pantone 199 C** is the nearest spot colour on screen; it has to be checked against a physical swatch before the
  first print run.
