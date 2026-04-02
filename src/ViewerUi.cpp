#include "ViewerUi.h"

#include "ImGuiBgfx.h"

#include <bx/math.h>
#include <dear-imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace
{

constexpr uint8_t kLightingLevelCount = 30u;
constexpr float kMinLightingScale = 0.3f;
constexpr float kMaxLightingScale = 1.75f;
constexpr float kUiPanelWidthFraction = 1.0f / 3.0f;
constexpr uint16_t kMinUiPanelWidth = 360u;
constexpr uint16_t kMinRenderViewportWidth = 640u;
constexpr float kMinParticleSizeScale = 0.01f;

}

const char *particleTypeName(TrajectoryReader::FileType fileType)
{
    switch (fileType)
    {
    case TrajectoryReader::FileType::Disk:
        return "disk";
    case TrajectoryReader::FileType::Rod:
        return "rod";
    case TrajectoryReader::FileType::Cube:
        return "cube";
    case TrajectoryReader::FileType::Polygon:
        return "polygon";
    case TrajectoryReader::FileType::Patchy:
    case TrajectoryReader::FileType::PatchyLegacy:
        return "patchy";
    case TrajectoryReader::FileType::Patchy2D:
        return "patchy2d";
    case TrajectoryReader::FileType::Sphere:
        return "sphere";
    }

    return "sphere";
}

const char *colorModeName(ColorMode colorMode)
{
    switch (colorMode)
    {
    case ColorMode::FileDefault:
        return "file";
    case ColorMode::PaletteCycle:
        return "palette";
    case ColorMode::Uniform:
        return "uniform";
    case ColorMode::Orientation:
        return "orientation";
    case ColorMode::BondCount:
        return "bonds";
    case ColorMode::Count:
        break;
    }

    return "unknown";
}

float lightingScaleFromIndex(uint8_t lightingLevelIndex)
{
    const uint8_t clampedIndex = bx::min<uint8_t>(lightingLevelIndex, kLightingLevelCount - 1u);
    if (kLightingLevelCount <= 1u)
    {
        return kMinLightingScale;
    }

    const float t = float(clampedIndex) / float(kLightingLevelCount - 1u);
    return bx::lerp(kMinLightingScale, kMaxLightingScale, t);
}

size_t countVisibleParticles(const ParticleSystem &particleSystem)
{
    return static_cast<size_t>(std::count_if(
        particleSystem.particles().begin(), particleSystem.particles().end(),
        [](const Particle &particle) { return particle.visible; }));
}

void printVisibleParticleCount(const ParticleSystem &particleSystem)
{
    std::cout << "Visible particles: " << countVisibleParticles(particleSystem) << std::endl;
}

uint16_t computeUiPanelWidth(uint16_t windowWidth, bool showUi)
{
    if (!showUi || !ImGuiBgfx::isAvailable() || windowWidth <= kMinRenderViewportWidth)
    {
        return 0u;
    }

    const uint16_t maxUiPanelWidth = static_cast<uint16_t>(windowWidth - kMinRenderViewportWidth);
    uint16_t requestedWidth = static_cast<uint16_t>(
        std::lround(static_cast<float>(windowWidth) * kUiPanelWidthFraction));
    requestedWidth = static_cast<uint16_t>(bx::max<uint16_t>(requestedWidth, kMinUiPanelWidth));
    return bx::min<uint16_t>(requestedWidth, maxUiPanelWidth);
}

uint16_t computeRenderViewportWidth(uint16_t windowWidth, bool showUi)
{
    const uint16_t uiPanelWidth = computeUiPanelWidth(windowWidth, showUi);
    return static_cast<uint16_t>(bx::max<int>(int(windowWidth) - int(uiPanelWidth), 1));
}

bool isInRenderViewport(const ViewerState &viewerState, double mouseX, double mouseY)
{
    return viewerState.renderViewportWidth > 0
           && viewerState.renderViewportHeight > 0
           && mouseX >= 0.0
           && mouseY >= 0.0
           && mouseX < static_cast<double>(viewerState.renderViewportWidth)
           && mouseY < static_cast<double>(viewerState.renderViewportHeight);
}

