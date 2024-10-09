#!/bin/sh
# Builds hierarchy.tex into a light and a dark SVG per diagram (needs xelatex, the Optima and Courier fonts, poppler and
# Python 3). The README picks one with the reader's colour scheme.
set -e
cd "$(dirname "$0")"
xelatex -interaction=nonstopmode -halt-on-error hierarchy.tex > /dev/null
xelatex -interaction=nonstopmode -halt-on-error -jobname=hierarchy-dark '\def\darkcharts{}\input{hierarchy.tex}' > /dev/null
i=1
for name in kernel filesystem networking ipv4 userspace shell toolchain selfhost; do
    pdftocairo -svg -f $i -l $i hierarchy.pdf $name.svg
    pdftocairo -svg -f $i -l $i hierarchy-dark.pdf $name-dark.svg
    i=$((i+1))
done
# pdftocairo leaves out the page colour, so each SVG gets a background of its own. A chart shown in the other theme is
# then still readable.
python3 ../background.py '#ffffff' kernel.svg filesystem.svg networking.svg ipv4.svg userspace.svg shell.svg toolchain.svg selfhost.svg
python3 ../background.py '#0d1117' *-dark.svg
rm -f hierarchy.aux hierarchy.log hierarchy.pdf hierarchy-dark.aux hierarchy-dark.log hierarchy-dark.pdf
