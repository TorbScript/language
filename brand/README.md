# The TorbScript brand files

The logo, the icons and the design tokens of TorbScript. Why each looks the way it does, and the rules for using them,
are in [docs/design/BRAND.md](../docs/design/BRAND.md). Every SVG here is outlined (no font is embedded or loaded),
references no other file, and carries a `<title>`.

The mark is the **Orb**: a carmine disc cut by one T-shaped gap. Its primary form is the depth gradient; the flat
mark is for small sizes, one-colour work and print.

## `logo/`

| File | What it is for |
|------|----------------|
| `mark.svg` | The mark in the depth gradient: the primary form, on light grounds, from 48 px up |
| `mark-on-dark.svg` | The mark in the depth gradient for dark grounds (its foot is lifted from red-800 to red-700) |
| `mark-flat.svg` | The mark in flat Torb Red: below 48 px, on any ground, and wherever a gradient cannot be reproduced |
| `mark-mono-black.svg` | One colour, black: print, stamps, engraving, fax-grade copies |
| `mark-mono-white.svg` | One colour, white: knocked out of photos and dark print |
| `wordmark.svg` | "TorbScript" in Chivo 700, ink, for use next to the mark when the lockup does not fit the layout |
| `wordmark-on-dark.svg` | The wordmark in paper for dark grounds |
| `lockup.svg` | Horizontal lockup, gradient mark and ink wordmark: the default logo on light grounds |
| `lockup-on-dark.svg` | Horizontal lockup for dark grounds |
| `lockup-flat.svg` | Horizontal lockup with the flat mark: small headers, email, documents printed in colour |
| `lockup-mono-black.svg` | Horizontal lockup in black |
| `lockup-mono-white.svg` | Horizontal lockup in white |
| `lockup-stacked.svg` | The mark above the wordmark: square spaces, title slides, stickers |
| `lockup-stacked-on-dark.svg` | The stacked lockup for dark grounds |
| `favicon.svg` | The flat mark drawn on the 16 px grid, every cut on a whole pixel: browser tabs, editor file lists |
| `favicon-mono.svg` | The favicon in black: Safari's pinned tabs (`mask-icon`) and other one-colour slots |

## `icons/`

| File | What it is for |
|------|----------------|
| `app-icon.svg` | The application icon, scalable: the mark in the depth gradient, full bleed (Linux `scalable/apps`) |
| `app-icon-16.png` ... `app-icon-512.png` | The application icon at 16, 24, 32, 48, 64, 128, 256 and 512 px. 16 to 48 are the flat mark drawn on each size's pixel grid; 64 and up are the depth gradient |
| `torb.ico` | The Windows icon of `torb.exe`: 16, 24, 32 and 48 px as 32-bit bitmaps, 256 px as PNG |
| `app-icon-tile.svg`, `app-icon-tile-512.png` | The mark in paper on a depth-gradient rounded square on Apple's 1024 grid: the macOS application icon |
| `file-icon.svg` | The icon of a `.trb` file: a page with a straight corner cut, a hairline, and the flat mark (drawn on the 48 px grid) |
| `file-icon-16.png` ... `file-icon-256.png` | The file icon at 16, 24, 32, 48, 64, 128 and 256 px, each drawn on its own pixel grid |
| `file-icon.ico` | The file icon for Windows' `DefaultIcon` of `.trb`: 16, 24, 32 and 48 px as bitmaps, 256 px as PNG |
| `favicon.ico` | The website's `favicon.ico`: the pixel art of 16, 32 and 48 px |
| `apple-touch-icon.png` | The website's icon on iOS, 180 px and opaque: the paper mark on a full-bleed depth gradient |
| `app-icon-maskable.svg`, `app-icon-maskable-192.png`, `app-icon-maskable-512.png` | The maskable icon of the web manifest: a full-bleed depth gradient with the paper mark at 62.5%, inside the safe zone of every mask |
| `social-card.svg`, `social-card.png` | The Open Graph and social preview card, 1200 x 630 |

## `fonts/`

| File | What it is for |
|------|----------------|
| `chivo.woff2` | Chivo, variable from 100 to 900: prose, headings and the UI of the website and the docs |
| `chivo-mono.woff2` | Chivo Mono, variable from 100 to 900: code, identifiers and commands |
| `OFL.txt` | The SIL Open Font License 1.1 both are published under, with their copyright line |

Both are subset to Basic Latin, Latin-1, Latin Extended-A, the general punctuation, the arrows and the mathematical
operators the fonts carry, with every OpenType feature kept (the slashed zero is `"zero" 1`), and compressed as WOFF2 -
by `pyftsubset` of fontTools from the fonts of github.com/Omnibus-Type/Chivo and Omnibus-Type/ChivoMono. The website
serves them itself, from `/assets/fonts/`: no page asks a font service for anything.

## Tokens

| File | What it is for |
|------|----------------|
| `tokens.json` | Every token: the logo's construction numbers, the red and neutral scales, the light and dark themes, the state colours, buttons, syntax colours, the terminal's colours, gradients, shadows, radii, borders, the type scale and motion |
| `tokens.css` | The same tokens as CSS custom properties (`--torb-*`), with a light theme, a dark theme chosen by `prefers-color-scheme`, and `data-theme="light"` or `"dark"` on `<html>` to override it. The site, the docs and the registry include this one file |

## Regenerating

These files are generated, not drawn by hand: a script outside the repository (it needs Node, a font outliner and a
rasteriser, none of which the repository carries) builds every file from the numbers in BRAND.md and the tokens.
Change a number in BRAND.md first, then regenerate every file from it; never edit one SVG alone.
