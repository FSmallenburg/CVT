#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// Settings loaded once from cvt.ini at startup.
// The program never writes back to the file; changes survive only for
// the current session (closing and reopening the program resets to
// whatever the file says, or to the built-in defaults if no file is found).
struct ViewerConfig
{
    // UI / display
    bool showUi                           = true;
    bool showBox                          = true;
    bool basicControlsOpen                = false;
    int  lightingLevel                    = 14;

    // Structure-factor computation
    bool     structureFactorUseGpu              = true;
    bool     structureFactorSuppressCentralPeak = true;
    uint32_t structureFactorCpuModesPerStep     = 32u;
    uint16_t structureFactorGpuRowsPerStep      = 16u;
};

// Parse a cvt.ini file.  Returns defaults for any key that is absent or
// cannot be parsed.  Silently ignores unknown keys.
ViewerConfig loadViewerConfig(const std::filesystem::path &path);
