#!/usr/bin/env python3
"""The Aminet readme checked as the upload checks it: Aminet's copy of
mpegv.readme (made by aminet_readme.py) through sidick/aminet-release-
action's own readme_validator (ACTION_DIR: a checkout of it, the release
workflow's version), its Uploader: an example address (the real one is a
secret, put in at upload). (c) 2026 Brian King, MIT.

  aminet_check.py ACTION_DIR README CATEGORY ARCHIVE_NAME"""
import re
import sys


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    action_dir, readme, category, archive = sys.argv[1:]
    sys.path.insert(0, action_dir + '/action')
    import readme_validator as rv

    text = open(readme, encoding='utf-8').read()     # (as the upload reads it)
    text = re.sub(r'(?m)^Uploader:( *).*$', r'Uploader:\1uploader@example.org (Brian King)', text)
    parsed = rv.parse(text)
    issues = rv.validate(parsed, category)
    issues += rv.validate_filename(archive)
    issues += rv.validate_filename(readme.split('/')[-1])
    issues += rv.validate_upload_extension(archive)
    for i in issues:
        print('  %s: line %s: %s' % (i.level, i.line, i.message))
    errors = [i for i in issues if i.level == 'error']
    if errors:
        print('  CHECK Aminet readme: %d error(s)' % len(errors))
        return 1
    print('  CHECK Aminet readme ok (%s, %s; %d warning(s))' % (archive, category, len(issues)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
