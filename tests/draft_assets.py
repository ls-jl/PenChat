"""Read this test fixture's persisted pending images without creating messages."""
import json
import pathlib
import sqlite3


def pending_images(directory):
    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
        return [json.loads(row[0]) for row in db.execute(
            'select image_json from draft_attachments order by draft_key,position,id')]
