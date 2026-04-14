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

## Neighbor analysis FK controls

Inside the Neighbor analysis panel:

- Switch to FK view mode:
	- Computes Frank-Kasper bonds from the current neighbor graph.
	- Enables bond rendering and switches coloring to bond-count mode.
- Hide unbonded (!12 neighbors):
	- Hidden until FK mode has been activated at least once.
	- When enabled, hides particles that have zero FK bonds and whose neighbor count is not 12.
- Auto-recalculate FK:
	- Hidden until FK mode has been activated at least once.
	- Enabled by default after first FK activation.
	- When enabled, FK bonds are recomputed automatically after neighbor recomputation events
		(for example cutoff changes or frame changes).

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
