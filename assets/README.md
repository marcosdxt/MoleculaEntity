# assets

| File | What for |
|---|---|
| `logo.svg` · `logo-dark.svg` | the mark with the name, for the top of the README and for anyone citing the project |
| `logo-mark.svg` | just the molecule, square — repository avatar, icon, favicon |
| `architecture.svg` · `architecture-dark.svg` | the architecture view in the README |

## Two copies, one source

The light `*.svg` files are the originals and **switch colors on their own**
through `prefers-color-scheme` — they work in any browser, including outside this
repository.

The `*-dark.svg` files are derived, and exist only because of GitHub: the README
uses `<picture>`, which is the path GitHub documents for light and dark themes and
which doesn't depend on its sanitizer preserving the `<style>` inside the SVG.

Edited an original? Regenerate:

```sh
python3 assets/make-dark-variant.py assets/logo.svg
python3 assets/make-dark-variant.py assets/architecture.svg
```

## Conventions

- **No embedded or external fonts.** The text uses the system font stack
  (`system-ui`), so the file stays small and needs no network. In exchange, the
  drawing shifts by a few pixels between systems — for a library logo, that's a
  trade worth making.
- **No raster.** Everything is vector, and the mark stays legible at 32 px.
- **Palette:** blue `#3b82f6` (the nucleus), teal `#14b8a6` (the highlighted
  atom), slate for structure and text. The dark equivalents live in each file's
  `@media` block.

To export a PNG (for a talk, a badge, a slide):

```sh
inkscape --export-type=png --export-width=1200 assets/logo.svg
```
