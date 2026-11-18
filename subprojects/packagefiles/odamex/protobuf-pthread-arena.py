"""Release Protobuf's pthread key when the client library unloads."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding='utf-8')
old = '''  static internal::ThreadLocalStorage<ThreadCache>* thread_cache_ =
      new internal::ThreadLocalStorage<ThreadCache>();
  return *thread_cache_->Get();'''
new = '''  static internal::ThreadLocalStorage<ThreadCache> thread_cache_;
  return *thread_cache_.Get();'''
assert source.count(old) == 1, 'Unexpected Protobuf arena implementation'
Path(sys.argv[2]).write_text(source.replace(old, new), encoding='utf-8')
