# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Lightweight sequence helpers for workflow extensions."""

from __future__ import annotations

from dataclasses import dataclass, field
import os
from typing import Any, Iterable


_SEQUENCES: list["SequenceHandle"] = []
_NEXT_SEQUENCE_ID = 1


@dataclass
class FrameRef:
    """Reference to one frame in a workflow sequence."""

    path: str = ""
    image: Any = None
    metadata: dict[str, Any] = field(default_factory=dict)
    registration: dict[str, Any] = field(default_factory=dict)
    included: bool = True
    index: int = 0


class SequenceHandle:
    """In-memory workflow sequence descriptor."""

    def __init__(self, sequence_id: str, name: str = "sequence", frames: Iterable[Any] | None = None):
        self.id = str(sequence_id)
        self.name = str(name or "sequence")
        self._frames: list[FrameRef] = []
        for frame in frames or []:
            self.add_frame(frame)

    def __len__(self):
        return len(self._frames)

    def __iter__(self):
        return iter(self._frames)

    def frames(self):
        return list(self._frames)

    def included_frames(self):
        return [frame for frame in self._frames if frame.included]

    def add_frame(self, frame=None, *, path="", image=None, metadata=None, registration=None, included=True):
        if isinstance(frame, FrameRef):
            frame_ref = frame
        elif isinstance(frame, dict):
            frame_ref = FrameRef(
                path=str(frame.get("path", path) or ""),
                image=frame.get("image", image),
                metadata=dict(frame.get("metadata", metadata or {}) or {}),
                registration=dict(frame.get("registration", registration or {}) or {}),
                included=bool(frame.get("included", included)),
            )
        elif frame is not None and not path and not image and isinstance(frame, (str, bytes, os.PathLike)):
            frame_ref = FrameRef(path=os.fspath(frame))
        else:
            frame_ref = FrameRef(
                path=os.fspath(path) if path else "",
                image=frame if frame is not None and image is None else image,
                metadata=dict(metadata or {}),
                registration=dict(registration or {}),
                included=bool(included),
            )

        frame_ref.index = len(self._frames)
        self._frames.append(frame_ref)
        return frame_ref

    def get_frame(self, index):
        return self._frames[int(index)]

    def set_frame_included(self, index, included):
        self.get_frame(index).included = bool(included)

    def include_frame(self, index):
        self.set_frame_included(index, True)

    def exclude_frame(self, index):
        self.set_frame_included(index, False)

    def get_frame_metadata(self, index):
        return dict(self.get_frame(index).metadata)

    def get_frame_registration(self, index):
        return dict(self.get_frame(index).registration)

    def get_frame_pixels(self, index):
        frame = self.get_frame(index)
        if frame.image is not None:
            return frame.image
        if not frame.path:
            raise FileNotFoundError("Sequence frame has no path")
        if not os.path.exists(frame.path):
            raise FileNotFoundError(frame.path)

        from . import io
        return io.load(frame.path)


def create(name="sequence", frames=None):
    global _NEXT_SEQUENCE_ID

    sequence = SequenceHandle(f"sequence-{_NEXT_SEQUENCE_ID}", name, frames)
    _NEXT_SEQUENCE_ID += 1
    _SEQUENCES.append(sequence)
    return sequence


def from_paths(paths, name=None, load_metadata=False):
    sequence = create(name or "sequence")
    for path in paths:
        metadata = {"path": os.fspath(path)} if load_metadata else {}
        sequence.add_frame(path=os.fspath(path), metadata=metadata)
    return sequence


def list_sequences():
    return list(_SEQUENCES)


__all__ = [
    "FrameRef",
    "SequenceHandle",
    "create",
    "from_paths",
    "list_sequences",
]
