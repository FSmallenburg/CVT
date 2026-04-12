# GUI usage and file dropping

This page describes the viewer interaction model in CVT, including drag-drop behavior.

## Startup behavior

- On startup with a path argument, CVT attempts to open that file immediately.
- On startup without a path, CVT opens with no trajectory loaded.

## Opening files by drag and drop

- Drag a supported trajectory file from your file manager and drop it onto the CVT window.
- The dropped file path is queued and opened in the main update loop.
- Success or failure is shown in the status line at the top of the controls panel.

## Main controls panel

The right-side Viewer Controls panel shows:

- Current file path and FPS
- Open status/error message
- Particle type, visible count, selected count
- Frame slider (for multi-frame trajectories)
- Basic controls, Analysis panels, Structure factor panel, size distribution
- Keyboard commands section (collapsed by default)

## Interaction basics

- Left drag: rotate view
- Shift + left drag: translate view
- Mouse wheel: zoom
- Left click: pick/select particle
- Shift + pick interactions are supported in analysis-specific contexts

## Keyboard overview

Use the built-in Keyboard commands section in the controls panel for the full, current shortcut list.

## Validation checklist

- Launch with no file, then drag-drop a supported file and confirm it opens.
- Drop an unsupported file and verify a readable parse/extension error.
- Confirm frame slider appears only when multiple frames exist.
- Confirm keyboard command panel is collapsed on first open.
