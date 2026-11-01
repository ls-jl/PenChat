#!/usr/bin/env python3
"""Generate a private native header. Never echo configuration values."""
import ipaddress, json, os, pathlib, sys, urllib.parse

def main():
    source, target = sys.argv[1:]
    config = {}
    if source:
        config = json.loads(pathlib.Path(source).read_text())
        url = urllib.parse.urlsplit(config.get('endpoint', ''))
        if url.scheme != 'https' or not url.hostname or url.username or url.password or url.query or url.fragment:
            raise ValueError()
        token = config.get('token', '')
        if not isinstance(token, str) or not token or len(token) > 512 or any(ord(c) < 33 or ord(c) > 126 for c in token):
            raise ValueError()
        nodes = config.get('nodes', [])
        if not isinstance(nodes, list) or len(nodes) > 4:
            raise ValueError()
        for value in nodes:
            if not isinstance(value, str):
                raise ValueError()
            ipaddress.ip_address(value)
    data = '#pragma once\nnamespace penchat {\n'
    for key in ('endpoint', 'token'):
        data += 'static const char FeedbackBuild_' + key + '[] = ' + json.dumps(config.get(key, ''), ensure_ascii=True) + ';\n'
    data += 'static const char FeedbackBuild_nodes[] = ' + json.dumps(json.dumps(config.get('nodes', []))) + ';\n}\n'
    path = pathlib.Path(target)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != data:
        fd = os.open(path, os.O_CREAT | os.O_TRUNC | os.O_WRONLY, 0o600)
        with os.fdopen(fd, 'w') as f:
            f.write(data)

if __name__ == '__main__':
    try:
        main()
    except Exception:
        print('Invalid private feedback build configuration; values are not displayed.', file=sys.stderr)
        sys.exit(1)
