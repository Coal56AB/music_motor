"""Build a standalone Windows executable with CPython 3.7 and Nuitka."""
import subprocess
import sys
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    command = [
        sys.executable, '-m', 'nuitka',
        '--standalone', '--onefile', '--enable-plugin=pyside2',
        '--include-qt-plugins=platforms,styles,imageformats,audio,mediaservice',
        '--windows-console-mode=disable', '--msvc=latest',
        '--assume-yes-for-downloads',
        '--include-data-files=desktop/config.json=config.json',
        '--include-package-data=imageio_ffmpeg',
        '--output-dir=_service/build/nuitka',
        '--output-filename=MusicMotorStudio.exe', 'desktop/run.py',
    ]
    subprocess.run(command, cwd=str(root), check=True)
    print(root / '_service/build/nuitka/MusicMotorStudio.exe')


if __name__ == '__main__':
    main()
