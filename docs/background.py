#!/usr/bin/env python3
"""Give SVGs from pdftocairo an opaque, rounded background, so they stay readable on GitHub's light and dark themes.

usage: background.py COLOUR FILE...
"""
import re
import sys

colour = sys.argv[1]
for path in sys.argv[2:]:
    svg = open(path).read()
    if 'id="background"' in svg:
        continue
    # The background goes first, so everything pdftocairo drew stays on top of it.
    svg = re.sub(r'(<svg[^>]*>)', r'\1\n<rect id="background" width="100%" height="100%" rx="8" fill="' + colour + '"/>', svg, count=1)
    open(path, 'w').write(svg)
