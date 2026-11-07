#!/usr/bin/env python3
"""Embed reviewed format docs and deterministic, fictional import examples.

Only the build uses Python. The device reads the same immutable resources and
creates the outer documentation ZIP with native miniz, without business data.
"""
import argparse
import io
import json
from pathlib import Path
import struct
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
DOCS = ('使用说明.md', '聊天格式.md', '预设格式.md', 'Markdown导入.md')


def encode(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(',', ':'), allow_nan=False).encode()


def crc(data):
    return f'{zlib.crc32(data) & 0xffffffff:08x}'


def origin(item, source):
    item['origin'] = {'source': source, 'checksum': crc(encode(item))}
    return item


def png():
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    # Tiny authored fixture; no photograph or user pixels enter these resources.
    rows = b''.join(b'\0' + b''.join(bytes((68, 120, 237, 255)) if (x//4+y//4)%2
                                   else bytes((236, 236, 236, 255)) for x in range(32)) for y in range(24))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 24, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


def archive(files):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, data in files.items():
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            bundle.writestr(entry, data)
    return output.getvalue()


def resources():
    version = json.loads((ROOT/'package.json').read_text())['version']
    result = {name: (ROOT/'docs/transfer-format'/name).read_bytes() for name in DOCS}
    image = png()
    stamp = 1767225600000  # Fictional fixed example timestamp, never current user activity.
    messages = []
    for seq, role, text in [(1, 'user', '请描述这张虚构的示例图。'),
                            (2, 'assistant', '这是一张蓝白相间的示例图，仅用于验证导入。')]:
        parts = [{'type': 'text', 'text': text}]
        if role == 'user':
            parts.append({'type': 'image', 'asset': 'example-image'})
        messages.append(dict(seq=seq, role=role, status='complete', message_type='text',
                             created_at=stamp, updated_at=stamp, text=text, reasoning='',
                             parts=parts, model={}, request={}, metadata={}))
    chat = origin(dict(id='penchat-format-example-chat-v1', title='格式规范示例聊天',
                       status='active', created_at=stamp, updated_at=stamp,
                       last_message_at=0, pinned_at=0, metadata={'chat_preferences': {}},
                       model={}, compression_model={}, messages=messages,
                       assets=[dict(id='example-image', file='assets/example.png', preview_file='',
                                    width=32, height=24, size_bytes=len(image), mime_type='image/png')]),
                  'penchat-format-example-chat-v1')
    files = {'conversations/example.json': encode(chat),
             'markdown/示例.md': '# 格式规范示例聊天\n\n## 用户\n\n请描述这张虚构的示例图。\n\n![图片](../assets/example.png)\n\n## 助手\n\n这是一张蓝白相间的示例图，仅用于验证导入。\n'.encode(),
             'assets/example.png': image}
    manifest = dict(format='penchat', version=1, kind='chats', app_version=version,
                    items=[dict(file='conversations/example.json', title=chat['title'], markdown='markdown/示例.md')],
                    files={name: dict(size=len(data), crc32=crc(data)) for name, data in files.items()})
    result['示例/示例聊天.penchat.zip'] = archive({**files, 'manifest.json': encode(manifest)})
    preset = origin(dict(id='penchat-format-example-preset-v1', name='格式规范示例预设', prompt_mode='inherit',
                         prompt='', model={}, model_when='empty', effort='', effort_when='empty', compression_model={}),
                    'penchat-format-example-preset-v1')
    result['示例/示例预设.penchat-presets.json'] = encode(dict(format='penchat', version=1, kind='presets', app_version=version, items=[preset]))
    result['示例/示例参考.md'] = '# 示例参考\n\n这是一份虚构的参考笔记，公式为 $a^2+b^2=c^2$。\n\n![示例图片](图片/示例.png)\n'.encode()
    result['示例/图片/示例.png'] = image
    assert len(result) == 8 and sum(map(len, result.values())) <= 256*1024
    for name, data in result.items():
        assert not name.startswith('/') and '..' not in name.split('/')
        if name.endswith('.md'):
            assert data.decode('utf-8') and b'\0' not in data
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    files = resources()
    lines = ['// Generated from docs/transfer-format and fictional examples. Do not edit.']
    for i, (name, data) in enumerate(files.items()):
        lines.append(f'static const unsigned char formatBytes{i}[] = {{')
        for at in range(0, len(data), 24):
            lines.append('  ' + ','.join(str(v) for v in data[at:at+24]) + ',')
        lines.append('};')
    lines.append('static const FormatResource formatFiles[] = {')
    for i, name in enumerate(files):
        lines.append('  {' + json.dumps(name, ensure_ascii=False) + f', formatBytes{i}, sizeof(formatBytes{i})' + '},')
    lines.append('};\n')
    content = '\n'.join(lines)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != content:
        args.output.write_text(content)


if __name__ == '__main__':
    main()
