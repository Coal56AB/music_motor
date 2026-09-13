import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def songs_directory():
    if "__compiled__" in globals() or getattr(sys, "frozen", False):
        return Path(sys.argv[0]).resolve().parent / "Songs"
    return ROOT.parent / "Songs"


def settings_file():
    if "__compiled__" in globals() or getattr(sys, "frozen", False):
        return Path(os.environ.get("APPDATA", str(Path.home()))) / "MusicMotorStudio" / "settings.json"
    return ROOT / "user_settings.json"


def load_settings(path=None):
    with open(str(ROOT / "config.json"), encoding="utf8") as f:
        config = json.load(f)
    path = Path(path) if path else settings_file()
    if path.exists():
        try:
            with open(str(path), encoding="utf8") as f:
                saved = json.load(f)
            if isinstance(saved, dict):
                config.update(saved)
        except (ValueError, OSError):
            pass
    # Clamp the settings that affect routing; never assume an arbitrary JSON is valid.
    for name in ("installed_mask", "music_mask"):
        config[name] = int(config[name]) & 63
    config["music_mask"] = config["installed_mask"]
    config.pop("simulation", None)
    config["disable_after_stop"] = True
    if config.get("motor_layout") not in ("horizontal", "vertical"):
        config["motor_layout"] = "horizontal"
    for name, default in [("names", "Мотор"), ("steps_per_revolution", 200), ("directions", 0)]:
        if not isinstance(config.get(name), list) or len(config[name]) != 6:
            config[name] = [default] * 6
    config["steps_per_revolution"] = [max(1, int(x)) for x in config["steps_per_revolution"]]
    config["directions"] = [int(bool(x)) for x in config["directions"]]
    config["microstep"] = max(1, int(config.get("microstep", 1)))
    config["microstep_raw"] = int(config.get("microstep_raw", 0)) & 7
    config['note_hold_ms'] = max(0, min(5000, int(config.get('note_hold_ms', 250))))
    config['min_frequency'] = max(20, min(1199, int(config.get('min_frequency', 20))))
    config['max_frequency'] = max(config['min_frequency'] + 1, min(1200, int(config.get('max_frequency', 1200))))
    return config


def save_settings(config, path=None):
    path = Path(path) if path else settings_file()
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".tmp")
    with open(str(temp), "w", encoding="utf8") as f:
        json.dump(config, f, ensure_ascii=False, indent=2)
    temp.replace(path)
