---
kind: process
stage: calibrate
menu: Process > Calibration > Flat Division
summary: Divides a light frame by a flat master, at the first stage of the pipeline.
data_in: cfa
data_out: cfa
destructive: false
preview: none
sequences: false
masks: none
external: A flat master on disk
glance:
  enabled: A raw frame is open.
  inputs: A light frame and a flat master.
  outputs: A new view with the divided frame.
---
# Flat Division

A process of the first stage: nothing comes before it, and its page says it leaves the target as it is and needs a
file from outside.

## Steps

1. **Choose the flat.** Pick the master that matches the filter.
2. **Divide.** A new view opens with the result.
