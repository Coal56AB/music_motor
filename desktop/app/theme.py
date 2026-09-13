STYLE = """
QWidget { background:#101824; color:#dce6f2; font-family:"Segoe UI"; font-size:12px; }
QMainWindow { background:#101824; }
QLabel#title { font-size:27px; font-weight:700; color:#eff5ff; }
QLabel#subtitle { color:#90a3bc; }
QFrame#motorCard { background:#1a2637; border:1px solid #314057; border-radius:9px; }
QFrame#motorCard QLabel, QFrame#motorCard QCheckBox { background:transparent; }
QFrame#motorCard QPushButton { min-height:26px; padding:5px 8px; font-size:14px; }
QFrame#motorCard QComboBox,QFrame#motorCard QDoubleSpinBox { min-height:26px; padding:5px; font-size:15px; }
QFrame#motorCard QCheckBox { min-height:32px; spacing:9px; font-size:15px; }
QFrame#motorCard QCheckBox::indicator { width:22px; height:22px; border:1px solid #8498b1; border-radius:3px; background:#111c2d; }
QFrame#motorCard QCheckBox::indicator:checked { background:#63dbc0; border-color:#a2f2df; }
QFrame#motorCard QPushButton#motorStep { min-height:34px; padding:6px 10px; font-size:17px; font-weight:600; }
QFrame#motorCard QPushButton#motorStep:enabled { background:#347964; border-color:#5bb99b; }
QFrame#motorCard QPushButton#motorStep:checked:enabled { background:#493443; border-color:#8d5a70; }
QGroupBox#commonSignals { font-size:14px; }
QGroupBox#commonSignals QCheckBox { font-size:14px; spacing:8px; }
QGroupBox#commonSignals QCheckBox::indicator { width:20px; height:20px; border:1px solid #8498b1; border-radius:3px; background:#111c2d; }
QGroupBox#commonSignals QCheckBox::indicator:checked { background:#63dbc0; border-color:#a2f2df; }
QGroupBox#commonSignals QPushButton,QGroupBox#commonSignals QComboBox,QGroupBox#commonSignals QSpinBox { min-height:28px; font-size:14px; }
QGroupBox#commonSignals QPushButton[commonAction="true"] { min-height:28px; font-size:14px; font-weight:600; }
QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox { background:#111c2d; border:1px solid #3a4b64; border-radius:4px; padding:4px; min-height:20px; }
QLineEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus,QComboBox:focus { border-color:#6dd6bf; }
QPushButton { background:#273a52; border:1px solid #405673; border-radius:5px; padding:6px 11px; min-height:20px; }
QPushButton:hover { background:#344e6b; border-color:#74a1c6; }
QPushButton:pressed { background:#435c78; }
QPushButton:disabled { color:#607186; background:#202c3c; }
QPushButton#primary { background:#347964; border-color:#5bb99b; font-weight:600; }
QPushButton#danger { background:#923d50; border-color:#c66277; color:white; font-weight:700; }
QTabWidget::pane { border:1px solid #2d3d52; top:-1px; }
QTabBar::tab { background:#182437; color:#95a9c2; padding:11px 17px; border-bottom:2px solid transparent; }
QTabBar::tab:selected { color:#ecf5ff; background:#24364d; border-bottom:2px solid #64d7b7; }
QGroupBox { border:1px solid #33445c; border-radius:6px; margin-top:12px; padding:12px 8px 7px; }
QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; color:#a9c1db; }
QProgressBar { border:0; background:#0c1320; border-radius:2px; text-align:center; }
QProgressBar::chunk { background:#5cbca7; border-radius:2px; }
QHeaderView::section { background:#24364b; border:0; padding:7px; }
QTableWidget { background:#142033; gridline-color:#2b3b51; selection-background-color:#344b66; }
QPlainTextEdit { background:#0d1420; border:1px solid #304058; font-family:Consolas; }
QScrollArea { border:0; }
QScrollBar:vertical { background:#111a29; width:12px; }
QScrollBar::handle:vertical { background:#3a4b64; min-height:30px; border-radius:4px; }
QScrollBar:horizontal { background:#111a29; height:12px; }
QScrollBar::handle:horizontal { background:#3a4b64; min-width:30px; border-radius:4px; }
QToolTip { background:#354a64; color:white; border:1px solid #647e9b; }
QStatusBar { color:#9dafc4; }
QFrame#errorNotice { background:#451e2a; border:2px solid #ff657c; border-radius:7px; }
QFrame#errorNotice QLabel { background:transparent; color:#ffe9ec; border:0; font-size:14px; }
QFrame#errorNotice QLabel#noticeTitle { color:#ff8d99; font-size:16px; font-weight:700; }
QFrame#errorNotice QPushButton { background:#6e2a3a; color:#fff1f3; border:1px solid #e98798; }
QLabel#warningText { color:#ff8d99; font-weight:600; }
"""
