#!/usr/bin/env python3
"""Compatibility entry for assistant in-place selection and original user menus."""
from test_text_selection import run

if __name__ == '__main__':
    for size in [(960, 266), (640, 360), (568, 1210)]:
        run(*size)
