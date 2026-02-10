#!/usr/bin/env python3
import os
import shlex
import sys
import time
import xml.etree.ElementTree as ET

from PySide6.QtCore import QProcess, QProcessEnvironment, QSettings, Qt, QTimer
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QDialog,
    QDoubleSpinBox,
    QFileDialog,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QPlainTextEdit,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)


def _default_gt_sim_path() -> str:
    candidates = [
        os.path.join("build", "GT_esmini", "Release", "GT_Sim.exe"),
        os.path.join("bin", "GT_Sim.exe"),
    ]
    for path in candidates:
        if os.path.exists(path):
            return os.path.abspath(path)
    return os.path.abspath(candidates[0])


def _safe_split(arg_line: str) -> list[str]:
    if not arg_line.strip():
        return []
    return shlex.split(arg_line, posix=False)


def _default_scenario_folder() -> str:
    candidate = os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "resources", "xosc")
    )
    return candidate if os.path.isdir(candidate) else ""


def _default_script_folder() -> str:
    candidate = os.path.normpath(os.path.dirname(os.path.abspath(__file__)))
    return candidate if os.path.isdir(candidate) else ""


def _has_option(args: list[str], opt: str) -> bool:
    return opt in args


def _strip_option(args: list[str], opt: str) -> list[str]:
    out: list[str] = []
    i = 0
    while i < len(args):
        if args[i] == opt:
            i += 1
            if i < len(args) and not args[i].startswith("--"):
                i += 1
            continue
        out.append(args[i])
        i += 1
    return out


