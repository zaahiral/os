#!/bin/sh
# Builds logo.tex into logo-light.svg and logo-dark.svg (needs xelatex, the Optima font, poppler and Python 3).
set -e
cd "$(dirname "$0")"
xelatex -interaction=nonstopmode -halt-on-error logo.tex > /dev/null
pdftocairo -svg -f 1 -l 1 logo.pdf logo-light.svg
pdftocairo -svg -f 2 -l 2 logo.pdf logo-dark.svg
# Each logo gets a background in its own theme's colour, so it stays readable if a viewer shows the other one.
python3 ../background.py '#ffffff' logo-light.svg
python3 ../background.py '#0d1117' logo-dark.svg
rm -f logo.aux logo.log logo.pdf
