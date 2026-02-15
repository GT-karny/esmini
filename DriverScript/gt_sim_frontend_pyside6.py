#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import time

from PySide6.QtCore import QProcess, QProcessEnvironment, QSettings, Qt, QTimer
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QDialog,
    QDoubleSpinBox,
    QFormLayout,
    QFileDialog,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QComboBox,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QPlainTextEdit,
    QScrollArea,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

try:
    from .runtime_api import GTSimArgsRequest, GTExecutionPlanner, PythonArgsRequest
except ImportError:
    from runtime_api import GTSimArgsRequest, GTExecutionPlanner, PythonArgsRequest


def _default_gt_sim_path() -> str:
    candidates = [
        os.path.join("build", "GT_esmini", "Release", "GT_Sim.exe"),
        os.path.join("bin", "GT_Sim.exe"),
    ]
    for path in candidates:
        if os.path.exists(path):
            return os.path.abspath(path)
    return os.path.abspath(candidates[0])


def _default_scenario_folder() -> str:
    base_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.normpath(os.path.join(base_dir, "..", "resources", "xosc")),
        os.path.normpath(os.path.join(base_dir, "..", "..", "resources", "xosc")),
    ]
    for candidate in candidates:
        if os.path.isdir(candidate):
            return candidate
    return ""


def _default_script_folder() -> str:
    base_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.normpath(os.path.join(base_dir, "examples")),
        os.path.normpath(base_dir),
    ]
    for candidate in candidates:
        if os.path.isdir(candidate):
            return candidate
    return ""


class SettingsDialog(QDialog):
    """Non-modal dialog for executable paths and run options."""

    def __init__(self, launcher: "LauncherWindow") -> None:
        super().__init__(launcher)
        self.setWindowTitle("Settings")
        self.resize(760, 620)
        self._setup_ui(launcher)

    def _setup_ui(self, launcher: "LauncherWindow") -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(18, 18, 18, 18)
        layout.setSpacing(14)

        # --- Executable Paths ---
        paths_group = QGroupBox("Executable Paths")
        paths_layout = QVBoxLayout(paths_group)
        paths_layout.setSpacing(10)

        gt_sim_row = QHBoxLayout()
        gt_sim_row.addWidget(QLabel("GT_Sim.exe"))
        gt_sim_row.addWidget(launcher.gt_sim_path_edit, 1)
        gt_sim_row.addWidget(launcher.gt_sim_browse_btn)
        paths_layout.addLayout(gt_sim_row)

        py_row = QHBoxLayout()
        py_row.addWidget(QLabel("Python"))
        py_row.addWidget(launcher.python_path_edit, 1)
        py_row.addWidget(launcher.python_browse_btn)
        paths_layout.addLayout(py_row)

        layout.addWidget(paths_group)

        # --- Run Options ---
        options_group = QGroupBox("Run Options")
        options_grid = QGridLayout(options_group)
        options_grid.setHorizontalSpacing(12)
        options_grid.setVerticalSpacing(10)

        options_grid.addWidget(launcher.realdriver_checkbox, 0, 0, 1, 2)

        options_grid.addWidget(QLabel("Target Entity"), 1, 0)
        options_grid.addWidget(launcher.entity_name_edit, 1, 1)

        options_grid.addWidget(QLabel("BasePort"), 1, 2)
        options_grid.addWidget(launcher.base_port_spin, 1, 3)

        options_grid.addWidget(QLabel("--osi"), 2, 0)
        options_grid.addWidget(launcher.osi_edit, 2, 1)

        options_grid.addWidget(QLabel("--hz"), 2, 2)
        options_grid.addWidget(launcher.hz_spin, 2, 3)

        window_row = QHBoxLayout()
        window_row.addWidget(QLabel("x"))
        window_row.addWidget(launcher.win_x_spin)
        window_row.addWidget(QLabel("y"))
        window_row.addWidget(launcher.win_y_spin)
        window_row.addWidget(QLabel("w"))
        window_row.addWidget(launcher.win_w_spin)
        window_row.addWidget(QLabel("h"))
        window_row.addWidget(launcher.win_h_spin)
        options_grid.addWidget(QLabel("--window"), 3, 0)
        options_grid.addLayout(window_row, 3, 1, 1, 3)

        options_grid.addWidget(launcher.threads_checkbox, 4, 0, 1, 4)

        options_grid.addWidget(launcher.auto_xodr_checkbox, 5, 0, 1, 4)

        options_grid.addWidget(QLabel("Python extra args"), 6, 0)
        options_grid.addWidget(launcher.script_args_edit, 6, 1, 1, 3)

        options_grid.addWidget(QLabel("GT_Sim extra args"), 7, 0)
        options_grid.addWidget(launcher.gtsim_extra_args_edit, 7, 1, 1, 3)

        options_grid.addWidget(QLabel("Python --ip"), 8, 0)
        options_grid.addWidget(launcher.py_arg_ip_edit, 8, 1)
        options_grid.addWidget(QLabel("Python --port"), 8, 2)
        options_grid.addWidget(launcher.py_arg_port_spin, 8, 3)

        options_grid.addWidget(QLabel("Python --osi_port"), 9, 0)
        options_grid.addWidget(launcher.py_arg_osi_port_spin, 9, 1)
        options_grid.addWidget(QLabel("Python --id"), 9, 2)
        options_grid.addWidget(launcher.py_arg_id_spin, 9, 3)

        options_grid.addWidget(QLabel("Python --target_speed_port"), 10, 0)
        options_grid.addWidget(launcher.py_arg_target_speed_port_spin, 10, 1)

        layout.addWidget(options_group)

        # --- Close button ---
        close_btn = QPushButton("Close")
        close_btn.setProperty("role", "secondary")
        close_btn.clicked.connect(self.hide)
        btn_row = QHBoxLayout()
        btn_row.addStretch()
        btn_row.addWidget(close_btn)
        layout.addLayout(btn_row)

    def closeEvent(self, event) -> None:  # type: ignore[override]
        event.ignore()
        self.hide()


class LauncherWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("GT_Sim Frontend (PySide6)")
        self.resize(1000, 700)

        self.settings = QSettings("GT_esmini", "GT_Sim_Frontend")
        self.python_proc = QProcess(self)
        self.gtsim_proc = QProcess(self)
        self.last_temp_xosc = ""
        self.pending_start_all = False
        self.start_all_wait_ms = 800
        self.script_argspec: list[dict] = []
        self.script_arg_widgets: dict[str, QWidget] = {}
        self.script_arg_values_by_script: dict[str, dict] = {}
        self.hidden_script_arg_names = {
            "--ip",
            "--port",
            "--osi_port",
            "--id",
            "--target_speed_port",
            "--xodr_path",
            "--lib_path",
            "--gt_lib_path",
        }
        self.exec_planner = GTExecutionPlanner(on_log=self._append_log)

        self._setup_ui()
        self._connect_processes()
        self._load_settings()
        self._update_buttons()

    def _setup_ui(self) -> None:
        central = QWidget(self)
        central.setObjectName("mainPanel")
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(22, 18, 22, 20)
        root.setSpacing(14)

        # --- Settings widgets (created here, laid out in SettingsDialog) ---
        self.gt_sim_path_edit = QLineEdit()
        self.gt_sim_browse_btn = QPushButton("Browse...")

        self.python_path_edit = QLineEdit()
        self.python_browse_btn = QPushButton("Browse...")

        self.realdriver_checkbox = QCheckBox("Use RealDriver temp scenario")
        self.realdriver_checkbox.setChecked(True)

        self.entity_name_edit = QLineEdit("Ego")

        self.base_port_spin = QSpinBox()
        self.base_port_spin.setRange(1, 65535)
        self.base_port_spin.setValue(53995)

        self.osi_edit = QLineEdit("127.0.0.1")

        self.hz_spin = QDoubleSpinBox()
        self.hz_spin.setRange(0.1, 1000.0)
        self.hz_spin.setDecimals(1)
        self.hz_spin.setValue(100.0)

        self.win_x_spin = QSpinBox()
        self.win_x_spin.setRange(-1, 10000)
        self.win_x_spin.setValue(60)
        self.win_y_spin = QSpinBox()
        self.win_y_spin.setRange(-1, 10000)
        self.win_y_spin.setValue(60)
        self.win_w_spin = QSpinBox()
        self.win_w_spin.setRange(-1, 10000)
        self.win_w_spin.setValue(1280)
        self.win_h_spin = QSpinBox()
        self.win_h_spin.setRange(-1, 10000)
        self.win_h_spin.setValue(720)

        self.threads_checkbox = QCheckBox("Use --threads (viewer separate thread)")
        self.threads_checkbox.setChecked(True)

        self.script_args_edit = QLineEdit()
        self.script_args_edit.setPlaceholderText("Manual override / extra flags")

        self.py_arg_ip_edit = QLineEdit("127.0.0.1")
        self.py_arg_port_spin = QSpinBox()
        self.py_arg_port_spin.setRange(1, 65535)
        self.py_arg_port_spin.setValue(53995)
        self.py_arg_osi_port_spin = QSpinBox()
        self.py_arg_osi_port_spin.setRange(1, 65535)
        self.py_arg_osi_port_spin.setValue(48198)
        self.py_arg_id_spin = QSpinBox()
        self.py_arg_id_spin.setRange(0, 1000000)
        self.py_arg_id_spin.setValue(0)
        self.py_arg_target_speed_port_spin = QSpinBox()
        self.py_arg_target_speed_port_spin.setRange(1, 65535)
        self.py_arg_target_speed_port_spin.setValue(54995)

        self.python_argspec_group = QGroupBox("Python Script Arguments (Auto)")
        self.python_argspec_group.setObjectName("argspecGroup")
        argspec_layout = QVBoxLayout(self.python_argspec_group)
        argspec_layout.setContentsMargins(8, 8, 8, 8)
        argspec_layout.setSpacing(6)
        self.python_argspec_hint = QLabel("Select a script to load argument fields.")
        self.python_argspec_hint.setObjectName("argspecHint")
        argspec_layout.addWidget(self.python_argspec_hint)

        self.python_argspec_form_widget = QWidget()
        self.python_argspec_form = QFormLayout(self.python_argspec_form_widget)
        self.python_argspec_form.setContentsMargins(0, 0, 0, 0)
        self.python_argspec_form.setHorizontalSpacing(8)
        self.python_argspec_form.setVerticalSpacing(6)

        self.python_argspec_scroll = QScrollArea()
        self.python_argspec_scroll.setWidgetResizable(True)
        self.python_argspec_scroll.setMinimumHeight(170)
        self.python_argspec_scroll.setWidget(self.python_argspec_form_widget)
        argspec_layout.addWidget(self.python_argspec_scroll, 1)

        self.auto_xodr_checkbox = QCheckBox("Auto add --xodr_path from scenario")
        self.auto_xodr_checkbox.setChecked(True)

        self.gtsim_extra_args_edit = QLineEdit()

        # --- Header bar ---
        top_bar = QHBoxLayout()
        top_bar.setSpacing(12)
        title_col = QVBoxLayout()
        title_col.setSpacing(1)
        title = QLabel("GT_Sim Control Center")
        title.setObjectName("appTitle")
        subtitle = QLabel("Launch and monitor Python controller + GT_Sim from one console")
        subtitle.setObjectName("appSubtitle")
        title_col.addWidget(title)
        title_col.addWidget(subtitle)
        top_bar.addLayout(title_col)
        top_bar.addStretch()
        self.settings_btn = QPushButton("Settings...")
        self.settings_btn.setProperty("role", "secondary")
        self.settings_btn.setFixedWidth(120)
        top_bar.addWidget(self.settings_btn)
        root.addLayout(top_bar)

        # --- Main content row ---
        content_row = QHBoxLayout()
        content_row.setSpacing(14)

        # Left: scenario/script selection
        selection_group = QGroupBox("Inputs")
        selection_col = QVBoxLayout(selection_group)
        selection_col.setSpacing(12)

        scenario_group = QGroupBox("Scenario (.xosc)")
        scenario_col = QVBoxLayout(scenario_group)
        scenario_col.setSpacing(8)

        self.scenario_folder_edit = QLineEdit()
        self.scenario_folder_edit.setPlaceholderText("Scenario folder")
        self.scenario_folder_btn = QPushButton("Folder...")
        self.scenario_folder_btn.setProperty("role", "secondary")
        sc_folder_row = QHBoxLayout()
        sc_folder_row.addWidget(self.scenario_folder_edit, 1)
        sc_folder_row.addWidget(self.scenario_folder_btn)
        scenario_col.addLayout(sc_folder_row)

        self.scenario_list = QListWidget()
        self.scenario_list.setSpacing(0)
        self.scenario_list.setMinimumHeight(250)
        scenario_col.addWidget(self.scenario_list, 1)

        self.scenario_edit = QLineEdit()
        self.scenario_edit.setReadOnly(True)
        self.scenario_edit.hide()

        script_group = QGroupBox("RealDriver Python Script")
        script_col = QVBoxLayout(script_group)
        script_col.setSpacing(8)

        self.script_folder_edit = QLineEdit()
        self.script_folder_edit.setPlaceholderText("Script folder")
        self.script_folder_btn = QPushButton("Folder...")
        self.script_folder_btn.setProperty("role", "secondary")
        sr_folder_row = QHBoxLayout()
        sr_folder_row.addWidget(self.script_folder_edit, 1)
        sr_folder_row.addWidget(self.script_folder_btn)
        script_col.addLayout(sr_folder_row)

        self.script_list = QListWidget()
        self.script_list.setSpacing(0)
        self.script_list.setMinimumHeight(250)
        script_col.addWidget(self.script_list, 1)

        self.script_edit = QLineEdit()
        self.script_edit.setReadOnly(True)
        self.script_edit.hide()

        selection_col.addWidget(scenario_group)
        selection_col.addWidget(script_group)

        content_row.addWidget(selection_group, 3)

        # Right: control/status
        control_group = QGroupBox("Runtime")
        control_layout = QVBoxLayout(control_group)
        control_layout.setSpacing(12)

        self.start_python_btn = QPushButton("Start Python")
        self.start_python_btn.setProperty("role", "success")
        self.stop_python_btn = QPushButton("Stop Python")
        self.stop_python_btn.setProperty("role", "danger-secondary")
        self.start_gtsim_btn = QPushButton("Start GT_Sim")
        self.start_gtsim_btn.setProperty("role", "success")
        self.stop_gtsim_btn = QPushButton("Stop GT_Sim")
        self.stop_gtsim_btn.setProperty("role", "danger-secondary")
        self.start_all_btn = QPushButton("Start All (Python -> GT_Sim)")
        self.start_all_btn.setProperty("role", "primary")
        self.stop_all_btn = QPushButton("Stop All")
        self.stop_all_btn.setProperty("role", "danger")

        btn_grid = QGridLayout()
        btn_grid.setHorizontalSpacing(8)
        btn_grid.setVerticalSpacing(8)
        status_col = QVBoxLayout()
        status_col.setSpacing(12)

        py_status_row = QHBoxLayout()
        py_status_row.setSpacing(6)
        self.python_status_lamp = QLabel()
        self.python_status_lamp.setObjectName("statusLamp")
        self.python_status_lamp.setFixedSize(14, 14)
        self.python_status_lamp.setProperty("status", "stopped")
        py_status_name = QLabel("Python")
        py_status_name.setObjectName("statusName")
        py_status_row.addWidget(self.python_status_lamp)
        py_status_row.addWidget(py_status_name)
        py_status_row.addStretch()

        gt_status_row = QHBoxLayout()
        gt_status_row.setSpacing(6)
        self.gtsim_status_lamp = QLabel()
        self.gtsim_status_lamp.setObjectName("statusLamp")
        self.gtsim_status_lamp.setFixedSize(14, 14)
        self.gtsim_status_lamp.setProperty("status", "stopped")
        gt_status_name = QLabel("GT_Sim")
        gt_status_name.setObjectName("statusName")
        gt_status_row.addWidget(self.gtsim_status_lamp)
        gt_status_row.addWidget(gt_status_name)
        gt_status_row.addStretch()

        status_col.addLayout(py_status_row)
        status_col.addLayout(gt_status_row)
        status_col.addStretch()

        btn_grid.addLayout(status_col, 0, 0, 2, 1)
        btn_grid.addWidget(self.start_python_btn, 0, 1)
        btn_grid.addWidget(self.stop_python_btn, 0, 2)
        btn_grid.addWidget(self.start_gtsim_btn, 1, 1)
        btn_grid.addWidget(self.stop_gtsim_btn, 1, 2)
        btn_grid.addWidget(self.start_all_btn, 2, 0, 1, 3)
        btn_grid.addWidget(self.stop_all_btn, 3, 0, 1, 3)
        control_layout.addLayout(btn_grid)
        control_layout.addWidget(self.python_argspec_group, 1)
        control_layout.addStretch()

        content_row.addWidget(control_group, 2)
        root.addLayout(content_row, 3)

        # --- Logs ---
        logs_group = QGroupBox("Live Logs")
        logs_layout = QVBoxLayout(logs_group)
        logs_layout.setSpacing(10)

        log_row = QHBoxLayout()
        log_row.setSpacing(10)

        python_log_group = QGroupBox("Python Log")
        py_log_layout = QVBoxLayout(python_log_group)
        self.python_log_view = QPlainTextEdit()
        self.python_log_view.setObjectName("pythonLog")
        self.python_log_view.setReadOnly(True)
        self.python_log_view.setPlaceholderText("Python process log")
        py_log_layout.addWidget(self.python_log_view, 1)
        log_row.addWidget(python_log_group, 1)

        gtsim_log_group = QGroupBox("GT_Sim Log")
        gt_log_layout = QVBoxLayout(gtsim_log_group)
        self.gtsim_log_view = QPlainTextEdit()
        self.gtsim_log_view.setObjectName("gtsimLog")
        self.gtsim_log_view.setReadOnly(True)
        self.gtsim_log_view.setPlaceholderText("GT_Sim process log")
        gt_log_layout.addWidget(self.gtsim_log_view, 1)
        log_row.addWidget(gtsim_log_group, 1)

        logs_layout.addLayout(log_row, 1)

        root.addWidget(logs_group, 4)

        # --- Create SettingsDialog (widgets are reparented into it) ---
        self._settings_dialog = SettingsDialog(self)
        self._apply_theme()

        # --- Signal connections ---
        self.settings_btn.clicked.connect(self._open_settings)

        self.gt_sim_browse_btn.clicked.connect(self._browse_gt_sim)
        self.python_browse_btn.clicked.connect(self._browse_python)

        self.scenario_folder_btn.clicked.connect(self._browse_scenario_folder)
        self.script_folder_btn.clicked.connect(self._browse_script_folder)
        self.scenario_folder_edit.editingFinished.connect(self._refresh_scenario_list)
        self.script_folder_edit.editingFinished.connect(self._refresh_script_list)
        self.scenario_list.currentItemChanged.connect(self._on_scenario_selected)
        self.script_list.currentItemChanged.connect(self._on_script_selected)
        self.python_path_edit.editingFinished.connect(self._reload_script_argspec)

        self.start_python_btn.clicked.connect(self.start_python_process)
        self.stop_python_btn.clicked.connect(self.stop_python_process)
        self.start_gtsim_btn.clicked.connect(self.start_gtsim_process)
        self.stop_gtsim_btn.clicked.connect(self.stop_gtsim_process)
        self.start_all_btn.clicked.connect(self.start_all_processes)
        self.stop_all_btn.clicked.connect(self.stop_all_processes)

    def _apply_theme(self) -> None:
        app = QApplication.instance()
        if app is None:
            return
        app.setStyleSheet(
            """
            QWidget {
                color: #e7ebf3;
                font-size: 13px;
                background-color: #0f141c;
            }
            QMainWindow, QDialog {
                background-color: #0f141c;
            }
            QWidget#mainPanel {
                background-color: #0f141c;
            }
            QLabel#appTitle {
                font-size: 22px;
                font-weight: 700;
                letter-spacing: 0.5px;
                color: #f4f7ff;
            }
            QLabel#appSubtitle {
                font-size: 12px;
                color: #99a7bf;
            }
            QLabel#statusName {
                color: #c6d4ea;
                font-size: 12px;
                font-weight: 600;
            }
            QLabel#argspecHint {
                color: #9fb0ca;
                font-size: 12px;
            }
            QLabel#argspecDesc {
                color: #8ea0bc;
                font-size: 11px;
                padding-top: 1px;
            }
            QGroupBox#argspecGroup {
                margin-top: 8px;
                padding-top: 10px;
            }
            QGroupBox {
                font-size: 14px;
                font-weight: 600;
                border: 1px solid #2d3748;
                border-radius: 10px;
                margin-top: 12px;
                padding: 12px 12px 12px 12px;
                background-color: #161d27;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 6px;
                color: #c7d2e5;
            }
            QLineEdit, QListWidget, QPlainTextEdit, QSpinBox, QDoubleSpinBox {
                background-color: #1b2431;
                border: 1px solid #364155;
                border-radius: 8px;
                padding: 7px 9px;
                selection-background-color: #2e7de9;
                selection-color: #f9fbff;
            }
            QLineEdit:focus, QListWidget:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
                border: 1px solid #4a90ff;
            }
            QListWidget {
                outline: 0;
            }
            QListWidget::item {
                padding: 1px 5px;
                border-radius: 4px;
                margin: 0px 1px;
            }
            QListWidget::item:selected {
                background-color: #2f4568;
                color: #f4f7ff;
            }
            QPlainTextEdit#pythonLog, QPlainTextEdit#gtsimLog {
                font-family: Consolas, "Courier New", monospace;
                font-size: 12px;
                line-height: 1.35;
                background-color: #111924;
            }
            QPushButton {
                border: 1px solid #3a465c;
                border-radius: 8px;
                padding: 8px 12px;
                background-color: #253042;
                color: #ebf2ff;
                font-weight: 600;
            }
            QPushButton:hover {
                background-color: #2d3a50;
            }
            QPushButton:pressed {
                background-color: #212b3b;
            }
            QPushButton:disabled {
                color: #7d8ba2;
                background-color: #1f2734;
                border-color: #323c4e;
            }
            QPushButton[role="secondary"] {
                background-color: #202b3b;
                border-color: #334158;
            }
            QPushButton[role="primary"] {
                background-color: #3578e5;
                border-color: #3578e5;
                color: #ffffff;
            }
            QPushButton[role="primary"]:hover {
                background-color: #4a89ec;
            }
            QPushButton[role="success"] {
                background-color: #198754;
                border-color: #198754;
                color: #f4fffb;
            }
            QPushButton[role="success"]:hover {
                background-color: #1ea766;
            }
            QPushButton[role="danger"] {
                background-color: #c54444;
                border-color: #c54444;
                color: #ffffff;
            }
            QPushButton[role="danger"]:hover {
                background-color: #d14f4f;
            }
            QPushButton[role="danger-secondary"] {
                background-color: #4d2d35;
                border-color: #7c3f4e;
                color: #ffd7de;
            }
            QPushButton[role="success"][tone="strong"] {
                background-color: #1fa866;
                border-color: #2dd687;
                color: #f7fffb;
            }
            QPushButton[role="success"][tone="muted"] {
                background-color: #2c5d49;
                border-color: #386c57;
                color: #b6d2c7;
            }
            QPushButton[role="danger-secondary"][tone="strong"] {
                background-color: #8c3e51;
                border-color: #bb5c74;
                color: #ffe4ea;
            }
            QPushButton[role="danger-secondary"][tone="muted"] {
                background-color: #4a3940;
                border-color: #5d4a52;
                color: #c7b2b8;
            }
            QPushButton[role="primary"][tone="strong"] {
                background-color: #3f86ff;
                border-color: #69a0ff;
                color: #ffffff;
            }
            QPushButton[role="primary"][tone="muted"] {
                background-color: #3c4f71;
                border-color: #4f6388;
                color: #c1cee4;
            }
            QPushButton[role="danger"][tone="strong"] {
                background-color: #d45454;
                border-color: #e77676;
                color: #ffffff;
            }
            QPushButton[role="danger"][tone="muted"] {
                background-color: #614249;
                border-color: #755259;
                color: #ceb7bd;
            }
            QPushButton[role][tone]:disabled {
                color: #c9d2e4;
            }
            QLabel#statusLamp {
                min-width: 14px;
                max-width: 14px;
                min-height: 14px;
                max-height: 14px;
                border-radius: 7px;
                border: 1px solid #495469;
                background-color: #5a6579;
            }
            QLabel#statusLamp[status="running"] {
                background-color: #1ed47f;
                border: 1px solid #77f1bd;
            }
            QLabel#statusLamp[status="stopped"] {
                background-color: #cc4f67;
                border: 1px solid #f699ab;
            }
            QCheckBox {
                spacing: 7px;
            }
            QCheckBox::indicator {
                width: 15px;
                height: 15px;
                border-radius: 4px;
                border: 1px solid #5b6b86;
                background: #1b2431;
            }
            QCheckBox::indicator:checked {
                background: #377ce9;
                border: 1px solid #377ce9;
            }
            """
        )

    def _refresh_dynamic_style(self, widget: QWidget) -> None:
        widget.style().unpolish(widget)
        widget.style().polish(widget)
        widget.update()

    def _connect_processes(self) -> None:
        self.python_proc.readyReadStandardOutput.connect(
            lambda: self._append_proc_output(self.python_proc, "PY", False)
        )
        self.python_proc.readyReadStandardError.connect(
            lambda: self._append_proc_output(self.python_proc, "PY", True)
        )
        self.gtsim_proc.readyReadStandardOutput.connect(
            lambda: self._append_proc_output(self.gtsim_proc, "GT", False)
        )
        self.gtsim_proc.readyReadStandardError.connect(
            lambda: self._append_proc_output(self.gtsim_proc, "GT", True)
        )

        self.python_proc.stateChanged.connect(lambda _: self._update_buttons())
        self.gtsim_proc.stateChanged.connect(lambda _: self._update_buttons())
        self.python_proc.finished.connect(self._on_python_finished)
        self.gtsim_proc.finished.connect(self._on_gtsim_finished)

    def _open_settings(self) -> None:
        self._settings_dialog.show()
        self._settings_dialog.raise_()
        self._settings_dialog.activateWindow()

    def _browse_gt_sim(self) -> None:
        parent = self._settings_dialog if self._settings_dialog.isVisible() else self
        path, _ = QFileDialog.getOpenFileName(
            parent, "Select GT_Sim.exe", self.gt_sim_path_edit.text(), "Executables (*.exe);;All Files (*)"
        )
        if path:
            self.gt_sim_path_edit.setText(path)

    def _browse_python(self) -> None:
        parent = self._settings_dialog if self._settings_dialog.isVisible() else self
        path, _ = QFileDialog.getOpenFileName(
            parent, "Select Python", self.python_path_edit.text(), "Executables (*.exe);;All Files (*)"
        )
        if path:
            self.python_path_edit.setText(path)

    def _browse_scenario(self) -> None:
        start_dir = self.scenario_folder_edit.text().strip() or self.scenario_edit.text().strip()
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Scenario", start_dir, "OpenSCENARIO (*.xosc);;All Files (*)"
        )
        if path:
            self.scenario_edit.setText(path)
            folder = os.path.dirname(path)
            if folder != self.scenario_folder_edit.text().strip():
                self.scenario_folder_edit.setText(folder)
                self._refresh_scenario_list()
            else:
                self._select_current_in_list(self.scenario_list, path)

    def _browse_script(self) -> None:
        start_dir = self.script_folder_edit.text().strip() or self.script_edit.text().strip()
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Python Script", start_dir, "Python (*.py);;All Files (*)"
        )
        if path:
            self.script_edit.setText(path)
            folder = os.path.dirname(path)
            if folder != self.script_folder_edit.text().strip():
                self.script_folder_edit.setText(folder)
                self._refresh_script_list()
            else:
                self._select_current_in_list(self.script_list, path)

    def _browse_scenario_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(
            self, "Select Scenario Folder", self.scenario_folder_edit.text()
        )
        if folder:
            self.scenario_folder_edit.setText(folder)
            self._refresh_scenario_list()

    def _browse_script_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(
            self, "Select Script Folder", self.script_folder_edit.text()
        )
        if folder:
            self.script_folder_edit.setText(folder)
            self._refresh_script_list()

    def _refresh_scenario_list(self) -> None:
        self.scenario_list.clear()
        folder = self.scenario_folder_edit.text().strip()
        if not folder or not os.path.isdir(folder):
            return
        for name in sorted(os.listdir(folder)):
            if not name.lower().endswith(".xosc"):
                continue
            if name.endswith("_realdriver_temp.xosc"):
                continue
            full_path = os.path.join(folder, name)
            if os.path.isfile(full_path):
                item = QListWidgetItem(name)
                item.setData(Qt.ItemDataRole.UserRole, full_path)
                self.scenario_list.addItem(item)
        self._select_current_in_list(self.scenario_list, self.scenario_edit.text().strip())

    def _refresh_script_list(self) -> None:
        self.script_list.clear()
        folder = self.script_folder_edit.text().strip()
        if not folder or not os.path.isdir(folder):
            return
        for name in sorted(os.listdir(folder)):
            if not name.lower().endswith(".py"):
                continue
            if name == "gt_sim_frontend_pyside6.py":
                continue
            full_path = os.path.join(folder, name)
            if os.path.isfile(full_path):
                item = QListWidgetItem(name)
                item.setData(Qt.ItemDataRole.UserRole, full_path)
                self.script_list.addItem(item)
        self._select_current_in_list(self.script_list, self.script_edit.text().strip())

    def _select_current_in_list(self, list_widget: QListWidget, current_path: str) -> None:
        if not current_path:
            return
        current_norm = os.path.normpath(current_path)
        for i in range(list_widget.count()):
            item = list_widget.item(i)
            item_path = os.path.normpath(item.data(Qt.ItemDataRole.UserRole))
            if item_path == current_norm:
                list_widget.setCurrentItem(item)
                return

    def _on_scenario_selected(self, current: QListWidgetItem, _previous: QListWidgetItem) -> None:
        if current is not None:
            self.scenario_edit.setText(current.data(Qt.ItemDataRole.UserRole))

    def _on_script_selected(self, current: QListWidgetItem, previous: QListWidgetItem) -> None:
        if previous is not None:
            prev_path = previous.data(Qt.ItemDataRole.UserRole)
            if isinstance(prev_path, str) and prev_path:
                self.script_arg_values_by_script[os.path.normpath(prev_path)] = (
                    self._capture_script_arg_values()
                )
        if current is not None:
            script_path = current.data(Qt.ItemDataRole.UserRole)
            if not isinstance(script_path, str):
                script_path = ""
            self.script_edit.setText(script_path)
            self._load_script_argspec(script_path)
        else:
            self.script_edit.clear()
            self._load_script_argspec("")

    def _reload_script_argspec(self) -> None:
        self._load_script_argspec(self.script_edit.text().strip())

    def _clear_script_argspec_form(self) -> None:
        while self.python_argspec_form.rowCount() > 0:
            self.python_argspec_form.removeRow(0)
        self.script_arg_widgets = {}
        self.script_argspec = []

    def _load_script_argspec(self, script_path: str) -> None:
        self._clear_script_argspec_form()
        script_path = os.path.normpath(script_path.strip()) if script_path else ""
        if not script_path:
            self.python_argspec_hint.setText("Select a script to load argument fields.")
            return
        if not os.path.exists(script_path):
            self.python_argspec_hint.setText("Selected script path is invalid.")
            return

        py = self.python_path_edit.text().strip()
        if not py or not os.path.exists(py):
            py = sys.executable

        cmd = [py, "-u", script_path, "--dump-argspec"]
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=4,
                cwd=self._resolve_python_workdir(script_path),
                check=False,
            )
        except Exception as exc:
            self.python_argspec_hint.setText("Failed to load script args metadata.")
            self._append_python_log(f"Failed to load argspec: {exc}")
            return

        # Some scripts validate required args before dump handling.
        # Retry with auto-resolved --xodr_path when required.
        if result.returncode != 0:
            stderr_lower = (result.stderr or "").lower()
            needs_xodr = "--xodr_path" in stderr_lower and "required" in stderr_lower
            if needs_xodr:
                scenario_path = self.scenario_edit.text().strip()
                auto_xodr = self._resolve_logicfile_xodr(scenario_path)
                if auto_xodr:
                    retry_cmd = [py, "-u", script_path, "--dump-argspec", "--xodr_path", auto_xodr]
                    try:
                        result = subprocess.run(
                            retry_cmd,
                            capture_output=True,
                            text=True,
                            timeout=4,
                            cwd=self._resolve_python_workdir(script_path),
                            check=False,
                        )
                    except Exception:
                        pass

        if result.returncode != 0:
            self.python_argspec_hint.setText("Script does not provide GUI arg metadata.")
            stderr = result.stderr.strip()
            if stderr:
                self._append_python_log(f"[argspec] {stderr}")
            return

        raw = result.stdout.strip()
        if not raw:
            self.python_argspec_hint.setText("No arg metadata returned from script.")
            return

        try:
            specs = json.loads(raw)
        except Exception as exc:
            self.python_argspec_hint.setText("Invalid args metadata format from script.")
            self._append_python_log(f"Failed to parse argspec JSON: {exc}")
            return
        if not isinstance(specs, list):
            self.python_argspec_hint.setText("Invalid args metadata payload.")
            return

        self.script_argspec = specs
        restored = self.script_arg_values_by_script.get(script_path, {})
        self._build_script_argspec_form(specs, restored)
        self.python_argspec_hint.setText(
            f"Loaded {len(self.script_arg_widgets)} args from {os.path.basename(script_path)}"
        )

    def _build_script_argspec_form(self, specs: list[dict], restored: dict) -> None:
        for spec in specs:
            if not isinstance(spec, dict):
                continue
            name = str(spec.get("name", "")).strip()
            if not name.startswith("--"):
                continue
            if name in self.hidden_script_arg_names:
                continue

            label = name[2:].replace("_", " ")
            label = label[:1].upper() + label[1:]
            help_text = str(spec.get("help", "")).strip()
            description_text = str(spec.get("description", "")).strip() or help_text
            arg_type = str(spec.get("type", "str")).strip().lower()
            choices = spec.get("choices")
            default = restored.get(name, spec.get("default"))

            widget: QWidget
            if isinstance(choices, list) and choices:
                combo = QComboBox()
                for choice in choices:
                    combo.addItem(str(choice))
                if default is not None:
                    idx = combo.findText(str(default))
                    if idx >= 0:
                        combo.setCurrentIndex(idx)
                widget = combo
            elif arg_type == "int":
                spin = QSpinBox()
                spin.setRange(-1_000_000, 1_000_000)
                spin.setValue(int(default) if default is not None else 0)
                widget = spin
            elif arg_type == "float":
                spin = QDoubleSpinBox()
                spin.setRange(-1_000_000.0, 1_000_000.0)
                spin.setDecimals(4)
                spin.setSingleStep(0.1)
                spin.setValue(float(default) if default is not None else 0.0)
                widget = spin
            elif arg_type == "bool":
                check = QCheckBox()
                check.setChecked(bool(default))
                widget = check
            else:
                line = QLineEdit()
                if default is not None:
                    line.setText(str(default))
                if spec.get("ui") == "path":
                    line.setPlaceholderText("Path")
                widget = line

            if help_text:
                widget.setToolTip(help_text)
            self.script_arg_widgets[name] = widget
            container = QWidget()
            container_layout = QVBoxLayout(container)
            container_layout.setContentsMargins(0, 0, 0, 0)
            container_layout.setSpacing(1)
            container_layout.addWidget(widget)
            if description_text:
                desc = QLabel(description_text)
                desc.setObjectName("argspecDesc")
                desc.setWordWrap(True)
                container_layout.addWidget(desc)
            self.python_argspec_form.addRow(QLabel(label), container)

    def _capture_script_arg_values(self) -> dict:
        values: dict[str, object] = {}
        for name, widget in self.script_arg_widgets.items():
            if isinstance(widget, QCheckBox):
                values[name] = widget.isChecked()
            elif isinstance(widget, QSpinBox):
                values[name] = widget.value()
            elif isinstance(widget, QDoubleSpinBox):
                values[name] = widget.value()
            elif isinstance(widget, QComboBox):
                values[name] = widget.currentText()
            elif isinstance(widget, QLineEdit):
                values[name] = widget.text().strip()
        return values

    def _collect_script_argspec_args(self) -> list[str]:
        args: list[str] = []
        spec_by_name: dict[str, dict] = {}
        for spec in self.script_argspec:
            if isinstance(spec, dict):
                name = str(spec.get("name", "")).strip()
                if name:
                    spec_by_name[name] = spec

        scenario_path = self.scenario_edit.text().strip()
        auto_xodr_path = self._resolve_logicfile_xodr(scenario_path)
        script_path = self.script_edit.text().strip()
        auto_lib_path = self._resolve_default_lib_path_for_script(script_path)
        auto_gt_lib_path = self._resolve_default_gt_lib_path_for_script(script_path)
        hidden_values = {
            "--ip": self.py_arg_ip_edit.text().strip(),
            "--port": str(self.py_arg_port_spin.value()),
            "--osi_port": str(self.py_arg_osi_port_spin.value()),
            "--id": str(self.py_arg_id_spin.value()),
            "--target_speed_port": str(self.py_arg_target_speed_port_spin.value()),
            "--xodr_path": auto_xodr_path if self.auto_xodr_checkbox.isChecked() else "",
            "--lib_path": auto_lib_path,
            "--gt_lib_path": auto_gt_lib_path,
        }
        for name in self.hidden_script_arg_names:
            if name not in spec_by_name:
                continue
            value = hidden_values.get(name, "").strip()
            if value:
                args += [name, value]

        for spec in self.script_argspec:
            if not isinstance(spec, dict):
                continue
            name = str(spec.get("name", "")).strip()
            if name in self.hidden_script_arg_names:
                continue
            if name not in self.script_arg_widgets:
                continue
            arg_type = str(spec.get("type", "str")).strip().lower()
            widget = self.script_arg_widgets[name]
            default = spec.get("default")

            if isinstance(widget, QCheckBox):
                checked = widget.isChecked()
                if checked and not bool(default):
                    args.append(name)
                continue

            if isinstance(widget, QSpinBox):
                value = widget.value()
                if default is None or int(default) != value:
                    args += [name, str(value)]
                continue

            if isinstance(widget, QDoubleSpinBox):
                value = widget.value()
                if default is None or abs(float(default) - value) > 1e-9:
                    args += [name, str(value)]
                continue

            if isinstance(widget, QComboBox):
                value = widget.currentText().strip()
                if value and (default is None or str(default) != value):
                    args += [name, value]
                continue

            if isinstance(widget, QLineEdit):
                value = widget.text().strip()
                if not value:
                    continue
                if arg_type == "str" and default is not None and str(default).strip() == value:
                    continue
                args += [name, value]
        return args

    def _append_log(self, message: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.gtsim_log_view.appendPlainText(f"[{timestamp}] {message}")

    def _append_python_log(self, message: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.python_log_view.appendPlainText(f"[{timestamp}] {message}")

    def _append_gtsim_log(self, message: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.gtsim_log_view.appendPlainText(f"[{timestamp}] {message}")

    def _append_proc_output(self, proc: QProcess, tag: str, is_err: bool) -> None:
        data = proc.readAllStandardError() if is_err else proc.readAllStandardOutput()
        text = bytes(data).decode(errors="replace").rstrip()
        if text:
            for line in text.splitlines():
                prefix = f"{tag} ERR" if is_err else tag
                if tag == "PY":
                    self._append_python_log(f"[{prefix}] {line}")
                else:
                    self._append_gtsim_log(f"[{prefix}] {line}")

    def _on_python_finished(self) -> None:
        self._append_python_log("Python process finished.")
        self._update_buttons()

    def _on_gtsim_finished(self) -> None:
        self._append_gtsim_log("GT_Sim process finished.")
        self._update_buttons()

    def _update_buttons(self) -> None:
        py_running = self.python_proc.state() != QProcess.NotRunning
        gt_running = self.gtsim_proc.state() != QProcess.NotRunning

        self.start_python_btn.setEnabled(not py_running)
        self.stop_python_btn.setEnabled(py_running)
        self.start_gtsim_btn.setEnabled(not gt_running)
        self.stop_gtsim_btn.setEnabled(gt_running)
        self.start_all_btn.setEnabled(not (py_running and gt_running))
        self.stop_all_btn.setEnabled(py_running or gt_running)

        self.start_python_btn.setProperty("tone", "strong" if not py_running else "muted")
        self.stop_python_btn.setProperty("tone", "strong" if py_running else "muted")
        self.start_gtsim_btn.setProperty("tone", "strong" if not gt_running else "muted")
        self.stop_gtsim_btn.setProperty("tone", "strong" if gt_running else "muted")
        self.start_all_btn.setProperty("tone", "strong" if not (py_running and gt_running) else "muted")
        self.stop_all_btn.setProperty("tone", "strong" if (py_running or gt_running) else "muted")

        self._refresh_dynamic_style(self.start_python_btn)
        self._refresh_dynamic_style(self.stop_python_btn)
        self._refresh_dynamic_style(self.start_gtsim_btn)
        self._refresh_dynamic_style(self.stop_gtsim_btn)
        self._refresh_dynamic_style(self.start_all_btn)
        self._refresh_dynamic_style(self.stop_all_btn)

        self.python_status_lamp.setProperty("status", "running" if py_running else "stopped")
        self.gtsim_status_lamp.setProperty("status", "running" if gt_running else "stopped")
        self.python_status_lamp.setToolTip(f"Python: {'RUNNING' if py_running else 'STOPPED'}")
        self.gtsim_status_lamp.setToolTip(f"GT_Sim: {'RUNNING' if gt_running else 'STOPPED'}")
        self._refresh_dynamic_style(self.python_status_lamp)
        self._refresh_dynamic_style(self.gtsim_status_lamp)

    def _error_dialog(self, message: str) -> None:
        QMessageBox.critical(self, "Error", message)

    def _validate_paths_for_python(self) -> bool:
        py = self.python_path_edit.text().strip()
        script = self.script_edit.text().strip()
        try:
            self.exec_planner.validate_python_paths(py, script)
        except ValueError as exc:
            self._error_dialog(str(exc))
            return False
        return True

    def _validate_paths_for_gtsim(self) -> bool:
        sim = self.gt_sim_path_edit.text().strip()
        scenario = self.scenario_edit.text().strip()
        try:
            self.exec_planner.validate_gtsim_paths(sim, scenario)
        except ValueError as exc:
            msg = str(exc)
            self._error_dialog(msg)
            self._append_gtsim_log(msg)
            return False
        return True

    def _check_scenario_dependencies(self, scenario_path: str) -> list[str]:
        return self.exec_planner.check_scenario_dependencies(scenario_path)

    def _get_target_scenario(self) -> str:
        source = self.scenario_edit.text().strip()
        return self.exec_planner.get_target_scenario(
            source_path=source,
            realdriver_enabled=self.realdriver_checkbox.isChecked(),
            entity_name=self.entity_name_edit.text().strip(),
            base_port=self.base_port_spin.value(),
        )

    def _generate_temp_realdriver_scenario(self, src_path: str) -> str:
        out_path = self.exec_planner.generate_temp_realdriver_scenario(
            src_path=src_path,
            entity_name=self.entity_name_edit.text().strip(),
            base_port=self.base_port_spin.value(),
        )
        self.last_temp_xosc = self.exec_planner.last_temp_xosc
        return out_path

    def _build_gtsim_args(self) -> list[str]:
        req = GTSimArgsRequest(
            scenario_path=self.scenario_edit.text().strip(),
            realdriver_enabled=self.realdriver_checkbox.isChecked(),
            entity_name=self.entity_name_edit.text().strip(),
            base_port=self.base_port_spin.value(),
            osi=self.osi_edit.text().strip(),
            hz=self.hz_spin.value(),
            window=(
                self.win_x_spin.value(),
                self.win_y_spin.value(),
                self.win_w_spin.value(),
                self.win_h_spin.value(),
            ),
            use_threads=self.threads_checkbox.isChecked(),
            extra_args_line=self.gtsim_extra_args_edit.text(),
        )
        args = self.exec_planner.build_gtsim_args(req)
        self.last_temp_xosc = self.exec_planner.last_temp_xosc
        return args

    def _resolve_logicfile_xodr(self, scenario_path: str) -> str:
        return self.exec_planner.resolve_logicfile_xodr(scenario_path)

    def _resolve_default_lib_path_for_script(self, script_path: str) -> str:
        return self.exec_planner.resolve_default_lib_path_for_script(script_path)

    def _resolve_default_gt_lib_path_for_script(self, script_path: str) -> str:
        return self.exec_planner.resolve_default_gt_lib_path_for_script(script_path)

    def _build_python_args(self) -> list[str]:
        req = PythonArgsRequest(
            python_executable=self.python_path_edit.text().strip(),
            script_path=self.script_edit.text().strip(),
            scenario_path=self.scenario_edit.text().strip(),
            dynamic_args=self._collect_script_argspec_args(),
            extra_args_line=self.script_args_edit.text(),
            auto_xodr=self.auto_xodr_checkbox.isChecked(),
        )
        return self.exec_planner.build_python_args(req, on_log=self._append_python_log)

    def _resolve_python_workdir(self, script_path: str) -> str:
        return self.exec_planner.resolve_python_workdir(script_path)

    def start_python_process(self) -> None:
        if self.python_proc.state() != QProcess.NotRunning:
            self._append_python_log("Python is already running.")
            return
        if not self._validate_paths_for_python():
            return

        py = self.python_path_edit.text().strip()
        script = self.script_edit.text().strip()
        try:
            args = self._build_python_args()
        except Exception as exc:
            self._error_dialog(f"Failed to prepare Python args: {exc}")
            self._append_python_log(f"Failed to prepare Python args: {exc}")
            return

        self.python_proc.setProgram(py)
        self.python_proc.setArguments(args)
        self.python_proc.setWorkingDirectory(self._resolve_python_workdir(script))
        env = QProcessEnvironment.systemEnvironment()
        env.insert("PYTHONUNBUFFERED", "1")
        self.python_proc.setProcessEnvironment(env)
        self.python_proc.start()
        started = self.python_proc.waitForStarted(5000)
        if not started:
            self._error_dialog("Failed to start Python process.")
            return
        self._append_python_log(f"Started Python: {py} {' '.join(args)}")
        self._update_buttons()

    def start_gtsim_process(self) -> None:
        if self.gtsim_proc.state() != QProcess.NotRunning:
            self._append_gtsim_log("GT_Sim is already running.")
            return
        if not self._validate_paths_for_gtsim():
            return

        sim = self.gt_sim_path_edit.text().strip()
        try:
            args = self._build_gtsim_args()
        except Exception as exc:
            self._error_dialog(f"Failed to prepare scenario: {exc}")
            return

        self.gtsim_proc.setProgram(sim)
        self.gtsim_proc.setArguments(args)
        self.gtsim_proc.setWorkingDirectory(os.path.dirname(sim) or os.getcwd())
        self.gtsim_proc.start()
        started = self.gtsim_proc.waitForStarted(5000)
        if not started:
            self._error_dialog("Failed to start GT_Sim process.")
            return
        self._append_gtsim_log(f"Started GT_Sim: {sim} {' '.join(args)}")
        self._update_buttons()

    def start_all_processes(self) -> None:
        self.pending_start_all = True
        if self.python_proc.state() == QProcess.NotRunning:
            self.start_python_process()
            if self.python_proc.state() == QProcess.NotRunning:
                self.pending_start_all = False
                return

        def _start_gt_after_delay() -> None:
            if not self.pending_start_all:
                return
            if self.gtsim_proc.state() == QProcess.NotRunning:
                self.start_gtsim_process()
            self.pending_start_all = False

        QTimer.singleShot(self.start_all_wait_ms, _start_gt_after_delay)

    def _terminate_process(self, proc: QProcess, name: str) -> None:
        if proc.state() == QProcess.NotRunning:
            if name == "Python":
                self._append_python_log(f"{name} is already stopped.")
            else:
                self._append_gtsim_log(f"{name} is already stopped.")
            return
        proc.terminate()
        if not proc.waitForFinished(3000):
            if name == "Python":
                self._append_python_log(f"{name} did not exit in time. Killing process.")
            else:
                self._append_gtsim_log(f"{name} did not exit in time. Killing process.")
            proc.kill()
            proc.waitForFinished(2000)
        if name == "Python":
            self._append_python_log(f"Stopped {name}.")
        else:
            self._append_gtsim_log(f"Stopped {name}.")
        self._update_buttons()

    def stop_python_process(self) -> None:
        self.pending_start_all = False
        self._terminate_process(self.python_proc, "Python")

    def stop_gtsim_process(self) -> None:
        self.pending_start_all = False
        self._terminate_process(self.gtsim_proc, "GT_Sim")

    def stop_all_processes(self) -> None:
        self.pending_start_all = False
        self._terminate_process(self.gtsim_proc, "GT_Sim")
        self._terminate_process(self.python_proc, "Python")

    def _load_settings(self) -> None:
        self.gt_sim_path_edit.setText(self.settings.value("gt_sim_path", _default_gt_sim_path()))
        self.python_path_edit.setText(self.settings.value("python_path", sys.executable))

        self.scenario_folder_edit.setText(
            self.settings.value("scenario_folder", _default_scenario_folder())
        )
        self.script_folder_edit.setText(
            self.settings.value("script_folder", _default_script_folder())
        )
        self.scenario_edit.setText(self.settings.value("scenario_path", ""))
        self.script_edit.setText(self.settings.value("script_path", ""))

        self.script_args_edit.setText(self.settings.value("script_args", ""))
        raw_dynamic = self.settings.value("script_arg_values_json", "{}")
        try:
            parsed = json.loads(raw_dynamic) if isinstance(raw_dynamic, str) else {}
            if isinstance(parsed, dict):
                normalized: dict[str, dict] = {}
                for key, value in parsed.items():
                    if isinstance(key, str) and isinstance(value, dict):
                        normalized[os.path.normpath(key)] = value
                self.script_arg_values_by_script = normalized
        except Exception:
            self.script_arg_values_by_script = {}
        self.gtsim_extra_args_edit.setText(self.settings.value("gtsim_extra_args", ""))
        self.auto_xodr_checkbox.setChecked(self.settings.value("auto_xodr", True, type=bool))

        self.realdriver_checkbox.setChecked(self.settings.value("realdriver_on", True, type=bool))
        self.entity_name_edit.setText(self.settings.value("entity_name", "Ego"))
        self.base_port_spin.setValue(self.settings.value("base_port", 53995, type=int))
        self.osi_edit.setText(self.settings.value("osi", "127.0.0.1"))
        self.hz_spin.setValue(self.settings.value("hz", 100.0, type=float))
        self.win_x_spin.setValue(self.settings.value("win_x", 60, type=int))
        self.win_y_spin.setValue(self.settings.value("win_y", 60, type=int))
        self.win_w_spin.setValue(self.settings.value("win_w", 1280, type=int))
        self.win_h_spin.setValue(self.settings.value("win_h", 720, type=int))
        self.threads_checkbox.setChecked(self.settings.value("threads", True, type=bool))
        self.py_arg_ip_edit.setText(self.settings.value("py_arg_ip", "127.0.0.1"))
        self.py_arg_port_spin.setValue(self.settings.value("py_arg_port", 53995, type=int))
        self.py_arg_osi_port_spin.setValue(self.settings.value("py_arg_osi_port", 48198, type=int))
        self.py_arg_id_spin.setValue(self.settings.value("py_arg_id", 0, type=int))
        self.py_arg_target_speed_port_spin.setValue(
            self.settings.value("py_arg_target_speed_port", 54995, type=int)
        )

        self._refresh_scenario_list()
        self._refresh_script_list()
        self._load_script_argspec(self.script_edit.text().strip())

    def _save_settings(self) -> None:
        self.settings.setValue("gt_sim_path", self.gt_sim_path_edit.text().strip())
        self.settings.setValue("python_path", self.python_path_edit.text().strip())
        self.settings.setValue("scenario_folder", self.scenario_folder_edit.text().strip())
        self.settings.setValue("script_folder", self.script_folder_edit.text().strip())
        self.settings.setValue("scenario_path", self.scenario_edit.text().strip())
        self.settings.setValue("script_path", self.script_edit.text().strip())
        self.settings.setValue("script_args", self.script_args_edit.text().strip())
        current_script = self.script_edit.text().strip()
        if current_script:
            self.script_arg_values_by_script[os.path.normpath(current_script)] = (
                self._capture_script_arg_values()
            )
        self.settings.setValue("script_arg_values_json", json.dumps(self.script_arg_values_by_script))
        self.settings.setValue("gtsim_extra_args", self.gtsim_extra_args_edit.text().strip())
        self.settings.setValue("auto_xodr", self.auto_xodr_checkbox.isChecked())

        self.settings.setValue("realdriver_on", self.realdriver_checkbox.isChecked())
        self.settings.setValue("entity_name", self.entity_name_edit.text().strip())
        self.settings.setValue("base_port", self.base_port_spin.value())
        self.settings.setValue("osi", self.osi_edit.text().strip())
        self.settings.setValue("hz", self.hz_spin.value())
        self.settings.setValue("win_x", self.win_x_spin.value())
        self.settings.setValue("win_y", self.win_y_spin.value())
        self.settings.setValue("win_w", self.win_w_spin.value())
        self.settings.setValue("win_h", self.win_h_spin.value())
        self.settings.setValue("threads", self.threads_checkbox.isChecked())
        self.settings.setValue("py_arg_ip", self.py_arg_ip_edit.text().strip())
        self.settings.setValue("py_arg_port", self.py_arg_port_spin.value())
        self.settings.setValue("py_arg_osi_port", self.py_arg_osi_port_spin.value())
        self.settings.setValue("py_arg_id", self.py_arg_id_spin.value())
        self.settings.setValue("py_arg_target_speed_port", self.py_arg_target_speed_port_spin.value())

    def closeEvent(self, event) -> None:  # type: ignore[override]
        self.pending_start_all = False
        self._save_settings()
        self.stop_all_processes()
        super().closeEvent(event)


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationDisplayName("GT_Sim Frontend")
    win = LauncherWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
