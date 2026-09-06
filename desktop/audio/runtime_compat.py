"""Read bundled resampling filters without temporary resource-file handles.

resampy 0.4 + importlib_resources can extract an NPZ to a temporary file under
Nuitka. Its lazy np.load handle stays open during cleanup on Windows. Loading
the arrays under a context manager avoids that lock and uses packaged data.
"""
from pathlib import Path
import numpy as np


def prepare_resampling_filters():
    import resampy.filters as filters
    for name in ("kaiser_best", "kaiser_fast"):
        if name not in filters.FILTER_CACHE:
            path = Path(filters.__file__).parent / "data" / (name + ".npz")
            with np.load(str(path)) as data:
                filters.FILTER_CACHE[name] = (
                    data["half_window"], data["precision"], data["rolloff"]
                )
