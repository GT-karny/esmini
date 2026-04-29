"""Filesystem watcher for preset YAML files.

Watches each active project's ``presets/`` directory only while at least one
WebSocket client is subscribed. Events are debounced to coalesce editor
save bursts (e.g. atomic write produces multiple raw filesystem events).
"""

from __future__ import annotations

import asyncio
import hashlib
import logging
import os
import threading
import time
import uuid
from pathlib import Path
from typing import Any


def _hash_file(path: Path) -> str | None:
    """Return a sha1 hash of file contents, or None if the file is absent."""
    try:
        with path.open("rb") as f:
            h = hashlib.sha1()
            for chunk in iter(lambda: f.read(65536), b""):
                h.update(chunk)
            return h.hexdigest()
    except OSError:
        return None


def _path_key(path: Path) -> str:
    """Normalize a filesystem path for self-write echo comparison.

    Resolves symlinks and applies platform-appropriate case folding so events
    coming back from watchdog match the path we recorded in mark_self_write,
    even if Windows reports a different case from the API caller.
    """
    try:
        resolved = str(path.resolve())
    except OSError:
        resolved = str(path)
    return os.path.normcase(resolved)

from watchdog.events import FileSystemEvent, FileSystemEventHandler
from watchdog.observers import Observer

from GT_esmini.web.backend.services.project_service import _get_presets_dir

_logger = logging.getLogger(__name__)

DEBOUNCE_SECONDS = 0.5

# Window during which a watchdog event is considered an echo of our own
# write (atomic replace can produce events slightly later than the call).
SELF_WRITE_GRACE_SECONDS = 2.0


class _PresetEventHandler(FileSystemEventHandler):
    def __init__(self, manager: "PresetWatcherManager", project_id: str) -> None:
        self._manager = manager
        self._project_id = project_id

    def _handle(self, event: FileSystemEvent, change: str) -> None:
        if event.is_directory:
            return
        path = Path(str(event.src_path))
        if path.suffix.lower() != ".yaml":
            return
        # Ignore atomic-write tempfiles (see _write_presets_file).
        if path.name.startswith(".") and path.name.endswith(".tmp"):
            return
        # Skip echo events caused by our own writes.
        if self._manager._is_recent_self_write(path):
            return
        self._manager._schedule_event(self._project_id, path.stem, change)

    def on_created(self, event: FileSystemEvent) -> None:
        self._handle(event, "created")

    def on_modified(self, event: FileSystemEvent) -> None:
        self._handle(event, "modified")

    def on_deleted(self, event: FileSystemEvent) -> None:
        self._handle(event, "deleted")

    def on_moved(self, event: FileSystemEvent) -> None:
        # Treat as modified on destination, deleted on source.
        if event.is_directory:
            return
        dest = Path(str(getattr(event, "dest_path", "")))
        src = Path(str(event.src_path))
        if (
            dest.suffix.lower() == ".yaml"
            and not (dest.name.startswith(".") and dest.name.endswith(".tmp"))
            and not self._manager._is_recent_self_write(dest)
        ):
            self._manager._schedule_event(self._project_id, dest.stem, "modified")
        if (
            src.suffix.lower() == ".yaml"
            and not (src.name.startswith(".") and src.name.endswith(".tmp"))
            and not self._manager._is_recent_self_write(src)
        ):
            self._manager._schedule_event(self._project_id, src.stem, "deleted")


