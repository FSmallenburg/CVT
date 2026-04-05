#include "ViewerUi.h"

#include "ImGuiBgfx.h"

#include "imgui.h"
#include <implot.h>

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{

constexpr uint8_t kLightingLevelCount = 30u;
constexpr float kMinLightingScale = 0.3f;
constexpr float kMaxLightingScale = 1.75f;
constexpr float kUiPanelWidthFraction = 1.0f / 3.0f;
constexpr uint16_t kMinUiPanelWidth = 360u;
constexpr uint16_t kMinRenderViewportWidth = 640u;
constexpr float kMinParticleSizeScale = 0.01f;

int analysisColorModeComboIndex(AnalysisColorMode analysisColorMode)
{
    switch (analysisColorMode)
    {
    case AnalysisColorMode::Disabled:
        return 0;
    case AnalysisColorMode::NeighborCount:
        return 1;
    case AnalysisColorMode::BondOrientationalOrderMagnitude:
        return 2;
    case AnalysisColorMode::BondOrientationalOrderPhase:
        return 3;
    }

    return 0;
}

AnalysisColorMode analysisColorModeFromComboIndex(int comboIndex, bool supportsPsi6)
{
    switch (comboIndex)
    {
    case 1:
        return AnalysisColorMode::NeighborCount;
    case 2:
        return supportsPsi6 ? AnalysisColorMode::BondOrientationalOrderMagnitude
                            : AnalysisColorMode::NeighborCount;
    case 3:
        return supportsPsi6 ? AnalysisColorMode::BondOrientationalOrderPhase
                            : AnalysisColorMode::NeighborCount;
    default:
        return AnalysisColorMode::Disabled;
    }
}

struct SizeDistributionData
{
    std::vector<float> binCounts;
    std::vector<float> binCenters;
    size_t sampleCount = 0u;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float plotMinValue = 0.0f;
    float plotMaxValue = 0.0f;
    float meanValue = 0.0f;
    float standardDeviation = 0.0f;
    float maxBinCount = 0.0f;
    float binWidth = 0.0f;
};

SizeDistributionData buildSizeDistributionData(const ParticleSystem &particleSystem,
                                               bool visibleOnly,
                                               uint16_t requestedBinCount)
{
    SizeDistributionData data;

    std::vector<float> diameters;
    diameters.reserve(particleSystem.particles().size());
    for (const Particle &particle : particleSystem.particles())
    {
        if (visibleOnly && !particle.visible)
        {
            continue;
        }
        diameters.push_back(2.0f * particle.sizeParams[0]);
    }

    data.sampleCount = diameters.size();
    if (diameters.empty())
    {
        return data;
    }

    data.minValue = *std::min_element(diameters.begin(), diameters.end());
    data.maxValue = *std::max_element(diameters.begin(), diameters.end());

    double sum = 0.0;
    for (float diameter : diameters)
    {
        sum += static_cast<double>(diameter);
    }
    data.meanValue = static_cast<float>(sum / static_cast<double>(diameters.size()));

    double squaredDifferenceSum = 0.0;
    for (float diameter : diameters)
    {
        const double delta = static_cast<double>(diameter) - static_cast<double>(data.meanValue);
        squaredDifferenceSum += delta * delta;
    }
    data.standardDeviation = static_cast<float>(
        std::sqrt(squaredDifferenceSum / static_cast<double>(diameters.size())));

    const uint16_t binCount = bx::max<uint16_t>(requestedBinCount, 1u);
    data.binCounts.assign(binCount, 0.0f);
    data.binCenters.assign(binCount, 0.0f);

    const float valueSpan = data.maxValue - data.minValue;
    data.plotMinValue = data.minValue;
    data.plotMaxValue = data.maxValue;
    if (valueSpan <= 1.0e-6f)
    {
        const float halfSpan = bx::max(1.0e-4f,
                                       0.05f * bx::max(std::abs(data.meanValue), 1.0f));
        data.plotMinValue = data.meanValue - halfSpan;
        data.plotMaxValue = data.meanValue + halfSpan;
    }

    data.binWidth = (data.plotMaxValue - data.plotMinValue) / float(binCount);
    if (data.binWidth <= 1.0e-6f)
    {
        data.binWidth = 1.0f;
    }

    for (uint16_t binIndex = 0u; binIndex < binCount; ++binIndex)
    {
        data.binCenters[binIndex] = data.plotMinValue
                                    + (float(binIndex) + 0.5f) * data.binWidth;
    }

    const float inverseBinWidth = 1.0f / data.binWidth;
    for (float diameter : diameters)
    {
        int binIndex = static_cast<int>((diameter - data.plotMinValue) * inverseBinWidth);
        binIndex = std::clamp(binIndex, 0, int(binCount) - 1);
        data.binCounts[static_cast<size_t>(binIndex)] += 1.0f;
    }

    data.maxBinCount = *std::max_element(data.binCounts.begin(), data.binCounts.end());
    return data;
}

}

