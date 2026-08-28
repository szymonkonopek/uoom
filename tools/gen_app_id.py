#!/usr/bin/env python3
"""Print the 16-hex-char APP_ID the UNA build system wants for an app name.

The SDK documents exactly this derivation, so keep it rather than inventing
random IDs -- rebuilding from a clean tree must not change the app's identity
on the watch.
"""
import hashlib
import sys

name = sys.argv[1] if len(sys.argv) > 1 else "UOOM"
print(hashlib.md5(name.encode()).hexdigest().upper()[:16])
