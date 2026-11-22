#!/usr/bin/env python3
"""mpegv.readme made Aminet's: plain ASCII (the upload's checker reads
UTF-8, and ASCII reads the same everywhere), accents dropped and (C) for
the mark; every line 78 characters or fewer; its Version: line VERSION's,
or it fails (a release's tag and its readme kept in step).
(c) 2026 Brian King, MIT.

  aminet_readme.py VERSION IN OUT

Used by make aminet-check and by the release workflows (.github)."""
import re
import sys
import unicodedata


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    version = sys.argv[1].lstrip('v')
    text = open(sys.argv[2], encoding='latin-1').read().replace('\xa9', '(C)')
    text = unicodedata.normalize('NFKD', text).encode('ascii', 'ignore').decode()
    m = re.search(r'(?m)^Version:\s*(\S+)\s*$', text)
    if not m or m.group(1) != version:
        print('::error::the readme says Version: %s, not %s' % (m.group(1) if m else '(none)', version))
        return 1
    long = [i + 1 for i, line in enumerate(text.split('\n')) if len(line) > 78]
    if long:
        print('::error::readme lines over 78 characters: %s' % long)
        return 1
    open(sys.argv[3], 'w', encoding='ascii', newline='\n').write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
