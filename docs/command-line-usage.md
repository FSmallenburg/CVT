# Command-line usage

This page describes the currently supported command-line interface for CVT.

## Synopsis

```bash
cvt [trajectory_file]
```

## Arguments

- trajectory_file: Optional path to a trajectory file.

If omitted, CVT starts with an empty scene and shows a status message asking you to drop a trajectory file into the window.

## Examples

```bash
# Open directly at startup
./build-release/cvt TestInputFiles/polydisperse.osph

# Start empty, then drag and drop a file
./build-release/cvt
```

## Current limitations

- No long-form flags are currently supported (for example, no --help or --version yet).
- If an unsupported extension is supplied, the open status displays the parser error.

## Validation checklist

- Run with no arguments and verify the app opens.
- Run with a valid sample file and verify frame 1 loads.
- Run with an invalid extension and verify a clear error message is shown.
- Drag-drop still works after startup with no arguments.
