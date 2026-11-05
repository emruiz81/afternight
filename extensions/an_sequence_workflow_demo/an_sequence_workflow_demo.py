# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Workflow launch-mode reference extension for AfterNight."""

from __future__ import annotations

import os

import numpy as np

import afternight
from afternight import calibration
from afternight import registration
from afternight import sequences
from afternight import stacking
from afternight import ui


class SequenceWorkflowDemoExtension(ui.WorkflowProcess):
    def get_params(self):
        return [
            ui.process_window_meta(panel_variant="workflow"),
            {
                "id": "workflow_overview",
                "type": "info",
                "text": (
                    "Creates synthetic calibration and light frames, runs the "
                    "native workflow SDK calibration, registration, and stacking domains, "
                    "and writes a report through WorkflowContext."
                ),
            },
            {
                "id": "synthetic_frames",
                "type": "int",
                "label": "Synthetic Frames",
                "default": 3,
                "min": 1,
                "max": 12,
            },
            {
                "id": "output_name",
                "type": "string",
                "label": "Report File",
                "default": "sequence_workflow_report.txt",
            },
        ]

    def execute_workflow(self, workflow, params, progress):
        session = afternight.current_session()
        working_dir = workflow.get_working_dir()
        temp_dir = workflow.temp_dir()
        workflow.set_working_dir(working_dir)

        frame_count = int(params.get("synthetic_frames", 3))
        output_name = str(params.get("output_name", "sequence_workflow_report.txt")).strip()
        if not output_name:
            output_name = "sequence_workflow_report.txt"

        progress.set_text("Preparing synthetic sequence")
        progress.set_value(0.05)
        sequence = sequences.create("Demo synthetic preprocessing frames")
        width = 128
        height = 128
        y_grid, x_grid = np.mgrid[0:height, 0:width].astype(np.float32)
        base_signal = 0.035 + (x_grid / (width - 1)) * 0.025 + (y_grid / (height - 1)) * 0.018
        flat_pattern = 1.0 + (x_grid / (width - 1)) * 0.04
        star_catalog = [
            (20.0, 24.0, 0.58, 3.0),
            (58.0, 18.0, 0.46, 3.4),
            (104.0, 26.0, 0.52, 3.2),
            (34.0, 58.0, 0.62, 3.6),
            (80.0, 52.0, 0.49, 3.2),
            (112.0, 72.0, 0.44, 3.5),
            (24.0, 98.0, 0.47, 3.1),
            (66.0, 108.0, 0.60, 3.7),
            (102.0, 104.0, 0.54, 3.3),
        ]

        def star_field(dx, dy):
            signal = np.zeros((height, width), dtype=np.float32)
            for cx, cy, amplitude, fwhm in star_catalog:
                sigma = fwhm / 2.35482004503
                signal += amplitude * np.exp(
                    -(
                        ((x_grid - (cx + dx)) ** 2)
                        + ((y_grid - (cy + dy)) ** 2)
                    )
                    / (2.0 * sigma * sigma)
                )
            return signal

        def add_frame(role, index, data, metadata, registration_data=None):
            frame_path = os.path.join(temp_dir, f"{role.lower()}_{index:03d}.fit")
            image = afternight.core.from_numpy(np.asarray(data, dtype=np.float32), metadata)
            afternight.io.save(image, frame_path)
            return sequence.add_frame(
                path=frame_path,
                metadata=metadata,
                registration=registration_data or {},
            )

        for index in range(2):
            if progress.is_cancelled():
                raise afternight.WorkflowError("Workflow cancelled")
            bias = np.full((height, width), 0.01 + index * 0.001, dtype=np.float32)
            add_frame(
                "bias",
                index + 1,
                bias,
                {
                    "IMAGETYP": "BIAS",
                    "FILTER": "L",
                    "EXPTIME": 0.0,
                    "SESSION": session.session_id,
                },
            )

        for index in range(2):
            if progress.is_cancelled():
                raise afternight.WorkflowError("Workflow cancelled")
            dark = np.full((height, width), 0.025 + index * 0.001, dtype=np.float32)
            add_frame(
                "dark",
                index + 1,
                dark,
                {
                    "IMAGETYP": "DARK",
                    "FILTER": "L",
                    "EXPTIME": 60.0,
                    "SESSION": session.session_id,
                },
            )

        for index in range(2):
            if progress.is_cancelled():
                raise afternight.WorkflowError("Workflow cancelled")
            flat = flat_pattern + 0.002 * index
            add_frame(
                "flat",
                index + 1,
                flat,
                {
                    "IMAGETYP": "FLAT",
                    "FILTER": "L",
                    "EXPTIME": 2.0,
                    "SESSION": session.session_id,
                },
            )

        for index in range(frame_count):
            if progress.is_cancelled():
                raise afternight.WorkflowError("Workflow cancelled")
            dx = [0.0, 2.0, -1.5, 1.2][index % 4]
            dy = [0.0, -1.25, 1.4, 0.9][index % 4]
            light_signal = base_signal + star_field(dx, dy) + index * 0.006
            raw_light = ((light_signal * flat_pattern) + 0.035).astype(np.float32)
            add_frame(
                "light",
                index + 1,
                raw_light,
                metadata={
                    "IMAGETYP": "LIGHT",
                    "FILTER": "L",
                    "SESSION": session.session_id,
                    "EXPTIME": 60.0,
                    "OBJECT": "AN_SEQUENCE_LRGB",
                },
                registration_data={
                    "dx": dx,
                    "dy": dy,
                    "rotation_degrees": 0.0,
                },
            )

        calibration_stage = progress.set_stage("Calibrating", weight=1.0)
        calibration_stage.set_text("Creating native master calibration frames")
        masters = calibration.create_masters(
            sequence,
            workflow.output_path("masters"),
            filter_name="L",
        )
        calibrated_sequence = calibration.calibrate_lights(
            sequence,
            masters,
            workflow.output_path("calibrated"),
            filter_name="L",
        )
        calibration_stage.set_value(1.0)
        if calibration_stage.is_cancelled():
            raise afternight.WorkflowError("Workflow cancelled")

        registration_stage = progress.set_stage("Registering", weight=1.0)
        registration_stage.set_text("Detecting stars and aligning lights")
        registered_result = registration.register(
            calibrated_sequence,
            workflow.output_path("registered"),
            manifest_name="L.sequence.json",
            star_params={"radius": 8, "sigma": 1.5, "profile": "gaussian"},
            min_stars=6,
            max_stars=64,
            transform_type="similarity",
        )
        registration_stage.set_value(1.0)
        if registration_stage.is_cancelled():
            raise afternight.WorkflowError("Workflow cancelled")

        stacking_stage = progress.set_stage("Stacking", weight=1.0)
        stacking_stage.set_text("Native average stacking registered lights")
        stack_result = stacking.stack(
            registered_result["sequence"],
            workflow.output_path("stacks/stacked_L.fit"),
            method="average",
            metadata={"OBJECT": "AN_SEQUENCE_LRGB"},
        )
        stacking_stage.set_value(1.0)
        if stacking_stage.is_cancelled():
            raise afternight.WorkflowError("Workflow cancelled")

        report_path = workflow.output_path(output_name)
        with open(report_path, "w", encoding="utf-8") as report:
            report.write("AfterNight Sequence Workflow Demo\n")
            report.write(f"session={session.session_id}\n")
            report.write(f"extension={session.extension_id}\n")
            report.write(f"launch_mode={session.launch_mode}\n")
            report.write(f"working_dir={working_dir}\n")
            report.write(f"frames={len(sequence)}\n")
            report.write("calibration_summary\n")
            for role in ("bias", "dark", "flat"):
                master = masters.get(role, {})
                report.write(
                    f"master_{role}={master.get('path', '')} "
                    f"frames={master.get('frame_count', 0)} "
                    f"native={master.get('native', False)}\n"
                )
            report.write("registration_summary\n")
            report.write(
                f"registered_frames={registered_result['frame_count']} "
                f"manifest={registered_result['manifest_path']}\n"
            )
            report.write("stacking_summary\n")
            report.write(
                f"stacked_L={stack_result['path']} "
                f"frames={stack_result['frame_count']} method={stack_result['method']}\n"
            )
            report.write("output_artifacts\n")
            for role in ("bias", "dark", "flat"):
                master = masters.get(role, {})
                if master.get("path"):
                    report.write(f"{master['path']}\n")
            report.write(f"{registered_result['manifest_path']}\n")
            report.write(f"{stack_result['path']}\n")
            for frame in sequence.frames():
                report.write(
                    f"{frame.index}: {frame.path} included={frame.included} "
                    f"metadata={frame.metadata} registration={frame.registration}\n"
                )

        workflow.open_result_sequence(report_path)
        self.settings.set("last_report_path", report_path)
        self.settings.set("last_sequence_frames", frame_count)
        self.settings.set("last_pipeline_status", "native_stacked")
        self.settings.set("last_stack_path", stack_result["path"])
        afternight.log_info(
            f"Sequence workflow demo wrote {report_path}",
            component="an.demo.sequence_workflow",
        )
        progress.set_text("Workflow complete")
        progress.set_value(1.0)