class SettingsDialog(QDialog):
    """Non-modal dialog for executable paths and run options."""

    def __init__(self, launcher: "LauncherWindow") -> None:
        super().__init__(launcher)
        self.setWindowTitle("Settings")
        self.resize(620, 480)
        self._setup_ui(launcher)

    def _setup_ui(self, launcher: "LauncherWindow") -> None:
        layout = QVBoxLayout(self)

        # --- Executable Paths ---
        paths_group = QGroupBox("Executable Paths")
        paths_layout = QVBoxLayout(paths_group)

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

        options_grid.addWidget(QLabel("Python script args"), 6, 0)
        options_grid.addWidget(launcher.script_args_edit, 6, 1, 1, 3)

        options_grid.addWidget(QLabel("GT_Sim extra args"), 7, 0)
        options_grid.addWidget(launcher.gtsim_extra_args_edit, 7, 1, 1, 3)

        layout.addWidget(options_group)

        # --- Close button ---
        close_btn = QPushButton("Close")
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

        self._setup_ui()
        self._connect_processes()
        self._load_settings()
        self._update_buttons()

    def _setup_ui(self) -> None:
        central = QWidget(self)
        self.setCentralWidget(central)
        root = QVBoxLayout(central)

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
        self.auto_xodr_checkbox = QCheckBox("Auto add --xodr_path from scenario")
        self.auto_xodr_checkbox.setChecked(True)

        self.gtsim_extra_args_edit = QLineEdit()

        # --- Top bar with Settings button ---
        top_bar = QHBoxLayout()
        top_bar.addStretch()
        self.settings_btn = QPushButton("Settings...")
        self.settings_btn.setFixedWidth(100)
        top_bar.addWidget(self.settings_btn)
        root.addLayout(top_bar)

        # --- Scenario & Script file selection (side-by-side) ---
        file_sel_layout = QHBoxLayout()

        # Scenario column
        scenario_group = QGroupBox("Scenario (.xosc)")
        scenario_col = QVBoxLayout(scenario_group)

        self.scenario_folder_edit = QLineEdit()
        self.scenario_folder_btn = QPushButton("Folder...")
        sc_folder_row = QHBoxLayout()
        sc_folder_row.addWidget(self.scenario_folder_edit, 1)
        sc_folder_row.addWidget(self.scenario_folder_btn)
        scenario_col.addLayout(sc_folder_row)

        self.scenario_list = QListWidget()
        self.scenario_list.setMinimumHeight(100)
        scenario_col.addWidget(self.scenario_list, 1)

        self.scenario_edit = QLineEdit()
        self.scenario_edit.setReadOnly(True)
        self.scenario_edit.setPlaceholderText("Select from list above, or Browse...")
        self.scenario_browse_btn = QPushButton("Browse...")
        sc_sel_row = QHBoxLayout()
        sc_sel_row.addWidget(self.scenario_edit, 1)
        sc_sel_row.addWidget(self.scenario_browse_btn)
        scenario_col.addLayout(sc_sel_row)

        file_sel_layout.addWidget(scenario_group)

        # Script column
        script_group = QGroupBox("RealDriver Python Script")
        script_col = QVBoxLayout(script_group)

        self.script_folder_edit = QLineEdit()
        self.script_folder_btn = QPushButton("Folder...")
        sr_folder_row = QHBoxLayout()
        sr_folder_row.addWidget(self.script_folder_edit, 1)
        sr_folder_row.addWidget(self.script_folder_btn)
        script_col.addLayout(sr_folder_row)

        self.script_list = QListWidget()
        self.script_list.setMinimumHeight(100)
        script_col.addWidget(self.script_list, 1)

        self.script_edit = QLineEdit()
        self.script_edit.setReadOnly(True)
        self.script_edit.setPlaceholderText("Select from list above, or Browse...")
        self.script_browse_btn = QPushButton("Browse...")
        sr_sel_row = QHBoxLayout()
        sr_sel_row.addWidget(self.script_edit, 1)
        sr_sel_row.addWidget(self.script_browse_btn)
        script_col.addLayout(sr_sel_row)

        file_sel_layout.addWidget(script_group)

        root.addLayout(file_sel_layout, 1)

        # --- Process Control ---
        buttons_group = QGroupBox("Process Control")
        buttons_layout = QGridLayout(buttons_group)

        self.start_python_btn = QPushButton("Start Python")
        self.stop_python_btn = QPushButton("Stop Python")
        self.start_gtsim_btn = QPushButton("Start GT_Sim")
        self.stop_gtsim_btn = QPushButton("Stop GT_Sim")
        self.start_all_btn = QPushButton("Start All (Python -> GT_Sim)")
        self.stop_all_btn = QPushButton("Stop All")

        buttons_layout.addWidget(self.start_python_btn, 0, 0)
        buttons_layout.addWidget(self.stop_python_btn, 0, 1)
        buttons_layout.addWidget(self.start_gtsim_btn, 1, 0)
        buttons_layout.addWidget(self.stop_gtsim_btn, 1, 1)
        buttons_layout.addWidget(self.start_all_btn, 2, 0)
        buttons_layout.addWidget(self.stop_all_btn, 2, 1)

        self.python_status = QLabel("Python: stopped")
        self.gtsim_status = QLabel("GT_Sim: stopped")
        buttons_layout.addWidget(self.python_status, 3, 0)
        buttons_layout.addWidget(self.gtsim_status, 3, 1)

        root.addWidget(buttons_group)

        # --- Logs ---
        logs_group = QGroupBox("Logs")
        logs_layout = QVBoxLayout(logs_group)

        self.python_log_view = QPlainTextEdit()
        self.python_log_view.setReadOnly(True)
        self.python_log_view.setPlaceholderText("Python process log")
        logs_layout.addWidget(QLabel("Python"))
        logs_layout.addWidget(self.python_log_view, 1)

        self.gtsim_log_view = QPlainTextEdit()
        self.gtsim_log_view.setReadOnly(True)
        self.gtsim_log_view.setPlaceholderText("GT_Sim process log")
        logs_layout.addWidget(QLabel("GT_Sim"))
        logs_layout.addWidget(self.gtsim_log_view, 1)

        root.addWidget(logs_group, 1)

        # --- Create SettingsDialog (widgets are reparented into it) ---
        self._settings_dialog = SettingsDialog(self)

        # --- Signal connections ---
        self.settings_btn.clicked.connect(self._open_settings)

        self.gt_sim_browse_btn.clicked.connect(self._browse_gt_sim)
        self.python_browse_btn.clicked.connect(self._browse_python)
        self.scenario_browse_btn.clicked.connect(self._browse_scenario)
        self.script_browse_btn.clicked.connect(self._browse_script)

        self.scenario_folder_btn.clicked.connect(self._browse_scenario_folder)
        self.script_folder_btn.clicked.connect(self._browse_script_folder)
        self.scenario_folder_edit.editingFinished.connect(self._refresh_scenario_list)
        self.script_folder_edit.editingFinished.connect(self._refresh_script_list)
        self.scenario_list.currentItemChanged.connect(self._on_scenario_selected)
        self.script_list.currentItemChanged.connect(self._on_script_selected)

        self.start_python_btn.clicked.connect(self.start_python_process)
        self.stop_python_btn.clicked.connect(self.stop_python_process)
        self.start_gtsim_btn.clicked.connect(self.start_gtsim_process)
        self.stop_gtsim_btn.clicked.connect(self.stop_gtsim_process)
        self.start_all_btn.clicked.connect(self.start_all_processes)
        self.stop_all_btn.clicked.connect(self.stop_all_processes)

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

    def _on_script_selected(self, current: QListWidgetItem, _previous: QListWidgetItem) -> None:
        if current is not None:
            self.script_edit.setText(current.data(Qt.ItemDataRole.UserRole))

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

        self.python_status.setText(f"Python: {'running' if py_running else 'stopped'}")
        self.gtsim_status.setText(f"GT_Sim: {'running' if gt_running else 'stopped'}")

    def _error_dialog(self, message: str) -> None:
        QMessageBox.critical(self, "Error", message)

    def _validate_paths_for_python(self) -> bool:
        py = self.python_path_edit.text().strip()
        script = self.script_edit.text().strip()
        if not py or not os.path.exists(py):
            self._error_dialog("Python path is invalid.")
            return False
        if not script or not os.path.exists(script):
            self._error_dialog("RealDriver Python script path is invalid.")
            return False
        return True

    def _validate_paths_for_gtsim(self) -> bool:
        sim = self.gt_sim_path_edit.text().strip()
        scenario = self.scenario_edit.text().strip()
        if not sim or not os.path.exists(sim):
            self._error_dialog("GT_Sim.exe path is invalid.")
            return False
        if not scenario or not os.path.exists(scenario):
            self._error_dialog("Scenario path is invalid.")
            return False
        dep_errors = self._check_scenario_dependencies(scenario)
        if dep_errors:
            msg = "Scenario dependency check failed:\n- " + "\n- ".join(dep_errors[:8])
            self._error_dialog(msg)
            self._append_gtsim_log(msg)
            return False
        return True

    def _check_scenario_dependencies(self, scenario_path: str) -> list[str]:
        errors: list[str] = []
        try:
            tree = ET.parse(scenario_path)
            root = tree.getroot()
            scenario_dir = os.path.dirname(os.path.abspath(scenario_path))

            catalog_dirs: list[str] = []
            cat_locations = root.find("CatalogLocations")
            if cat_locations is not None:
                for cat in list(cat_locations):
                    directory = cat.find("Directory")
                    if directory is None:
                        continue
                    path = (directory.get("path") or "").strip()
                    if not path:
                        continue
                    abs_dir = path if os.path.isabs(path) else os.path.normpath(os.path.join(scenario_dir, path))
                    catalog_dirs.append(abs_dir)

            catalog_refs = root.findall(".//CatalogReference")
            if catalog_refs and not catalog_dirs:
                errors.append("CatalogReference exists but CatalogLocations/Directory is missing.")
                return errors

            for ref in catalog_refs:
                catalog_name = (ref.get("catalogName") or "").strip()
                if not catalog_name:
                    continue
                candidates = [catalog_name]
                if not catalog_name.lower().endswith(".xosc"):
                    candidates.append(f"{catalog_name}.xosc")
                found = False
                for base in catalog_dirs:
                    for name in candidates:
                        if os.path.exists(os.path.join(base, name)):
                            found = True
                            break
                    if found:
                        break
                if not found:
                    if catalog_dirs:
                        errors.append(
                            f"Catalog '{catalog_name}' not found in: {', '.join(catalog_dirs)}"
                        )
                    else:
                        errors.append(f"Catalog '{catalog_name}' not found.")
        except Exception as exc:
            errors.append(f"Failed to parse scenario: {exc}")
        return errors

    def _get_target_scenario(self) -> str:
        source = self.scenario_edit.text().strip()
        if not self.realdriver_checkbox.isChecked():
            return source
        return self._generate_temp_realdriver_scenario(source)

    def _generate_temp_realdriver_scenario(self, src_path: str) -> str:
        tree = ET.parse(src_path)
        root = tree.getroot()

        entities = root.find("Entities")
        if entities is None:
            raise RuntimeError("Entities node is missing in scenario.")

        target_name = self.entity_name_edit.text().strip() or "Ego"
        target_obj = None
        for obj in entities.findall("ScenarioObject"):
            if obj.get("name") == target_name:
                target_obj = obj
                break

        if target_obj is None:
            for obj in entities.findall("ScenarioObject"):
                if obj.get("name") == "Ego":
                    target_obj = obj
                    target_name = "Ego"
                    break

        if target_obj is None:
            target_obj = entities.find("ScenarioObject")
            if target_obj is None:
                raise RuntimeError("No ScenarioObject found in Entities.")
            target_name = target_obj.get("name", "Ego")

        obj_ctrl = target_obj.find("ObjectController")
        if obj_ctrl is None:
            obj_ctrl = ET.SubElement(target_obj, "ObjectController")
        controller = obj_ctrl.find("Controller")
        if controller is None:
            controller = ET.SubElement(obj_ctrl, "Controller")
        controller.set("name", "RealDriverController")

        props = controller.find("Properties")
        if props is None:
            props = ET.SubElement(controller, "Properties")

        def _set_prop(name: str, value: str) -> None:
            target = None
            for prop in props.findall("Property"):
                if prop.get("name") == name:
                    target = prop
                    break
            if target is None:
                target = ET.SubElement(props, "Property")
                target.set("name", name)
            target.set("value", value)

        base_port_i = self.base_port_spin.value()
        _set_prop("esminiController", "RealDriverController")
        _set_prop("BasePort", str(base_port_i))
        _set_prop("ClientPort", str(base_port_i + 1000))
        _set_prop("SendWaypoints", "true")
        _set_prop("WaypointPort", str(base_port_i + 1001))

        storyboard = root.find("Storyboard")
        if storyboard is None:
            raise RuntimeError("Storyboard node is missing in scenario.")
        init = storyboard.find("Init")
        if init is None:
            init = ET.SubElement(storyboard, "Init")
        actions = init.find("Actions")
        if actions is None:
            actions = ET.SubElement(init, "Actions")

        target_private = None
        for private in actions.findall("Private"):
            if private.get("entityRef") == target_name:
                target_private = private
                break
        if target_private is None:
            target_private = ET.SubElement(actions, "Private")
            target_private.set("entityRef", target_name)

        activate_exists = False
        for private_action in target_private.findall("PrivateAction"):
            activate = private_action.find("ActivateControllerAction")
            if activate is not None:
                activate.set("longitudinal", "true")
                activate.set("lateral", "true")
                activate_exists = True
                break
        if not activate_exists:
            private_action = ET.SubElement(target_private, "PrivateAction")
            activate = ET.SubElement(private_action, "ActivateControllerAction")
            activate.set("longitudinal", "true")
            activate.set("lateral", "true")

        src_dir = os.path.dirname(os.path.abspath(src_path))
        base_name = os.path.splitext(os.path.basename(src_path))[0]
        temp_dir = os.path.join(src_dir, ".gt_sim_temp")
        os.makedirs(temp_dir, exist_ok=True)
        self._absolutize_scenario_paths(root, src_dir)
        out_path = os.path.join(temp_dir, f"{base_name}_realdriver_temp.xosc")

        tree.write(out_path, encoding="utf-8", xml_declaration=True)
        self.last_temp_xosc = out_path
        self._append_log(f"Generated temp scenario: {out_path}")
        return out_path

    def _absolutize_scenario_paths(self, root: ET.Element, base_dir: str) -> None:
        logic = root.find("RoadNetwork/LogicFile")
        if logic is not None:
            fp = logic.get("filepath", "")
            if fp and not os.path.isabs(fp):
                logic.set("filepath", os.path.normpath(os.path.join(base_dir, fp)))

        scene = root.find("RoadNetwork/SceneGraphFile")
        if scene is not None:
            fp = scene.get("filepath", "")
            if fp and not os.path.isabs(fp):
                scene.set("filepath", os.path.normpath(os.path.join(base_dir, fp)))

        cat_locs = root.find("CatalogLocations")
        if cat_locs is not None:
            for cat in list(cat_locs):
                directory = cat.find("Directory")
                if directory is None:
                    continue
                path = directory.get("path", "")
                if path and not os.path.isabs(path):
                    directory.set("path", os.path.normpath(os.path.join(base_dir, path)))

    def _build_gtsim_args(self) -> list[str]:
        scenario_path = self._get_target_scenario()
        args = ["--osc", scenario_path]

        osi_value = self.osi_edit.text().strip()
        if osi_value:
            args += ["--osi", osi_value]

        args += ["--hz", f"{self.hz_spin.value():.1f}"]

        args += [
            "--window",
            str(self.win_x_spin.value()),
            str(self.win_y_spin.value()),
            str(self.win_w_spin.value()),
            str(self.win_h_spin.value()),
        ]

        if self.threads_checkbox.isChecked():
            args.append("--threads")

        args += _safe_split(self.gtsim_extra_args_edit.text())
        return args

    def _resolve_logicfile_xodr(self, scenario_path: str) -> str:
        if not scenario_path or not os.path.exists(scenario_path):
            return ""
        try:
            tree = ET.parse(scenario_path)
            root = tree.getroot()
            logic = root.find("RoadNetwork/LogicFile")
            if logic is None:
                return ""
            logic_fp = logic.get("filepath", "").strip()
            if not logic_fp:
                return ""
            if os.path.isabs(logic_fp):
                return logic_fp if os.path.exists(logic_fp) else ""
            abs_fp = os.path.normpath(os.path.join(os.path.dirname(scenario_path), logic_fp))
            return abs_fp if os.path.exists(abs_fp) else ""
        except Exception:
            return ""

    def _build_python_args(self) -> list[str]:
        script = self.script_edit.text().strip()
        extra_args = _safe_split(self.script_args_edit.text())
        args = []

        py = self.python_path_edit.text().strip()
        if os.path.basename(py).lower().startswith("python"):
            # Force unbuffered stdio so logs are visible in real time from QProcess.
            args.append("-u")

        supports_xodr = False
        supports_mode = False
        try:
            with open(script, "r", encoding="utf-8", errors="ignore") as f:
                src = f.read()
            supports_xodr = "--xodr_path" in src
            supports_mode = "--mode" in src
        except Exception:
            pass

        scenario_path = self.scenario_edit.text().strip()
        xodr_path = self._resolve_logicfile_xodr(scenario_path)

        if self.auto_xodr_checkbox.isChecked() and supports_xodr and xodr_path and not _has_option(extra_args, "--xodr_path"):
            extra_args += ["--xodr_path", xodr_path]
            self._append_python_log(f"Auto-added --xodr_path from scenario LogicFile: {xodr_path}")
        elif self.auto_xodr_checkbox.isChecked() and supports_xodr and not _has_option(extra_args, "--xodr_path"):
            raise RuntimeError(
                "Failed to resolve --xodr_path from selected Scenario (.xosc). "
                "Select a valid scenario with <RoadNetwork><LogicFile ...>, or set --xodr_path manually."
            )

        if not supports_mode and _has_option(extra_args, "--mode"):
            extra_args = _strip_option(extra_args, "--mode")
            self._append_python_log(
                "Removed --mode argument because selected script does not define --mode."
            )

        script_basename = os.path.basename(script).lower()
        if script_basename == "scenario_drive_example.py":
            if not _has_option(extra_args, "--mode"):
                self._append_python_log(
                    "Hint: scenario_drive_example.py default mode is 'waypoints'. "
                    "Use '--mode udp' if you want route/waypoints from ControllerRealDriver."
                )

        args += [script] + extra_args
        return args

    def _resolve_python_workdir(self, script_path: str) -> str:
        script_dir = os.path.dirname(script_path) or os.getcwd()

        # Prefer DriverScript root when running examples that use ./bin relative paths.
        # e.g. lkas_example.py default --lib_path is ./bin/esminiRMLib.dll
        parent_dir = os.path.dirname(script_dir)
        if os.path.exists(os.path.join(parent_dir, "bin", "esminiRMLib.dll")):
            return parent_dir

        return script_dir

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

        self._refresh_scenario_list()
        self._refresh_script_list()

    def _save_settings(self) -> None:
        self.settings.setValue("gt_sim_path", self.gt_sim_path_edit.text().strip())
        self.settings.setValue("python_path", self.python_path_edit.text().strip())
        self.settings.setValue("scenario_folder", self.scenario_folder_edit.text().strip())
        self.settings.setValue("script_folder", self.script_folder_edit.text().strip())
        self.settings.setValue("scenario_path", self.scenario_edit.text().strip())
        self.settings.setValue("script_path", self.script_edit.text().strip())
        self.settings.setValue("script_args", self.script_args_edit.text().strip())
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
