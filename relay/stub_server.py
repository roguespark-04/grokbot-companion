#!/usr/bin/env python3
"""Thin re-export — use server.py (v1 audio relay).

Kept so older docs/scripts that invoke stub_server.py still work.
"""
from server import app, main

if __name__ == "__main__":
    main()
