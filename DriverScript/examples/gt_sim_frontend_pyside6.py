#!/usr/bin/env python3
import os
import shlex
import sys
import time
import xml.etree.ElementTree as ET

from PySide6.QtCore import QProcess, QProcessEnvironment, QSettings, QTimer
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
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


class LauncherWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("GT_Sim Frontend (PySide6)")
        self.resize(1100, 760)

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

        paths_group = QGroupBox("Paths")
        paths_form = QFormLayout(paths_group)

        self.gt_sim_path_edit = QLineEdit()
        self.gt_sim_browse_btn = QPushButton("Browse...")
        gt_sim_row = QHBoxLayout()
        gt_sim_row.addWidget(self.gt_sim_path_edit)
        gt_sim_row.addWidget(self.gt_sim_browse_btn)
        paths_form.addRow("GT_Sim.exe", gt_sim_row)

        self.python_path_edit = QLineEdit()
        self.python_browse_btn = QPushButton("Browse...")
        py_row = QHBoxLayout()
        py_row.addWidget(self.python_path_edit)
        py_row.addWidget(self.python_browse_btn)
        paths_form.addRow("Python", py_row)

        self.scenario_edit = QLineEdit()
        self.scenario_browse_btn = QPushButton("Browse...")
        scenario_row = QHBoxLayout()
        scenario_row.addWidget(self.scenario_edit)
        scenario_row.addWidget(self.scenario_browse_btn)
        paths_form.addRow("Scenario (.xosc)", scenario_row)

        self.script_edit = QLineEdit()
        self.script_browse_btn = QPushButton("Browse...")
        script_row = QHBoxLayout()
        script_row.addWidget(self.script_edit)
        script_row.addWidget(self.script_browse_btn)
        paths_form.addRow("RealDriver Python Script", script_row)

        root.addWidget(paths_group)

        options_group = QGroupBox("Run Options")
        options_grid = QGridLayout(options_group)

        self.realdriver_checkbox = QCheckBox("Use RealDriver temp scenario")
        self.realdriver_checkbox.setChecked(True)
        options_grid.addWidget(self.realdriver_checkbox, 0, 0, 1, 2)

        self.entity_name_edit = QLineEdit("Ego")
        options_grid.addWidget(QLabel("Target Entity"), 1, 0)
        options_grid.addWidget(self.entity_name_edit, 1, 1)

        self.base_port_spin = QSpinBox()
        self.base_port_spin.setRange(1, 65535)
        self.base_port_spin.setValue(53995)
        options_grid.addWidget(QLabel("BasePort"), 1, 2)
        options_grid.addWidget(self.base_port_spin, 1, 3)

        self.osi_edit = QLineEdit("127.0.0.1")
        options_grid.addWidget(QLabel("--osi"), 2, 0)
        options_grid.addWidget(self.osi_edit, 2, 1)

        self.hz_spin = QDoubleSpinBox()
        self.hz_spin.setRange(0.1, 1000.0)
        self.hz_spin.setDecimals(1)
        self.hz_spin.setValue(100.0)
        options_grid.addWidget(QLabel("--hz"), 2, 2)
        options_grid.addWidget(self.hz_spin, 2, 3)

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

        window_row = QHBoxLayout()
        window_row.addWidget(QLabel("x"))
        window_row.addWidget(self.win_x_spin)
        window_row.addWidget(QLabel("y"))
        window_row.addWidget(self.win_y_spin)
        window_row.addWidget(QLabel("w"))
        window_row.addWidget(self.win_w_spin)
        window_row.addWidget(QLabel("h"))
        window_row.addWidget(self.win_h_spin)
        options_grid.addWidget(QLabel("--window"), 3, 0)
        options_grid.addLayout(window_row, 3, 1, 1, 3)

        self.threads_checkbox = QCheckBox("Use --threads (viewer separate thread)")
        self.threads_checkbox.setChecked(True)
        options_grid.addWidget(self.threads_checkbox, 4, 0, 1, 4)

        self.script_args_edit = QLineEdit()
        options_grid.addWidget(QLabel("Python script args"), 5, 0)
        options_grid.addWidget(self.script_args_edit, 5, 1, 1, 3)

        self.gtsim_extra_args_edit = QLineEdit()
        options_grid.addWidget(QLabel("GT_Sim extra args"), 6, 0)
        options_grid.addWidget(self.gtsim_extra_args_edit, 6, 1, 1, 3)

        root.addWidget(options_group)

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

        self.gt_sim_browse_btn.clicked.connect(self._browse_gt_sim)
        self.python_browse_btn.clicked.connect(self._browse_python)
        self.scenario_browse_btn.clicked.connect(self._browse_scenario)
        self.script_browse_btn.clicked.connect(self._browse_script)

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

    def _browse_gt_sim(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Select GT_Sim.exe", self.gt_sim_path_edit.text(), "Executables (*.exe);;All Files (*)"
        )
        if path:
            self.gt_sim_path_edit.setText(path)

    def _browse_python(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Python", self.python_path_edit.text(), "Executables (*.exe);;All Files (*)"
        )
        if path:
            self.python_path_edit.setText(path)

    def _browse_scenario(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Scenario", self.scenario_edit.text(), "OpenSCENARIO (*.xosc);;All Files (*)"
        )
        if path:
            self.scenario_edit.setText(path)

    def _browse_script(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Python Script", self.script_edit.text(), "Python (*.py);;All Files (*)"
        )
        if path:
            self.script_edit.setText(path)

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
        return True

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
        base_port = str(self.base_port_spin.value())
        base_port_prop = None
        for prop in props.findall("Property"):
            if prop.get("name") == "BasePort":
                base_port_prop = prop
                break
        if base_port_prop is None:
            base_port_prop = ET.SubElement(props, "Property")
            base_port_prop.set("name", "BasePort")
        base_port_prop.set("value", base_port)

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
        out_path = os.path.join(src_dir, f"{base_name}_realdriver_temp.xosc")

        tree.write(out_path, encoding="utf-8", xml_declaration=True)
        self.last_temp_xosc = out_path
        self._append_log(f"Generated temp scenario: {out_path}")
        return out_path

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

    def start_python_process(self) -> None:
        if self.python_proc.state() != QProcess.NotRunning:
            self._append_python_log("Python is already running.")
            return
        if not self._validate_paths_for_python():
            return

        py = self.python_path_edit.text().strip()
        script = self.script_edit.text().strip()
        extra_args = _safe_split(self.script_args_edit.text())
        args = []
        if os.path.basename(py).lower().startswith("python"):
            # Force unbuffered stdio so logs are visible in real time from QProcess.
            args.append("-u")
        args += [script] + extra_args

        self.python_proc.setProgram(py)
        self.python_proc.setArguments(args)
        self.python_proc.setWorkingDirectory(os.path.dirname(script) or os.getcwd())
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
        self.scenario_edit.setText(self.settings.value("scenario_path", ""))
        self.script_edit.setText(self.settings.value("script_path", ""))
        self.script_args_edit.setText(self.settings.value("script_args", ""))
        self.gtsim_extra_args_edit.setText(self.settings.value("gtsim_extra_args", ""))

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

    def _save_settings(self) -> None:
        self.settings.setValue("gt_sim_path", self.gt_sim_path_edit.text().strip())
        self.settings.setValue("python_path", self.python_path_edit.text().strip())
        self.settings.setValue("scenario_path", self.scenario_edit.text().strip())
        self.settings.setValue("script_path", self.script_edit.text().strip())
        self.settings.setValue("script_args", self.script_args_edit.text().strip())
        self.settings.setValue("gtsim_extra_args", self.gtsim_extra_args_edit.text().strip())

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
