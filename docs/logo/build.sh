#!/bin/sh
# Builds logo.tex into logo-light.svg and logo-dark.svg (needs xelatex, the Optima font, and poppler).
set -e
cd "$(dirname "$0")"
xelatex -interaction=nonstopmode -halt-on-error logo.tex > /dev/null
pdftocairo -svg -f 1 -l 1 logo.pdf logo-light.svg
pdftocairo -svg -f 2 -l 2 logo.pdf logo-dark.svg
rm -f logo.aux logo.log logo.pdf
