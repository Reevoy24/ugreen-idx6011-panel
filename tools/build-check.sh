#!/bin/bash
cd /mnt/c/Users/Basti/Desktop/ugreen-idx6011-pro-nas-display || exit 1
make 2>&1 | grep -E 'main\.c|error|warning' | grep -vi 'unused-result'
make 2>&1 | grep -E 'Built ug-paneld' || echo "BUILD FAILED"
