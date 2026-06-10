#!/bin/bash
# Build the mock renderer and produce PNG mockups of every dashboard page.
set -e
cd "$(dirname "$0")"
mkdir -p mockups
make mock 2>&1 | grep -vE "^CC |^cc " | tail -5
./render-mock
python3 tools/raw2png.py mockups
rm -f mockups/page_*.raw
ls -la mockups/
