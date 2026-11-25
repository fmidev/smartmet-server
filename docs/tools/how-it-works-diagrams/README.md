# Diagrams of how-smartmet-server-works.md

`make_diagrams.py` writes the SVG figures of
[how-smartmet-server-works.md](../../how-smartmet-server-works.md):

```bash
python3 make_diagrams.py ../../images/how-it-works
```

Edit the script rather than the SVG files. To check a figure, render it with
`rsvg-convert -z 1.5 file.svg -o file.png`.
