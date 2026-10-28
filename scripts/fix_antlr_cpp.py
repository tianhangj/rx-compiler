"""Qualify ANTLR base classes when generated classes are named Lexer/Parser."""

import sys
from pathlib import Path

for name in ("Lexer", "Parser"):
    path = Path(sys.argv[1]) / f"{name}.cpp"
    source = path.read_text()
    source = source.replace(f": {name}(input) {{", f": antlr4::{name}(input) {{")
    if "\nnamespace rx {\n" not in source:
        source = source.replace(
            f'#include "{name}.h"\n',
            f'#include "{name}.h"\n\nnamespace rx {{\n',
            1,
        )
        source += "\n} // namespace rx\n"
    path.write_text(source)
