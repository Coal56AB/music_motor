"""Discover external songs without loading their contents."""
from pathlib import Path


def discover_songs(folder):
    root = Path(folder)
    files = [p for p in root.rglob('*') if p.is_file() and
             (p.name.lower().endswith('.motor.json') or p.suffix.lower() in ('.mid', '.midi'))]
    projects = {str(p.relative_to(root))[:-11].casefold() for p in files
                if p.name.lower().endswith('.motor.json')}
    result = []
    for path in files:
        relative = str(path.relative_to(root))
        project = relative.lower().endswith('.motor.json')
        name = relative[:-11] if project else str(Path(relative).with_suffix(''))
        if not project and name.casefold() in projects:
            continue
        result.append((name, path))
    return sorted(result, key=lambda item: (item[0].casefold(), str(item[1])))