const char *particleTypeName(TrajectoryReader::FileType fileType)
{
    switch (fileType)
    {
    case TrajectoryReader::FileType::Disk:
        return "disk";
    case TrajectoryReader::FileType::OrderedSphere:
        return "ordered sphere";
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

std::string colorModeName(ColorMode colorMode, const ViewerState &viewerState)
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
    case ColorMode::ParticleSize:
        return "size";
    case ColorMode::Count:
        break;
    }

    const size_t colorModeIndex = static_cast<size_t>(colorMode);
    const size_t firstOrderParameterMode = static_cast<size_t>(ColorMode::Count);
    if (colorModeIndex >= firstOrderParameterMode)
    {
        const size_t orderParameterIndex = colorModeIndex - firstOrderParameterMode;
        if (orderParameterIndex < viewerState.orderParameterCount)
        {
            return "order " + std::to_string(orderParameterIndex + 1u);
        }
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
                        const BondDiagramResources *bondDiagramResources,
                        const StructureFactorResources *structureFactorResources,
                        TrajectoryReader::FileType particleFileType,
                        const std::string &loadedPath,
                        size_t currentFrame, size_t totalFrames,
                        uint16_t windowWidth, uint16_t windowHeight,
                        float cutPlaneMinSceneZ, float cutPlaneMaxSceneZ)
{
    viewerState.bondDiagramRenderRequested = false;

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
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 110.0f);
    ImGui::Text("FPS: %.1f", viewerState.currentFps);
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

    if (ImGui::CollapsingHeader("Basic controls"))
    {
        if (ImGui::Checkbox("Show simulation box (b)", &viewerState.showBox))
        {
            markPickDirty = true;
        }
        if (ImGui::Checkbox("Wrap particles to box (w)", &viewerState.wrapParticlesToBox))
        {
            markBondLikeHelperSystemsDirty(viewerState);
            markPickDirty = true;
        }

        bool bondModeEnabled = viewerState.bondModeEnabled;
        if (ImGui::Checkbox("Bond mode (m)", &bondModeEnabled))
        {
            viewerState.bondModeEnabled = bondModeEnabled;
            if (viewerState.bondModeEnabled)
            {
                viewerState.mobilityModeEnabled = false;
                viewerState.nearestNeighborModeEnabled = false;
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
                viewerState.nearestNeighborModeEnabled = false;
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
            markBondDiagramViewDirty(viewerState);
        }

        clampColorModeToAvailable(viewerState);
        std::vector<std::string> colorModeLabels = {
            "File default",
            "Palette cycle",
            "Uniform",
            "Orientation",
            "Bond count",
            "Particle size",
        };
        for (uint16_t orderParameterIndex = 0u;
             orderParameterIndex < viewerState.orderParameterCount;
             ++orderParameterIndex)
        {
            colorModeLabels.push_back("Order parameter "
                                      + std::to_string(orderParameterIndex + 1u));
        }

        std::vector<const char *> colorModeLabelPointers;
        colorModeLabelPointers.reserve(colorModeLabels.size());
        for (const std::string &label : colorModeLabels)
        {
            colorModeLabelPointers.push_back(label.c_str());
        }

        int colorModeIndex = static_cast<int>(viewerState.colorMode);
        if (ImGui::Combo("Color mode", &colorModeIndex,
                         colorModeLabelPointers.data(),
                         static_cast<int>(colorModeLabelPointers.size())))
        {
            viewerState.colorMode = static_cast<ColorMode>(colorModeIndex);
            markColorDependentHelperSystemsDirty(viewerState);
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
                    markVisibilityDependentHelperSystemsDirty(viewerState);
                    markPickDirty = true;
                }
            }
            ImGui::TextDisabled("Hotkeys: 1-8 toggle species, Shift+1-8 solo.");
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

        ImGui::BeginDisabled(viewerState.selectedIds.size() != 2u);
        if (ImGui::Button("Align view to selection (a)"))
        {
            viewerState.pendingAlignViewToSelection = true;
        }
        ImGui::EndDisabled();

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

    if (ImGui::CollapsingHeader("Analysis", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        ImGui::TextDisabled("Tools for neighbor metrics, bond diagrams, and S(k).");

        if (ImGui::CollapsingHeader("Neighbor analysis"))
        {
            const bool supportsPsi6 =
                viewerState.fileDimensionality == TrajectoryReader::Dimensionality::TwoDimensional;
            if (!supportsPsi6
                && (viewerState.analysisColorMode
                        == AnalysisColorMode::BondOrientationalOrderMagnitude
                    || viewerState.analysisColorMode
                        == AnalysisColorMode::BondOrientationalOrderPhase))
            {
                viewerState.analysisColorMode = AnalysisColorMode::Disabled;
                markColorDependentHelperSystemsDirty(viewerState);
            }

            float neighborCutoffFactor = viewerState.neighborCutoffFactor;
            if (ImGui::SliderFloat("Neighbor cutoff factor", &neighborCutoffFactor,
                                1.0f, 2.0f, "%.2f"))
            {
                viewerState.neighborCutoffFactor = neighborCutoffFactor;
                viewerState.neighborAnalysisValid = false;
                particleSystem.clearNeighborAnalysis();
                markNearestNeighborRenderSystemsDirty(viewerState);
                markBondDiagramGeometryDirty(viewerState);
                if (viewerState.analysisColorMode != AnalysisColorMode::Disabled)
                {
                    markColorDependentHelperSystemsDirty(viewerState);
                }
                viewerState.pendingFindNeighbors = viewerState.autoFindNeighbors;
                markPickDirty = true;
            }

            if (ImGui::Button("Find neighbors"))
            {
                viewerState.pendingFindNeighbors = true;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("auto", &viewerState.autoFindNeighbors)
                && viewerState.autoFindNeighbors
                && !viewerState.neighborAnalysisValid)
            {
                viewerState.pendingFindNeighbors = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Toggle neighbor mode (Shift-N)"))
            {
                viewerState.nearestNeighborModeEnabled = !viewerState.nearestNeighborModeEnabled;
                if (viewerState.nearestNeighborModeEnabled)
                {
                    viewerState.bondModeEnabled = false;
                    viewerState.mobilityModeEnabled = false;
                }
                markPickDirty = true;
            }

            ImGui::Text("Neighbors: %s",
                        viewerState.neighborAnalysisValid ? "computed" : "not computed");

            ImGui::BeginDisabled(!viewerState.neighborAnalysisValid);
            int analysisColorModeIndex =
                analysisColorModeComboIndex(viewerState.analysisColorMode);
            if (supportsPsi6)
            {
                static const char *kAnalysisColorModeLabels2D[] = {
                    "None",
                    "Neighbor count",
                    "Bond-orientational order: magnitude",
                    "Bond-orientational order: phase",
                };
                if (ImGui::Combo("Analysis color", &analysisColorModeIndex,
                                kAnalysisColorModeLabels2D,
                                IM_ARRAYSIZE(kAnalysisColorModeLabels2D)))
                {
                    viewerState.analysisColorMode =
                        analysisColorModeFromComboIndex(analysisColorModeIndex, true);
                    markColorDependentHelperSystemsDirty(viewerState);
                }
            }
            else
            {
                static const char *kAnalysisColorModeLabels3D[] = {
                    "None",
                    "Neighbor count",
                };
                analysisColorModeIndex = bx::min(analysisColorModeIndex, 1);
                if (ImGui::Combo("Analysis color", &analysisColorModeIndex,
                                kAnalysisColorModeLabels3D,
                                IM_ARRAYSIZE(kAnalysisColorModeLabels3D)))
                {
                    viewerState.analysisColorMode =
                        analysisColorModeFromComboIndex(analysisColorModeIndex, false);
                    markColorDependentHelperSystemsDirty(viewerState);
                }
            }
            ImGui::EndDisabled();

            const bool usesBondOrientationalColor =
                viewerState.analysisColorMode == AnalysisColorMode::BondOrientationalOrderMagnitude
                || viewerState.analysisColorMode == AnalysisColorMode::BondOrientationalOrderPhase;
            ImGui::BeginDisabled(!supportsPsi6 || !usesBondOrientationalColor);
            int symmetryOrderIndex = int(viewerState.bondOrientationalOrder) - 2;
            static const char *kBondOrientationalOrderLabels[] = {
                "2",
                "3",
                "4",
                "5",
                "6",
            };
            if (ImGui::Combo("Bond-order symmetry", &symmetryOrderIndex,
                            kBondOrientationalOrderLabels,
                            IM_ARRAYSIZE(kBondOrientationalOrderLabels)))
            {
                viewerState.bondOrientationalOrder =
                    static_cast<uint8_t>(symmetryOrderIndex + 2);
                viewerState.pendingRefreshAnalysisResults = viewerState.neighborAnalysisValid;
            }
            ImGui::EndDisabled();

            if (viewerState.neighborAnalysisValid
                && ImGui::CollapsingHeader("Bond diagram"))
            {
                viewerState.bondDiagramRenderRequested = true;
                ImGui::Separator();
                if (bondDiagramResources != nullptr && bondDiagramResources->enabled
                    && bgfx::isValid(bondDiagramResources->colorTexture))
                {
                    const float sofqImageSize = bx::max(1.0f, ImGui::GetContentRegionAvail().x - 6.0f);
                    ImGui::Image(bondDiagramResources->colorTexture,
                                ImVec2(sofqImageSize, sofqImageSize));
                }
                else if (bondDiagramResources != nullptr
                        && !bondDiagramResources->disableReason.empty())
                {
                    ImGui::TextWrapped("Structure factor unavailable: %s",
                                    bondDiagramResources->disableReason.c_str());
                }

                float bondDiagramPointScale = viewerState.bondDiagramPointScale;
                if (ImGui::SliderFloat("Point size", &bondDiagramPointScale,
                                    0.01f, 0.12f, "%.3f"))
                {
                    viewerState.bondDiagramPointScale = bondDiagramPointScale;
                    markBondDiagramGeometryDirty(viewerState);
                }
            }         
        }           

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Structure factor"))
        {
            if (ImGui::Button("Compute structure factor"))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                markStructureFactorDirty(viewerState);
                viewerState.structureFactorPendingCompute = true;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Auto update##StructureFactor",
                                &viewerState.structureFactorAutoUpdate))
            {
                if (!viewerState.structureFactorAutoUpdate)
                {
                    viewerState.structureFactorInteractionLowResActive = false;
                }
                else if (viewerState.structureFactorDirty)
                {
                    viewerState.structureFactorPendingCompute = true;
                }
            }

            bool useGpu = viewerState.structureFactorUseGpu;
            if (ImGui::Checkbox("Use GPU##StructureFactor", &useGpu))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorUseGpu = useGpu;
                markStructureFactorDirty(viewerState);
                if (viewerState.structureFactorAutoUpdate)
                {
                    viewerState.structureFactorPendingCompute = true;
                }
            }

            int maxMode = viewerState.structureFactorMaxModeX;
            if (ImGui::SliderInt("Max |k-mode|", &maxMode, 4, 128))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorMaxModeX = maxMode;
                viewerState.structureFactorMaxModeY = maxMode;
                markStructureFactorDirty(viewerState);
            }

            int sofqPixels = int(viewerState.structureFactorImageSize);
            if (ImGui::SliderInt("Image resolution", &sofqPixels, 64, 384))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorImageSize =
                    static_cast<uint16_t>(std::clamp(sofqPixels, 64, 384));
                markStructureFactorDirty(viewerState);
            }

            int blurRadius = int(viewerState.structureFactorBlurRadius);
            if (ImGui::SliderInt("Blur radius", &blurRadius, 0, 2))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorBlurRadius =
                    static_cast<uint8_t>(std::clamp(blurRadius, 0, 2));
                markStructureFactorDirty(viewerState);
            }

            float colorRangeMin = viewerState.structureFactorColorRangeMin;
            if (ImGui::SliderFloat("Minimum intensity", &colorRangeMin, 0.0f, 0.99f, "%.2f"))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorColorRangeMin =
                    std::clamp(colorRangeMin, 0.0f, viewerState.structureFactorColorRangeMax - 0.01f);
                markStructureFactorDirty(viewerState);
            }

            float colorRangeMax = viewerState.structureFactorColorRangeMax;
            if (ImGui::SliderFloat("Maximum intensity", &colorRangeMax, 0.01f, 1.0f, "%.2f"))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorColorRangeMax =
                    std::clamp(colorRangeMax, viewerState.structureFactorColorRangeMin + 0.01f, 1.0f);
                markStructureFactorDirty(viewerState);
            }

            bool logScale = viewerState.structureFactorLogScale;
            if (ImGui::Checkbox("Log scale##StructureFactor", &logScale))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorLogScale = logScale;
                markStructureFactorDirty(viewerState);
            }

            bool suppressCentralPeak = viewerState.structureFactorSuppressCentralPeak;
            if (ImGui::Checkbox("Suppress k=0 peak", &suppressCentralPeak))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorSuppressCentralPeak = suppressCentralPeak;
                markStructureFactorDirty(viewerState);
            }

            bool visibleOnly = viewerState.structureFactorUseVisibleParticlesOnly;
            if (ImGui::Checkbox("Visible particles only", &visibleOnly))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorUseVisibleParticlesOnly = visibleOnly;
                ++viewerState.structureFactorDataRevision;
                if (viewerState.structureFactorDataRevision == 0u)
                {
                    viewerState.structureFactorDataRevision = 1u;
                }
                markStructureFactorDirty(viewerState);
            }

            ImGui::Text("Status: %s",
                        viewerState.structureFactorDirty ? "stale - recompute needed"
                                                         : "up to date");

            if (viewerState.structureFactorDirty && structureFactorResources != nullptr
                && structureFactorResources->enabled)
            {
                ImGui::TextWrapped("The current structure factor is stale and no longer matches the scene orientation.");
            }

            if (structureFactorResources != nullptr && !structureFactorResources->statusText.empty())
            {
                ImGui::TextWrapped("%s", structureFactorResources->statusText.c_str());
            }

            if (structureFactorResources != nullptr && structureFactorResources->enabled
                && bgfx::isValid(structureFactorResources->colorTexture))
            {
                ImGui::Text("Last compute: %.1f ms using %zu particles",
                            structureFactorResources->computeMilliseconds,
                            structureFactorResources->particleCount);
                const float sofqImageSize = bx::max(1.0f, ImGui::GetContentRegionAvail().x - 6.0f);
                ImGui::Image(structureFactorResources->colorTexture,
                             ImVec2(sofqImageSize, sofqImageSize));
            }
            else if (structureFactorResources != nullptr
                     && !structureFactorResources->disableReason.empty())
            {
                ImGui::TextWrapped("Structure factor unavailable: %s",
                                   structureFactorResources->disableReason.c_str());
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Size distribution"))
        {
            bool useVisibleOnly = viewerState.sizeDistributionUseVisibleOnly;
            if (ImGui::Checkbox("Visible particles only##SizeDistribution", &useVisibleOnly))
            {
                viewerState.sizeDistributionUseVisibleOnly = useVisibleOnly;
            }

            int binCount = int(viewerState.sizeDistributionBinCount);
            if (ImGui::SliderInt("Bins##SizeDistribution", &binCount, 4, 128))
            {
                viewerState.sizeDistributionBinCount =
                    static_cast<uint16_t>(std::clamp(binCount, 4, 128));
            }

            const SizeDistributionData sizeDistribution =
                buildSizeDistributionData(particleSystem,
                                          viewerState.sizeDistributionUseVisibleOnly,
                                          viewerState.sizeDistributionBinCount);
            if (sizeDistribution.sampleCount == 0u)
            {
                ImGui::TextDisabled("No particles available for the current filter.");
            }
            else
            {
                ImGui::Text("Count: %zu", sizeDistribution.sampleCount);
                ImGui::Text("Mean diameter: %.4f", sizeDistribution.meanValue);
                ImGui::Text("SD: %.4f", sizeDistribution.standardDeviation);
                ImGui::Text("Polydispersity: %.4f", sizeDistribution.standardDeviation / sizeDistribution.meanValue);
                ImGui::Text("Range: [%.4f, %.4f]", sizeDistribution.minValue,
                            sizeDistribution.maxValue);

                const float plotWidth =
                    bx::max(1.0f, ImGui::GetContentRegionAvail().x - 6.0f);
                if (ImPlot::GetCurrentContext() != nullptr
                    && ImPlot::BeginPlot("##SizeDistributionPlot",
                                         ImVec2(plotWidth, 180.0f),
                                         ImPlotFlags_NoLegend))
                {
                    ImPlot::SetupAxes("Diameter", "Count",
                                      ImPlotAxisFlags_AutoFit,
                                      ImPlotAxisFlags_AutoFit);
                    ImPlot::PlotBars("Count",
                                     sizeDistribution.binCenters.data(),
                                     sizeDistribution.binCounts.data(),
                                     static_cast<int>(sizeDistribution.binCounts.size()),
                                     static_cast<double>(sizeDistribution.binWidth) * 0.95);
                    ImPlot::EndPlot();
                }
                else
                {
                    std::string overlayText = std::to_string(sizeDistribution.sampleCount)
                                              + " particles";
                    ImGui::PlotHistogram("##SizeDistributionPlotFallback",
                                         sizeDistribution.binCounts.data(),
                                         static_cast<int>(sizeDistribution.binCounts.size()),
                                         0, overlayText.c_str(), 0.0f,
                                         bx::max(1.0f, sizeDistribution.maxBinCount),
                                         ImVec2(plotWidth, 160.0f));
                }
                ImGui::TextDisabled("Histogram uses particle diameters from the current snapshot.");
            }
        }

        ImGui::Unindent();
    }

    if (markPickDirty)
    {
        markPickBufferDirty(viewerState);
    }

    ImGui::End();
}