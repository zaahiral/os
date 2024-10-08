#!/bin/sh
# Builds hierarchy.tex into one SVG per diagram (needs xelatex, the Optima and Courier fonts, and poppler).
set -e
cd "$(dirname "$0")"
xelatex -interaction=nonstopmode -halt-on-error hierarchy.tex > /dev/null
i=1
for name in kernel filesystem networking ipv4 userspace shell toolchain selfhost; do
    pdftocairo -svg -f $i -l $i hierarchy.pdf $name.svg
    i=$((i+1))
done
rm -f hierarchy.aux hierarchy.log hierarchy.pdf
