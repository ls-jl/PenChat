#!/usr/bin/env python3
"""Apply deterministic C++14 compatibility and safety overlays to locked MicroTeX sources.

Structured bindings and if-init expressions are lowered; upstream's existing
CLATEX_CXX17=false resource path is retained. The asprintf fallback uses free().
Indexed font lookups bound their inclusive binary search to the last row.
Cached pristine archives remain unchanged.
"""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]/'.deps/microtex/src'
for path in root.rglob('*'):
    if path.suffix not in ['.cpp','.h']: continue
    text=path.read_text()
    def lower(match):
        first,second,expr=match.groups()
        name='_penchat_pair_'+first+'_'+second
        return f'auto {name} = {expr}; auto {first} = {name}.first; auto {second} = {name}.second;'
    updated=re.sub(r'auto\s*\[\s*(\w+)\s*,\s*(\w+)\s*\]\s*=\s*([^;]+);',lower,text)
    updated=re.sub(r'if \(auto (\w+) = (dynamic_pointer_cast<[^>]+>\([^;]+\)); \1 != nullptr\)', r'if (auto \1 = \2)', updated)
    updated=updated.replace("delete userdata_fallback;", "free(userdata_fallback);")
    if path == root/'utils/indexed_arr.h':
        updated=updated.replace('if (_raw == nullptr) return nullptr;',
                                'if (_raw == nullptr || _rows == 0) return nullptr;')
        updated=updated.replace('int     l = 0, h = _rows;',
                                'int     l = 0, h = static_cast<int>(_rows) - 1;')
    if path == root/'core/macro.cpp':
        updated=updated.replace('for (const auto& i : _commands) delete i.second;\n}',
                                'for (const auto& i : _commands) delete i.second;\n  _commands.clear();\n}')
    if path == root/'core/macro_def.cpp' and 'PenchatMacroCleanup' not in updated:
        # The registry allocates before main, including when startup fails before
        # RichText exists. Construct its owner in the same TU after the registry
        # so teardown precedes map destruction. Normal LaTeX release clears it.
        updated+='\nnamespace {\nstruct PenchatMacroCleanup {\n  ~PenchatMacroCleanup() { tex::MacroInfo::_free_(); }\n};\nstatic PenchatMacroCleanup penchatMacroCleanup;\n}\n'
    if updated != text: path.write_text(updated)
