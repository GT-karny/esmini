#!/usr/bin/env python3
"""
Setup script for the RealDriver package.

Installation:
    pip install -e .
"""

from setuptools import setup, find_packages

setup(
    name="realdriver",
    version="1.0.0",
    description="UDP client and utilities for controlling esmini RealDriverController",
    author="GT_esmini Team",
    packages=find_packages(),
    python_requires=">=3.8",
    install_requires=[
        "protobuf>=6.30.2",
    ],
    extras_require={
        "examples": [
            "tk",  # For gui_controller.py (usually included with Python)
        ],
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
    ],
)
