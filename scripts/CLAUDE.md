# Component Knowledge Graph: scripts/

Python utility scripts. Always execute via `DriverScript/.venv/Scripts/python.exe`.

## Script Categories

### Data Conversion
| Script | Role |
| :--- | :--- |
| `dat.py` | `DATFile` class — read/write esmini binary `.dat` recording files |
| `dat2csv.py` | CLI wrapper: `.dat` → `.csv` conversion |
| `osi2csv.py` | OSI binary trace → CSV conversion |

### Visualization
| Script | Role |
| :--- | :--- |
| `plot.py` | General plotting utilities |
| `plot_csv.py` | Plot from CSV data |
| `plot_dat.py` | Plot directly from `.dat` files |
| `plot_comparison.py` | Generate comparison plots for controller tests |
| `osiviewer.py` | OSI trace viewer |

### Testing & Comparison
| Script | Role |
| :--- | :--- |
| `compare_python_vs_default.py` | **Main orchestrator** — PythonDriver vs Default comparison test |
| `comparison_kpis.py` | Metric calculation (trajectory, speed, lane keeping, route) |
| `validate_realdriver_feature_results.py` | RealDriver feature validation |

### Scenario Generation
| Script | Role |
| :--- | :--- |
| `scenario_generator.py` | Generate XOSC variants (default/python controller) |

### Analysis
| Script | Role |
| :--- | :--- |
| `collect_osi_light_metrics.py` | Collect OSI traffic light metrics |
| `count_osi_messages.py` | Count OSI messages in trace |
| `render_realdriver_report.py` | Generate RealDriver HTML report |

### Tools
| Script | Role |
| :--- | :--- |
| `esmini-launcher.py` | esmini scenario launcher |
| `fix_dae_materials.py` | Fix Collada (.dae) material issues |
| `move_to_origin.py` | Move 3D model to origin |
| `xodr_lines2curves.py` | Convert OpenDRIVE lines to curves |

### Migration
| Script | Role |
| :--- | :--- |
| `migrate_realdriver_to_pythondriver.py` | Migrate RealDriver → PythonDriver |
| `run_distribution.py` | Distribution packaging runner |
| `run_schema_comply.py` | OpenSCENARIO schema compliance check |

## Origin

Scripts from `dat.py` through `xodr_lines2curves.py` originate from upstream esmini. Scripts from `collect_osi_light_metrics.py` onward are GT_esmini-specific additions.
