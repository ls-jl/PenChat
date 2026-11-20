"""Atomic, private checkpoints for disposable release scopes."""
import json
import os
from pathlib import Path
import shutil

def write_json(path,value):
 path=Path(path);pending=path.with_name(path.name+'.pending')
 try:
  fd=os.open(pending,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
  with os.fdopen(fd,'w') as stream:
   json.dump(value,stream,ensure_ascii=False,indent=2);stream.write('\n');stream.flush();os.fsync(stream.fileno())
  os.replace(pending,path)
  directory=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY)
  try:os.fsync(directory)
  finally:os.close(directory)
 finally:
  pending.unlink(missing_ok=True)

def require_space(path):
 if shutil.disk_usage(path).free<512*1024*1024:
  raise RuntimeError('Release scope not started: fewer than 512 MiB available for recovery records and artifacts')