class PresetWatcherManager:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._observers: dict[str, Observer] = {}
        self._subscribers: dict[str, dict[str, asyncio.Queue]] = {}
        self._pending: dict[tuple[str, str], dict[str, Any]] = {}
        self._timers: dict[tuple[str, str], asyncio.TimerHandle] = {}
        self._loop: asyncio.AbstractEventLoop | None = None
        # Self-write echo suppression: maps resolved-path -> deadline (monotonic).
        self._self_writes: dict[str, float] = {}
        # Last known content hash per (project_id, scenario_stem). Populated at
        # subscribe time so Notepad-style "open but don't save" events that
        # bump mtime without changing bytes don't trigger spurious updates.
        self._content_hashes: dict[tuple[str, str], str | None] = {}

    # ---- public API ----

    async def subscribe(self, project_id: str) -> tuple[str, asyncio.Queue]:
        """Register a subscriber and ensure the project's directory is watched."""
        if self._loop is None:
            self._loop = asyncio.get_running_loop()

        queue: asyncio.Queue = asyncio.Queue()
        sub_id = uuid.uuid4().hex

        presets_dir = await _get_presets_dir(project_id)

        baseline: dict[tuple[str, str], str | None] = {}
        if presets_dir is not None and presets_dir.is_dir():
            for p in presets_dir.glob("*.yaml"):
                if p.name.startswith(".") and p.name.endswith(".tmp"):
                    continue
                baseline[(project_id, p.stem)] = _hash_file(p)

        with self._lock:
            self._subscribers.setdefault(project_id, {})[sub_id] = queue
            if project_id not in self._observers and presets_dir is not None:
                presets_dir.mkdir(parents=True, exist_ok=True)
                observer = Observer()
                observer.schedule(
                    _PresetEventHandler(self, project_id),
                    str(presets_dir),
                    recursive=False,
                )
                observer.daemon = True
                observer.start()
                self._observers[project_id] = observer
                # Seed baseline hashes only on the first subscriber so a later
                # subscriber doesn't overwrite a hash mid-edit.
                for k, h in baseline.items():
                    self._content_hashes.setdefault(k, h)
                _logger.info("Preset watcher started for project=%s dir=%s", project_id, presets_dir)
        return sub_id, queue

    async def unsubscribe(self, project_id: str, sub_id: str) -> None:
        observer_to_stop: Observer | None = None
        with self._lock:
            subs = self._subscribers.get(project_id)
            if subs is not None:
                subs.pop(sub_id, None)
                if not subs:
                    self._subscribers.pop(project_id, None)
                    observer_to_stop = self._observers.pop(project_id, None)
                    # Cancel any pending timers for this project
                    keys = [k for k in self._timers if k[0] == project_id]
                    for k in keys:
                        try:
                            self._timers[k].cancel()
                        except Exception:
                            pass
                        self._timers.pop(k, None)
                        self._pending.pop(k, None)
        if observer_to_stop is not None:
            try:
                observer_to_stop.stop()
                observer_to_stop.join(timeout=2.0)
            except Exception:
                _logger.warning("Failed to cleanly stop preset watcher", exc_info=True)
            _logger.info("Preset watcher stopped for project=%s", project_id)

    def shutdown(self) -> None:
        with self._lock:
            observers = list(self._observers.values())
            self._observers.clear()
            self._subscribers.clear()
            for handle in self._timers.values():
                try:
                    handle.cancel()
                except Exception:
                    pass
            self._timers.clear()
            self._pending.clear()
        for obs in observers:
            try:
                obs.stop()
            except Exception:
                pass
        for obs in observers:
            try:
                obs.join(timeout=2.0)
            except Exception:
                pass

    def mark_self_write(self, path: Path, content: bytes) -> None:
        """Record that the application is writing ``path`` with ``content``.

        Two effects:
            1. Filesystem-watcher events for ``path`` within the next few
               seconds are treated as our own echo and suppressed.
            2. The content baseline for any active (project_id, stem) entry
               matching this file's stem is set to the hash of ``content``,
               so a delayed echo or follow-up event still diffs as "no
               change".
        """
        key = _path_key(path)
        deadline = time.monotonic() + SELF_WRITE_GRACE_SECONDS
        new_hash = hashlib.sha1(content).hexdigest()
        with self._lock:
            self._self_writes[key] = deadline
            # Garbage-collect expired entries opportunistically.
            now = time.monotonic()
            expired = [k for k, d in self._self_writes.items() if d < now]
            for k in expired:
                self._self_writes.pop(k, None)
            # Refresh / seed baseline hash for matching stems.
            stem = path.stem
            seeded = False
            for ckey in list(self._content_hashes.keys()):
                if ckey[1] == stem:
                    self._content_hashes[ckey] = new_hash
                    seeded = True
            if not seeded:
                # No watcher subscriber yet — pre-seed for any project that
                # may subscribe later with the same file present.
                for project_id in self._observers.keys():
                    self._content_hashes[(project_id, stem)] = new_hash

    def _is_recent_self_write(self, path: Path) -> bool:
        key = _path_key(path)
        with self._lock:
            deadline = self._self_writes.get(key)
            if deadline is None:
                _logger.debug("preset watcher: no self-write match for %s", key)
                return False
            if deadline < time.monotonic():
                self._self_writes.pop(key, None)
                return False
            _logger.debug("preset watcher: suppressed self-write echo for %s", key)
            return True

    # ---- internal: called from watchdog background thread ----

    def _schedule_event(self, project_id: str, scenario_stem: str, change: str) -> None:
        loop = self._loop
        if loop is None or loop.is_closed():
            return
        loop.call_soon_threadsafe(self._coalesce, project_id, scenario_stem, change)

    def _coalesce(self, project_id: str, scenario_stem: str, change: str) -> None:
        key = (project_id, scenario_stem)
        # "deleted" wins over "modified/created" within a debounce window.
        prev = self._pending.get(key)
        if prev is None or change == "deleted":
            self._pending[key] = {"change": change}
        # Reset debounce timer.
        existing = self._timers.pop(key, None)
        if existing is not None:
            try:
                existing.cancel()
            except Exception:
                pass
        loop = self._loop
        if loop is None:
            return
        self._timers[key] = loop.call_later(
            DEBOUNCE_SECONDS, self._dispatch, project_id, scenario_stem,
        )

    def _dispatch(self, project_id: str, scenario_stem: str) -> None:
        key = (project_id, scenario_stem)
        self._timers.pop(key, None)
        payload = self._pending.pop(key, None)
        if payload is None:
            return

        # Content-hash gate: skip notifications when the file's bytes haven't
        # actually changed (Notepad-open, mtime-only touches, antivirus scans).
        # Resolve the file path lazily; we accept a small async hop because the
        # event handler doesn't have access to the project root synchronously.
        loop = self._loop
        if loop is None:
            return
        asyncio.ensure_future(
            self._dispatch_with_hash_check(project_id, scenario_stem, payload["change"]),
            loop=loop,
        )

    async def _dispatch_with_hash_check(
        self, project_id: str, scenario_stem: str, change: str,
    ) -> None:
        key = (project_id, scenario_stem)
        presets_dir = await _get_presets_dir(project_id)
        new_hash: str | None = None
        if presets_dir is not None:
            new_hash = _hash_file(presets_dir / f"{scenario_stem}.yaml")
        with self._lock:
            old_hash = self._content_hashes.get(key)
            if new_hash == old_hash:
                _logger.debug(
                    "preset watcher: content unchanged for %s/%s, skipping notify",
                    project_id, scenario_stem,
                )
                return
            self._content_hashes[key] = new_hash
            queues = list(self._subscribers.get(project_id, {}).values())
        message = {
            "type": "presets_changed",
            "project_id": project_id,
            "scenario_stem": scenario_stem,
            "change": change,
        }
        for q in queues:
            try:
                q.put_nowait(message)
            except asyncio.QueueFull:
                _logger.warning("Preset watcher queue full, dropping event for %s", project_id)


_manager: PresetWatcherManager | None = None


def get_preset_watcher_manager() -> PresetWatcherManager:
    global _manager
    if _manager is None:
        _manager = PresetWatcherManager()
    return _manager
