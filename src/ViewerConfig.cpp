#include "ViewerConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace
{

std::string trimWhitespace(const std::string &s)
{
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1u);
}

bool parseBool(const std::string &value, bool fallback)
{
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "true"  || lower == "1" || lower == "yes" || lower == "on")  return true;
    if (lower == "false" || lower == "0" || lower == "no"  || lower == "off") return false;
    return fallback;
}

int parseInt(const std::string &value, int fallback)
{
    try
    {
        size_t pos = 0u;
        const int result = std::stoi(value, &pos);
        return (pos > 0u) ? result : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

} // namespace

ViewerConfig loadViewerConfig(const std::filesystem::path &path)
{
    ViewerConfig config;

    std::ifstream file(path);
    if (!file.is_open())
    {
        return config;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // Strip inline comments
        const size_t commentPos = line.find('#');
        if (commentPos != std::string::npos)
        {
            line = line.substr(0u, commentPos);
        }

        const size_t eqPos = line.find('=');
        if (eqPos == std::string::npos)
        {
            continue;
        }

        const std::string key   = trimWhitespace(line.substr(0u, eqPos));
        const std::string value = trimWhitespace(line.substr(eqPos + 1u));

        if (key.empty() || value.empty())
        {
            continue;
        }

        if (key == "show_ui")
            config.showUi = parseBool(value, config.showUi);
        else if (key == "show_box")
            config.showBox = parseBool(value, config.showBox);
        else if (key == "basic_controls_open")
            config.basicControlsOpen = parseBool(value, config.basicControlsOpen);
        else if (key == "lighting_level")
            config.lightingLevel = std::clamp(parseInt(value, config.lightingLevel), 0, 29);
        else if (key == "sf_use_gpu")
            config.structureFactorUseGpu = parseBool(value, config.structureFactorUseGpu);
        else if (key == "sf_suppress_central_peak")
            config.structureFactorSuppressCentralPeak =
                parseBool(value, config.structureFactorSuppressCentralPeak);
        else if (key == "sf_cpu_modes_per_step")
            config.structureFactorCpuModesPerStep =
                static_cast<uint32_t>(std::clamp(parseInt(value, int(config.structureFactorCpuModesPerStep)), 8, 512));
        else if (key == "sf_gpu_rows_per_step")
            config.structureFactorGpuRowsPerStep =
                static_cast<uint16_t>(std::clamp(parseInt(value, int(config.structureFactorGpuRowsPerStep)), 4, 128));
        // Unknown keys are silently ignored.
    }

    return config;
}
