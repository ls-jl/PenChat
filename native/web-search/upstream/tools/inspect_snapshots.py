"""Validate bounded response captures without printing query strings or URL tokens."""
import argparse
import json
import os
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit

ENGINES = {'baidu', 'bing_cn', 'duckduckgo', 'brave', 'yahoo', 'so360', 'sogou'}


def read_regular(path, limit):
    import stat
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise ValueError('not a regular bounded capture file')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(limit + 1)
        if len(data) > limit:
            raise ValueError('capture exceeds size limit')
        return data
    finally:
        os.close(fd)


class Outline(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.in_title = False
        self.title = ''
        self.scripts = []

    def handle_starttag(self, tag, attrs):
        if tag == 'title':
            self.in_title = True
        if tag == 'script' and len(self.scripts) < 16:
            src = dict(attrs).get('src')
            if src:
                value = urlsplit(src)
                self.scripts.append((value.netloc + value.path)[:256])

    def handle_endtag(self, tag):
        if tag == 'title':
            self.in_title = False

    def handle_data(self, data):
        if self.in_title:
            self.title = (self.title + data)[:512]


def inspect(path):
    engine = path.stem
    if engine not in ENGINES:
        raise ValueError('unknown engine filename')
    metadata = json.loads(read_regular(path, 32768))
    if metadata.get('snapshot_format') != 1 or metadata.get('engine') != engine:
        raise ValueError('metadata identity mismatch')
    body = read_regular(path.with_suffix('.html'), 8 * 1024 * 1024)
    digest = 14695981039346656037
    for byte in body:
        digest = ((digest ^ byte) * 1099511628211) & 0xffffffffffffffff
    if len(body) != metadata.get('stored_bytes') or f'{digest:016x}' != metadata.get('body_fnv1a64'):
        raise ValueError('body size/checksum mismatch; capture pair may be incomplete')
    received = metadata.get('received_body_bytes')
    if not isinstance(received, int) or received < len(body) or metadata.get('truncated') != (received > len(body)):
        raise ValueError('inconsistent body length flags')
    outline = Outline()
    outline.feed(body.decode('utf-8', errors='replace'))
    target = urlsplit(metadata.get('effective_url', ''))
    complete = metadata.get('complete_response') is True and metadata.get('truncated') is False
    return {'engine': engine, 'http_status': metadata.get('http_status'),
            'transport_status': metadata.get('transport_status'),
            'captured_at_unix_ms': metadata.get('captured_at_unix_ms'),
            'stored_bytes': len(body), 'complete_body': complete,
            'effective_host': target.hostname, 'effective_path': target.path[:256],
            'title': ' '.join(outline.title.split()), 'script_sources': outline.scripts,
            'note': 'A complete body still needs independent ad/result labeling.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--require-complete', action='store_true')
    args = parser.parse_args()
    rows = []
    failed = False
    for path in sorted(args.directory.glob('*.json')):
        if path.stem not in ENGINES:
            continue
        try:
            row = inspect(path)
            failed |= args.require_complete and not row['complete_body']
        except (OSError, ValueError, TypeError) as error:
            row = {'engine': path.stem, 'error': str(error)}
            failed = True
        rows.append(row)
    print(json.dumps(rows, ensure_ascii=False, indent=2))
    return 1 if failed or not rows else 0


if __name__ == '__main__':
    raise SystemExit(main())