void drawViewerControls(ViewerState &viewerState, ParticleSystem &particleSystem,
                        TrajectoryReader::FileType particleFileType,
                        const std::string &loadedPath,
                        size_t currentFrame, size_t totalFrames,
                        uint16_t windowWidth, uint16_t windowHeight,
                        float cutPlaneMinSceneZ, float cutPlaneMaxSceneZ)
{
    if (!viewerState.showUi || !ImGuiBgfx::isAvailable())
    {
        return;
    }

    bool markPickDirty = false;

    const float panelX = static_cast<float>(windowWidth - viewerState.uiPanelWidth);
    ImGui::SetNextWindowPos(ImVec2(panelX, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(viewerState.uiPanelWidth),
                                    static_cast<float>(windowHeight)),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoResize
                                         | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Viewer Controls", &viewerState.showUi, windowFlags))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("File: %s", loadedPath.empty() ? "<none>" : loadedPath.c_str());
    ImGui::Text("Particle type: %s", particleTypeName(particleFileType));
    ImGui::Text("Visible: %zu", countVisibleParticles(particleSystem));
    ImGui::Text("Selected: %zu", viewerState.selectedIds.size());

    if (totalFrames > 1)
    {
        const int displayedFrameNumber = static_cast<int>(currentFrame) + 1;
        ImGui::Text("Frame: %d / %zu", displayedFrameNumber, totalFrames);

        int sliderFrameNumber = displayedFrameNumber;
        if (ImGui::SliderInt("##FrameSlider", &sliderFrameNumber, 1,
                             static_cast<int>(totalFrames), ""))
        {
            viewerState.pendingFrameIndex = sliderFrameNumber - 1;
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Basic controls", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Show simulation box (b)", &viewerState.showBox))
        {
            markPickDirty = true;
        }
        if (ImGui::Checkbox("Wrap particles to box (w)", &viewerState.wrapParticlesToBox))
        {
            markPickDirty = true;
        }

        bool bondModeEnabled = viewerState.bondModeEnabled;
        if (ImGui::Checkbox("Bond mode (m)", &bondModeEnabled))
        {
            viewerState.bondModeEnabled = bondModeEnabled;
            if (viewerState.bondModeEnabled)
            {
                viewerState.mobilityModeEnabled = false;
            }
            markPickDirty = true;
        }

        bool mobilityModeEnabled = viewerState.mobilityModeEnabled;
        if (ImGui::Checkbox("Mobility mode (Shift-M)", &mobilityModeEnabled))
        {
            viewerState.mobilityModeEnabled = mobilityModeEnabled;
            if (viewerState.mobilityModeEnabled)
            {
                viewerState.bondModeEnabled = false;
            }
            markPickDirty = true;
        }

        float particleSizeScale = viewerState.particleSizeScale;
        if (ImGui::SliderFloat("Particle size scale (/ and *)", &particleSizeScale,
                               kMinParticleSizeScale, 3.0f, "%.2f"))
        {
            viewerState.particleSizeScale = particleSizeScale;
            markPickDirty = true;
        }

        int lightingLevelIndex = viewerState.lightingLevelIndex;
        if (ImGui::SliderInt("Lighting level (s)", &lightingLevelIndex, 0,
                             int(kLightingLevelCount - 1u)))
        {
            viewerState.lightingLevelIndex = static_cast<uint8_t>(lightingLevelIndex);
        }

        static const char *kColorModeLabels[] = {
            "File default",
            "Palette cycle",
            "Uniform",
            "Orientation",
            "Bond count",
        };
        int colorModeIndex = static_cast<int>(viewerState.colorMode);
        if (ImGui::Combo("Color mode", &colorModeIndex,
                         kColorModeLabels, IM_ARRAYSIZE(kColorModeLabels)))
        {
            viewerState.colorMode = static_cast<ColorMode>(colorModeIndex);
        }

        if (viewerState.maxSeenParticleTypeIndex > 0u)
        {
            ImGui::TextUnformatted("Particle types");
            for (uint8_t typeIndex = 0; typeIndex <= viewerState.maxSeenParticleTypeIndex;
                 ++typeIndex)
            {
                bool typeVisible = viewerState.particleTypeVisible[typeIndex];
                const char label[] = {
                    static_cast<char>('a' + bx::min<uint8_t>(typeIndex, 25u)),
                    '\0',
                };
                if (ImGui::Checkbox(label, &typeVisible))
                {
                    viewerState.particleTypeVisible[typeIndex] = typeVisible;
                    applyParticleVisibilityFilters(particleSystem, viewerState);
                    markPickDirty = true;
                }
            }
        }

        bool cutPlaneEnabled = viewerState.cutPlaneEnabled;

        if (ImGui::Button("Hide selected (h)"))
        {
            viewerState.pendingHideSelected = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reveal all (Shift-H)"))
        {
            viewerState.pendingRevealAll = true;
        }
        if (ImGui::Button("Clear selection (u)"))
        {
            viewerState.pendingUnselect = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Invert selection (i)"))
        {
            viewerState.pendingInvertSelected = true;
        }

        if (ImGui::Button("Screenshot (p)"))
        {
            viewerState.pendingScreenshotRequest = true;
        }
        if (ImGui::Checkbox("Enable cut plane", &cutPlaneEnabled))
        {
            viewerState.cutPlaneEnabled = cutPlaneEnabled;
            viewerState.cutPlaneSceneZ = cutPlaneEnabled ? 0.0f : cutPlaneMaxSceneZ;
            markPickDirty = true;
        }
        if (viewerState.cutPlaneEnabled)
        {
            float cutPlaneSceneZ = viewerState.cutPlaneSceneZ;
            if (ImGui::SliderFloat("Cut plane z", &cutPlaneSceneZ,
                                   cutPlaneMinSceneZ, cutPlaneMaxSceneZ, "%.2f"))
            {
                viewerState.cutPlaneSceneZ = cutPlaneSceneZ;
                markPickDirty = true;
            }
        }        
    }

    if (markPickDirty)
    {
        markPickBufferDirty(viewerState);
    }

    ImGui::End();
}