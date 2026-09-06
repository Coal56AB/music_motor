"""Launch using CPython 3.7 x64: python desktop/run.py"""
import argparse
import os
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--screenshot", help="Save a screenshot and exit (offscreen supported)")
    parser.add_argument("--tab", type=int, default=0)
    parser.add_argument("--service-check", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.screenshot or args.service_check:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PySide2.QtCore import Qt, QTimer
    from PySide2.QtWidgets import QApplication
    from PySide2.QtGui import QFontDatabase

    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)
    QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps, True)
    app = QApplication(sys.argv)
    if os.environ.get("QT_QPA_PLATFORM") == "offscreen" and os.name == "nt":
        for font in ("segoeui.ttf", "segoeuib.ttf", "seguisym.ttf"):
            QFontDatabase.addApplicationFont(
                str(Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts" / font)
            )
    from app.main_window import MainWindow

    settings_path = Path(args.service_check).with_suffix(".settings.json") if args.service_check else None
    window = MainWindow(settings_path=settings_path, auto_connect=False)
    window.show()
    window.tabs.setCurrentIndex(args.tab)
    if args.service_check:
        from _service_smoke import check
        QTimer.singleShot(0, lambda: check(app, window, args.service_check))
    if args.screenshot:

        def capture():
            Path(args.screenshot).parent.mkdir(parents=True, exist_ok=True)
            window.grab().save(args.screenshot)
            window.client.close()
            app.quit()

        QTimer.singleShot(1200, capture)
    return app.exec_()


if __name__ == "__main__":
    sys.exit(main())
