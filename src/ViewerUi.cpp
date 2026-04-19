#include "ViewerUi.h"

#include "BondOrderScatter.h"
#include "ImGuiBgfx.h"
#include "Log.h"

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
constexpr size_t kLargeStructureFactorParticleThreshold = 25000u;

// Returns the packing fraction (volume fraction in 3D, area fraction in 2D).
// Returns a negative value when the box has zero measure or the type is unsupported.
double computePackingFraction(const ParticleSystem &particleSystem,
                               const SimulationBox &simulationBox,
                               TrajectoryReader::FileType fileType,
                               TrajectoryReader::Dimensionality dimensionality)
{
    using FileType     = TrajectoryReader::FileType;
    using Dimensionality = TrajectoryReader::Dimensionality;
    using Shape        = SimulationBox::Shape;

    const bool is2D = (dimensionality == Dimensionality::TwoDimensional);

    // ---- box measure (volume or area) --------------------------------
    double boxMeasure = 0.0;
    if (simulationBox.shape() == Shape::Spherical)
    {
        const double r = static_cast<double>(simulationBox.renderRadius());
        boxMeasure = (4.0 / 3.0) * bx::kPi * r * r * r;
    }
    else
    {
        const bx::Vec3 sz = simulationBox.size();
        if (is2D)
        {
            boxMeasure = static_cast<double>(sz.x) * static_cast<double>(sz.y);
        }
        else
        {
            boxMeasure = static_cast<double>(sz.x) * static_cast<double>(sz.y)
                         * static_cast<double>(sz.z);
        }
    }

    if (boxMeasure <= 0.0)
    {
        return -1.0;
    }

    // ---- per-particle contribution -----------------------------------
    double particleSum = 0.0;
    for (const Particle &p : particleSystem.particles())
    {
        double contrib = 0.0;
        switch (fileType)
        {
        case FileType::Sphere:
        case FileType::BondedSphere:
        case FileType::OrderedSphere:
        {
            const double r = static_cast<double>(p.sizeParams[0]);
            contrib = (4.0 / 3.0) * bx::kPi * r * r * r;
            break;
        }
        case FileType::Disk:
        {
            const double r = static_cast<double>(p.sizeParams[0]);
            contrib = bx::kPi * r * r;
            break;
        }
        case FileType::Rod:
        {
            // Capsule: cylinder (radius r, length L) + two hemispherical caps (radius rcap).
            const double r    = static_cast<double>(p.sizeParams[0]);
            const double L    = static_cast<double>(p.sizeParams[1]);
            const double rcap = static_cast<double>(p.sizeParams[2]);
            contrib = bx::kPi * r * r * L + (4.0 / 3.0) * bx::kPi * rcap * rcap * rcap;
            break;
        }
        case FileType::Cube:
        {
            const double e = static_cast<double>(p.sizeParams[0]);
            contrib = e * e * e;
            break;
        }
        case FileType::Polygon:
        {
            // Regular n-gon with circumradius r: area = (n * r² * sin(2π/n)) / 2
            const double r = static_cast<double>(p.sizeParams[0]);
            const int    n = static_cast<int>(std::round(static_cast<double>(p.sizeParams[1])));
            if (n >= 3)
            {
                contrib = 0.5 * n * r * r * std::sin(2.0 * bx::kPi / n);
            }
            break;
        }
        case FileType::Patchy:
        case FileType::PatchyLegacy:
        {
            // Treat as a sphere using the core radius.
            const double r = static_cast<double>(p.sizeParams[0]);
            contrib = (4.0 / 3.0) * bx::kPi * r * r * r;
            break;
        }
        case FileType::Patchy2D:
        {
            // Treat as a disk using the core radius.
            const double r = static_cast<double>(p.sizeParams[0]);
            contrib = bx::kPi * r * r;
            break;
        }
        }
        particleSum += contrib;
    }

    return particleSum / boxMeasure;
}



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
    case AnalysisColorMode::BondOrientationalQLMagnitude:
        return 2;
    case AnalysisColorMode::BondOrientationalQBarLMagnitude:
        return 3;
    }

    return 0;
}

AnalysisColorMode analysisColorModeFromComboIndex(int comboIndex, bool isTwoDimensional)
{
    switch (comboIndex)
    {
    case 1:
        return AnalysisColorMode::NeighborCount;
    case 2:
        return isTwoDimensional ? AnalysisColorMode::BondOrientationalOrderMagnitude
                                : AnalysisColorMode::BondOrientationalQLMagnitude;
    case 3:
        return isTwoDimensional ? AnalysisColorMode::BondOrientationalOrderPhase
                                : AnalysisColorMode::BondOrientationalQBarLMagnitude;
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

constexpr std::array<const char *, 5> kBondOrientationalOrderLabels2D = {
    "2", "3", "4", "5", "6",
};
constexpr std::array<const char *, 7> kBondOrientationalOrderLabels3D = {
    "2", "3", "4", "5", "6", "7", "8",
};

void drawBondOrderScatterPanel(ViewerState &viewerState,
                               ParticleSystem &particleSystem,
                               bool isTwoDimensional,
                               uint8_t minimumBondOrientationalOrder,
                               uint8_t maximumBondOrientationalOrder,
                               bool &markPickDirty)
{
    if (!viewerState.neighborAnalysisValid
        || !ImGui::CollapsingHeader("Bond-order scatter plot"))
    {
        viewerState.bondOrderScatterInteraction.dragActive = false;
        return;
    }

    viewerState.bondOrderScatterXAxisOrder =
        std::clamp<uint8_t>(viewerState.bondOrderScatterXAxisOrder,
                            minimumBondOrientationalOrder,
                            maximumBondOrientationalOrder);
    viewerState.bondOrderScatterYAxisOrder =
        std::clamp<uint8_t>(viewerState.bondOrderScatterYAxisOrder,
                            minimumBondOrientationalOrder,
                            maximumBondOrientationalOrder);

    static const char *kBondOrderScatterModeLabels[] = {
        "Raw axes (q_l)",
        "Raw axes (qbar_l)",
        "PCA (q_l)",
        "PCA (qbar_l)",
    };
    int scatterModeIndex = static_cast<int>(viewerState.bondOrderScatterMode);
    if (ImGui::Combo("Scatter mode##BondOrderScatterMode",
                     &scatterModeIndex,
                     kBondOrderScatterModeLabels,
                     IM_ARRAYSIZE(kBondOrderScatterModeLabels)))
    {
        viewerState.bondOrderScatterMode =
            static_cast<BondOrderScatterMode>(scatterModeIndex);
    }

    if (!particleSystem.hasAnalysisResults(viewerState.bondOrderScatterXAxisOrder)
        || !particleSystem.hasAnalysisResults(viewerState.bondOrderScatterYAxisOrder))
    {
        ImGui::TextDisabled("Bond-order values are not available yet.");
    }
    else
    {
        const bool usePcaMode = bondOrderScatterModeUsesPca(viewerState.bondOrderScatterMode);
        const bool useAveragedValues =
            bondOrderScatterModeUsesAveragedValues(viewerState.bondOrderScatterMode);
        BondOrderScatterData &scatterData =
            getBondOrderScatterData(particleSystem, viewerState);
        const std::string xAxisLabel =
            usePcaMode ? "PC1"
                       : bondOrderAxisLabel(isTwoDimensional,
                                            viewerState.bondOrderScatterXAxisOrder,
                                            useAveragedValues);
        const std::string yAxisLabel =
            usePcaMode ? "PC2"
                       : bondOrderAxisLabel(isTwoDimensional,
                                            viewerState.bondOrderScatterYAxisOrder,
                                            useAveragedValues);
        ImGui::Text("Particles plotted: %zu", scatterData.xValues.size());
        if (usePcaMode)
        {
            const std::string pcaSourceLabel =
                bondOrderPcaSourceLabel(isTwoDimensional, useAveragedValues);
            ImGui::TextDisabled("%s", pcaSourceLabel.c_str());
        }
        else
        {
            ImGui::TextDisabled("Raw-axis mode uses each particle's base color.");
        }
        ImGui::TextDisabled(
            "Left-drag to select points; hold Shift to add, right-drag to pan.");

        const float plotWidth = bx::max(1.0f, ImGui::GetContentRegionAvail().x - 6.0f);
        BondOrderScatterInteractionState &interaction =
            viewerState.bondOrderScatterInteraction;
        if (ImPlot::GetCurrentContext() != nullptr
            && ImPlot::BeginPlot("##BondOrderScatterPlot",
                                 ImVec2(plotWidth, 220.0f),
                                 ImPlotFlags_NoLegend | ImPlotFlags_NoBoxSelect))
        {
            ImPlot::SetupAxes(xAxisLabel.c_str(), yAxisLabel.c_str(),
                              ImPlotAxisFlags_AutoFit,
                              ImPlotAxisFlags_AutoFit);
            const size_t scatterPointCount = scatterData.xValues.size();
            const bool useLightweightMarkers = scatterPointCount > 10000u;
            ImPlotSpec scatterSpec;
            scatterSpec.Marker = useLightweightMarkers ? ImPlotMarker_Square
                                                       : ImPlotMarker_Circle;
            scatterSpec.MarkerSize = 1.0f;
            scatterSpec.LineWeight = 0.0f;
            scatterSpec.MarkerLineColors = scatterData.pointColors.data();
            scatterSpec.MarkerFillColors = scatterData.pointColors.data();
            ImPlot::PlotScatter("Particles",
                                scatterData.xValues.data(),
                                scatterData.yValues.data(),
                                static_cast<int>(scatterPointCount),
                                scatterSpec);

            const ImVec2 plotPos = ImPlot::GetPlotPos();
            const ImVec2 plotSize = ImPlot::GetPlotSize();
            const ImVec2 plotMin = plotPos;
            const ImVec2 plotMax = ImVec2(plotPos.x + plotSize.x,
                                          plotPos.y + plotSize.y);
            const auto clampToPlotRect = [&](const ImVec2 &point) {
                return ImVec2(std::clamp(point.x, plotMin.x, plotMax.x),
                              std::clamp(point.y, plotMin.y, plotMax.y));
            };

            if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const ImVec2 clampedMousePos = clampToPlotRect(ImGui::GetIO().MousePos);
                interaction.dragActive = true;
                interaction.dragStartX = clampedMousePos.x;
                interaction.dragStartY = clampedMousePos.y;
            }

            if (interaction.dragActive)
            {
                const ImVec2 dragStart = {interaction.dragStartX, interaction.dragStartY};
                const ImVec2 dragCurrent = clampToPlotRect(ImGui::GetIO().MousePos);
                const float dragDeltaX = dragCurrent.x - dragStart.x;
                const float dragDeltaY = dragCurrent.y - dragStart.y;
                const float dragDistanceSquared =
                    dragDeltaX * dragDeltaX + dragDeltaY * dragDeltaY;

                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    if (dragDistanceSquared > 16.0f)
                    {
                        ImDrawList *drawList = ImGui::GetWindowDrawList();
                        const ImVec2 rectMin = ImVec2(std::min(dragStart.x, dragCurrent.x),
                                                      std::min(dragStart.y, dragCurrent.y));
                        const ImVec2 rectMax = ImVec2(std::max(dragStart.x, dragCurrent.x),
                                                      std::max(dragStart.y, dragCurrent.y));
                        drawList->AddRectFilled(rectMin, rectMax,
                                                IM_COL32(80, 160, 255, 40));
                        drawList->AddRect(rectMin, rectMax,
                                          IM_COL32(80, 160, 255, 220));
                    }
                }
                else
                {
                    if (dragDistanceSquared > 16.0f)
                    {
                        const ImPlotPoint startPlot = ImPlot::PixelsToPlot(dragStart);
                        const ImPlotPoint endPlot = ImPlot::PixelsToPlot(dragCurrent);
                        const double minX = std::min(startPlot.x, endPlot.x);
                        const double maxX = std::max(startPlot.x, endPlot.x);
                        const double minY = std::min(startPlot.y, endPlot.y);
                        const double maxY = std::max(startPlot.y, endPlot.y);
                        const bool addToSelection = ImGui::GetIO().KeyShift;

                        if (!addToSelection)
                        {
                            viewerState.selectedIds.clear();
                        }

                        for (size_t pointIndex = 0u;
                             pointIndex < scatterData.particleIds.size();
                             ++pointIndex)
                        {
                            const double xValue = scatterData.xValues[pointIndex];
                            const double yValue = scatterData.yValues[pointIndex];
                            if (xValue >= minX && xValue <= maxX
                                && yValue >= minY && yValue <= maxY)
                            {
                                viewerState.selectedIds.insert(scatterData.particleIds[pointIndex]);
                            }
                        }

                        markPickDirty = true;
                    }

                    interaction.dragActive = false;
                }
            }
            ImPlot::EndPlot();
        }
        else
        {
            interaction.dragActive = false;
            ImGui::TextDisabled("Scatter plotting is currently unavailable.");
        }

        if (!usePcaMode)
        {
            int scatterXAxisIndex =
                int(viewerState.bondOrderScatterXAxisOrder - minimumBondOrientationalOrder);
            int scatterYAxisIndex =
                int(viewerState.bondOrderScatterYAxisOrder - minimumBondOrientationalOrder);
            if (ImGui::Combo("X-axis l##BondOrderScatter",
                             &scatterXAxisIndex,
                             isTwoDimensional ? kBondOrientationalOrderLabels2D.data()
                                              : kBondOrientationalOrderLabels3D.data(),
                             isTwoDimensional
                                 ? static_cast<int>(kBondOrientationalOrderLabels2D.size())
                                 : static_cast<int>(kBondOrientationalOrderLabels3D.size())))
            {
                viewerState.bondOrderScatterXAxisOrder =
                    static_cast<uint8_t>(scatterXAxisIndex + minimumBondOrientationalOrder);
            }
            if (ImGui::Combo("Y-axis l##BondOrderScatter",
                             &scatterYAxisIndex,
                             isTwoDimensional ? kBondOrientationalOrderLabels2D.data()
                                              : kBondOrientationalOrderLabels3D.data(),
                             isTwoDimensional
                                 ? static_cast<int>(kBondOrientationalOrderLabels2D.size())
                                 : static_cast<int>(kBondOrientationalOrderLabels3D.size())))
            {
                viewerState.bondOrderScatterYAxisOrder =
                    static_cast<uint8_t>(scatterYAxisIndex + minimumBondOrientationalOrder);
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Species included in scatter/PCA");
    for (uint8_t typeIndex = 0u;
         typeIndex <= viewerState.maxSeenParticleTypeIndex;
         ++typeIndex)
    {
        bool includeSpecies = viewerState.bondOrderScatterTypeEnabled[typeIndex];
        const char typeLabel[] = {
            static_cast<char>('a' + bx::min<uint8_t>(typeIndex, 25u)),
            '\0',
        };
        const std::string checkboxLabel = std::string(typeLabel)
                                          + "##BondOrderScatterSpecies"
                                          + std::to_string(typeIndex);
        if (ImGui::Checkbox(checkboxLabel.c_str(), &includeSpecies))
        {
            viewerState.bondOrderScatterTypeEnabled[typeIndex] = includeSpecies;
            viewerState.bondOrderScatterCache.valid = false;
        }

        const bool continueSameLine = typeIndex < viewerState.maxSeenParticleTypeIndex
                                      && ((typeIndex + 1u) % 6u) != 0u;
        if (continueSameLine)
        {
            ImGui::SameLine();
        }
    }
    ImGui::TextDisabled(
        "These checkboxes affect both the scatter plot and the PCA input set.");
}

float sizeDistributionValue(const Particle &particle,
                            TrajectoryReader::FileType particleFileType)
{
    if (particleFileType == TrajectoryReader::FileType::Cube)
    {
        return particle.sizeParams[0];
    }

    return 2.0f * particle.sizeParams[0];
}

const char *sizeDistributionQuantityLabel(TrajectoryReader::FileType particleFileType)
{
    return particleFileType == TrajectoryReader::FileType::Cube
               ? "Edge length"
               : "Diameter";
}

SizeDistributionData buildSizeDistributionData(const ParticleSystem &particleSystem,
                                               TrajectoryReader::FileType particleFileType,
                                               bool visibleOnly,
                                               uint16_t requestedBinCount)
{
    SizeDistributionData data;

    std::vector<float> values;
    values.reserve(particleSystem.particles().size());
    for (const Particle &particle : particleSystem.particles())
    {
        if (visibleOnly && !particle.visible)
        {
            continue;
        }
        values.push_back(sizeDistributionValue(particle, particleFileType));
    }

    data.sampleCount = values.size();
    if (values.empty())
    {
        return data;
    }

    data.minValue = *std::min_element(values.begin(), values.end());
    data.maxValue = *std::max_element(values.begin(), values.end());

    double sum = 0.0;
    for (float value : values)
    {
        sum += static_cast<double>(value);
    }
    data.meanValue = static_cast<float>(sum / static_cast<double>(values.size()));

    double squaredDifferenceSum = 0.0;
    for (float value : values)
    {
        const double delta = static_cast<double>(value) - static_cast<double>(data.meanValue);
        squaredDifferenceSum += delta * delta;
    }
    data.standardDeviation = static_cast<float>(
        std::sqrt(squaredDifferenceSum / static_cast<double>(values.size())));

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
    for (float value : values)
    {
        int binIndex = static_cast<int>((value - data.plotMinValue) * inverseBinWidth);
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
    case TrajectoryReader::FileType::BondedSphere:
        return "bonded sphere";
    case TrajectoryReader::FileType::Rod:
        return "rod";
    case TrajectoryReader::FileType::Cube:
        return "cube";
    case TrajectoryReader::FileType::Polygon:
        return "polygon";
    case TrajectoryReader::FileType::Voronoi:
        return "voronoi polyhedron";
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
    cvt::log::info() << "Visible particles: " << countVisibleParticles(particleSystem)
                     << std::endl;
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
                        const SimulationBox &simulationBox,
                        const std::string &loadedPath,
                        size_t currentFrame, size_t totalFrames,
                        uint16_t windowWidth, uint16_t windowHeight,
                        float cutPlaneMinSceneZ, float cutPlaneMaxSceneZ)
{
    const bool wasStructureFactorPanelOpen = viewerState.structureFactorPanelOpen;
    const bool wasNeighborAnalysisPanelOpen = viewerState.neighborAnalysisPanelOpen;
    viewerState.bondDiagramRenderRequested = false;
    viewerState.structureFactorPanelOpen = false;

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
    if (!viewerState.fileOpenStatusMessage.empty())
    {
        ImGui::TextWrapped("%s", viewerState.fileOpenStatusMessage.c_str());
    }
    ImGui::Text("Particle type: %s", particleTypeName(particleFileType));
    ImGui::Text("Visible: %zu", countVisibleParticles(particleSystem));
    ImGui::Text("Selected: %zu", viewerState.selectedIds.size());
    {
        const double phi = computePackingFraction(particleSystem, simulationBox,
                                                   particleFileType,
                                                   viewerState.fileDimensionality);
        if (phi >= 0.0)
        {
            ImGui::Text("Packing fraction: %.4f", phi);
        }
    }

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

    ImGui::SetNextItemOpen(viewerState.basicControlsDefaultOpen, ImGuiCond_Once);
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

        const bool supportsResolutionAdjustment =
            particleFileType != TrajectoryReader::FileType::Polygon;
        ImGui::BeginDisabled(!supportsResolutionAdjustment);
        int particleResolution = int(viewerState.particleResolution);
        if (ImGui::SliderInt("Particle resolution (+/-)", &particleResolution, 4, 64))
        {
            viewerState.particleResolution =
                static_cast<uint16_t>(std::clamp(particleResolution, 4, 64));
        }
        ImGui::EndDisabled();

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
            viewerState.frankKasperViewModeEnabled = false;
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

        viewerState.neighborAnalysisPanelOpen = ImGui::CollapsingHeader("Neighbor analysis");
        if (viewerState.neighborAnalysisPanelOpen)
        {
            const bool neighborAnalysisPanelJustOpened = !wasNeighborAnalysisPanelOpen;
            if (neighborAnalysisPanelJustOpened)
            {
                viewerState.autoFindNeighbors = true;
                viewerState.pendingFindNeighbors = true;
            }

            const bool isTwoDimensional =
                viewerState.fileDimensionality == TrajectoryReader::Dimensionality::TwoDimensional;
            const bool uses2DBondOrientationalColor =
                viewerState.analysisColorMode
                    == AnalysisColorMode::BondOrientationalOrderMagnitude
                || viewerState.analysisColorMode
                    == AnalysisColorMode::BondOrientationalOrderPhase;
            const bool uses3DBondOrientationalColor =
                viewerState.analysisColorMode
                    == AnalysisColorMode::BondOrientationalQLMagnitude
                || viewerState.analysisColorMode
                    == AnalysisColorMode::BondOrientationalQBarLMagnitude;
            if ((isTwoDimensional && uses3DBondOrientationalColor)
                || (!isTwoDimensional && uses2DBondOrientationalColor))
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
            if (isTwoDimensional)
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
                    "Bond-orientational order |q_l|",
                    "Averaged bond-orientational order |qbar_l|",
                };
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
                isTwoDimensional ? uses2DBondOrientationalColor
                                 : uses3DBondOrientationalColor;
            const uint8_t minimumBondOrientationalOrder = 2u;
            const uint8_t maximumBondOrientationalOrder = isTwoDimensional ? 6u : 8u;
            ImGui::BeginDisabled(!viewerState.neighborAnalysisValid
                                 || !usesBondOrientationalColor);
            int symmetryOrderIndex =
                int(std::clamp<uint8_t>(viewerState.bondOrientationalOrder,
                                        minimumBondOrientationalOrder,
                                        maximumBondOrientationalOrder)
                    - minimumBondOrientationalOrder);
            if (ImGui::Combo(isTwoDimensional ? "Bond-order symmetry"
                                           : "Spherical-harmonic degree l",
                             &symmetryOrderIndex,
                             isTwoDimensional ? kBondOrientationalOrderLabels2D.data()
                                              : kBondOrientationalOrderLabels3D.data(),
                             isTwoDimensional
                                 ? static_cast<int>(kBondOrientationalOrderLabels2D.size())
                                 : static_cast<int>(kBondOrientationalOrderLabels3D.size())))
            {
                viewerState.bondOrientationalOrder =
                    static_cast<uint8_t>(symmetryOrderIndex + minimumBondOrientationalOrder);
                viewerState.pendingRefreshAnalysisResults = viewerState.neighborAnalysisValid;
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!viewerState.neighborAnalysisValid
                                 || viewerState.selectedIds.empty());
            if (ImGui::Button("Print selected bond-order values"))
            {
                viewerState.pendingDescribeSelectedBondOrder = true;
            }
            ImGui::EndDisabled();

            if (ImGui::Button("Switch to FK view mode"))
            {
                viewerState.pendingActivateFrankKasperView = true;
                if (!viewerState.neighborAnalysisValid)
                {
                    viewerState.pendingFindNeighbors = true;
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!viewerState.frankKasperViewActivatedOnce);
            if (ImGui::Checkbox("Hide unbonded (!12 neighbors)",
                                &viewerState.hideNonFrankKasperUnbonded))
            {
                viewerState.pendingToggleFrankKasperUnbondedVisibility = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!viewerState.frankKasperViewActivatedOnce);
            if (ImGui::Checkbox("Auto-recalculate FK", &viewerState.frankKasperAutoRecalculate)
                && viewerState.frankKasperAutoRecalculate)
            {
                viewerState.pendingRecalculateFrankKasperBonds = true;
            }
            ImGui::EndDisabled();

            drawBondOrderScatterPanel(viewerState, particleSystem,
                                      isTwoDimensional,
                                      minimumBondOrientationalOrder,
                                      maximumBondOrientationalOrder,
                                      markPickDirty);

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
        viewerState.structureFactorPanelOpen = ImGui::CollapsingHeader("Structure factor");
        if (viewerState.structureFactorPanelOpen)
        {
            const bool structureFactorPanelJustOpened = !wasStructureFactorPanelOpen;
            if (structureFactorPanelJustOpened && viewerState.structureFactorDirty
                && structureFactorAllowsAutomaticUpdates(viewerState.structureFactorUpdateMode))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorPendingCompute = true;
            }

            const size_t structureFactorParticleCount = particleSystem.particles().size();
            const bool isLargeStructureFactorSystem =
                structureFactorParticleCount > kLargeStructureFactorParticleThreshold;

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

            if (ImGui::Button("Compute structure factor"))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                markStructureFactorDirty(viewerState);
                viewerState.structureFactorPendingCompute = true;
            }
            ImGui::SameLine();
            static const char *kStructureFactorUpdateModeLabelsSmall[] = {
                "update when stationary",
                "update always",
                "manual only",
            };
            static const StructureFactorUpdateMode kStructureFactorUpdateModesSmall[] = {
                StructureFactorUpdateMode::UpdateWhenStationary,
                StructureFactorUpdateMode::UpdateAlways,
                StructureFactorUpdateMode::ManualOnly,
            };
            static const char *kStructureFactorUpdateModeLabelsLarge[] = {
                "update when stationary",
                "manual only",
            };
            static const StructureFactorUpdateMode kStructureFactorUpdateModesLarge[] = {
                StructureFactorUpdateMode::UpdateWhenStationary,
                StructureFactorUpdateMode::ManualOnly,
            };

            const char *const *updateModeLabels = isLargeStructureFactorSystem
                                                       ? kStructureFactorUpdateModeLabelsLarge
                                                       : kStructureFactorUpdateModeLabelsSmall;
            const StructureFactorUpdateMode *updateModes = isLargeStructureFactorSystem
                                                               ? kStructureFactorUpdateModesLarge
                                                               : kStructureFactorUpdateModesSmall;
            const int updateModeCount = isLargeStructureFactorSystem ? 2 : 3;
            if (isLargeStructureFactorSystem
                && viewerState.structureFactorUpdateMode == StructureFactorUpdateMode::UpdateAlways)
            {
                viewerState.structureFactorUpdateMode =
                    StructureFactorUpdateMode::UpdateWhenStationary;
            }

            int updateModeIndex = 0;
            for (int modeIndex = 0; modeIndex < updateModeCount; ++modeIndex)
            {
                if (updateModes[modeIndex] == viewerState.structureFactorUpdateMode)
                {
                    updateModeIndex = modeIndex;
                    break;
                }
            }
            if (ImGui::Combo("Update mode##StructureFactor",
                             &updateModeIndex,
                             updateModeLabels,
                             updateModeCount))
            {
                viewerState.structureFactorUpdateMode = updateModes[updateModeIndex];
                if (!structureFactorAllowsInteractionUpdates(viewerState.structureFactorUpdateMode))
                {
                    viewerState.structureFactorInteractionLowResActive = false;
                }
                if (viewerState.structureFactorDirty
                    && structureFactorAllowsAutomaticUpdates(viewerState.structureFactorUpdateMode))
                {
                    viewerState.structureFactorPendingCompute = true;
                }
            }
            if (isLargeStructureFactorSystem)
            {
                ImGui::TextWrapped(
                    "For systems larger than %zu particles, structure-factor "
                    "calculations may be slow or buggy.",
                    kLargeStructureFactorParticleThreshold);
                ImGui::TextDisabled("Large systems are automatically batched across frames.");
            }

            if (viewerState.structureFactorBatchState.active)
            {
                const uint32_t totalModes =
                    static_cast<uint32_t>(viewerState.structureFactorBatchState.uniqueModes.size());
                const uint32_t completedModes =
                    viewerState.structureFactorBatchState.nextModeIndex;
                ImGui::Text("Batch progress: %u / %u", completedModes, totalModes);
            }
            else if (structureFactorResources != nullptr
                     && structureFactorResources->gpuBatchActive)
            {
                ImGui::Text("GPU batch progress: %u / %u rows",
                            unsigned(structureFactorResources->gpuBatchNextRow),
                            unsigned(viewerState.structureFactorImageSize));
            }

            bool useGpu = viewerState.structureFactorUseGpu;
            if (ImGui::Checkbox("Use GPU##StructureFactor", &useGpu))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorUseGpu = useGpu;
                markStructureFactorDirty(viewerState);
                if (structureFactorAllowsAutomaticUpdates(viewerState.structureFactorUpdateMode))
                {
                    viewerState.structureFactorPendingCompute = true;
                }
            }

            bool specifyModeCount = viewerState.structureFactorSpecifyModeCount;
            if (ImGui::Checkbox("Specify number of modes", &specifyModeCount))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorSpecifyModeCount = specifyModeCount;
                markStructureFactorDirty(viewerState);
            }

            if (viewerState.structureFactorSpecifyModeCount)
            {
                int maxMode = viewerState.structureFactorMaxModeX;
                if (ImGui::SliderInt("Max |k-mode|", &maxMode, 4, 128))
                {
                    viewerState.structureFactorInteractionLowResActive = false;
                    viewerState.structureFactorMaxModeX = maxMode;
                    viewerState.structureFactorMaxModeY = maxMode;
                    markStructureFactorDirty(viewerState);
                }
            }
            else
            {
                float maxKTimesSigma = viewerState.structureFactorMaxKTimesSigma;
                if (ImGui::SliderFloat("Max k * sigma", &maxKTimesSigma, 1.0f, 64.0f, "%.1f"))
                {
                    viewerState.structureFactorInteractionLowResActive = false;
                    viewerState.structureFactorMaxKTimesSigma = std::clamp(maxKTimesSigma,
                                                                           1.0f, 64.0f);
                    markStructureFactorDirty(viewerState);
                }
                ImGui::TextDisabled("Uses the input file's native length unit (sigma = 1).");
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
            if (ImGui::IsItemDeactivatedAfterEdit()
                && structureFactorAllowsAutomaticUpdates(viewerState.structureFactorUpdateMode))
            {
                viewerState.structureFactorPendingCompute = true;
            }

            float colorRangeMin = viewerState.structureFactorColorRangeMin;
            if (ImGui::SliderFloat("Minimum intensity", &colorRangeMin, 0.0f, 0.99f, "%.2f"))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorColorRangeMin =
                    std::clamp(colorRangeMin, 0.0f, viewerState.structureFactorColorRangeMax - 0.01f);
                markStructureFactorDirty(viewerState);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()
                && structureFactorAllowsAutomaticUpdates(viewerState.structureFactorUpdateMode))
            {
                viewerState.structureFactorPendingCompute = true;
            }

            float colorRangeMax = viewerState.structureFactorColorRangeMax;
            if (ImGui::SliderFloat("Maximum intensity", &colorRangeMax, 0.01f, 1.0f, "%.2f"))
            {
                viewerState.structureFactorInteractionLowResActive = false;
                viewerState.structureFactorColorRangeMax =
                    std::clamp(colorRangeMax, viewerState.structureFactorColorRangeMin + 0.01f, 1.0f);
                markStructureFactorDirty(viewerState);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()
                && structureFactorAllowsAutomaticUpdates(viewerState.structureFactorUpdateMode))
            {
                viewerState.structureFactorPendingCompute = true;
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

            if (viewerState.maxSeenParticleTypeIndex > 0u)
            {
                ImGui::TextUnformatted("Species included in S(k)");
                for (uint8_t typeIndex = 0u;
                     typeIndex <= viewerState.maxSeenParticleTypeIndex;
                     ++typeIndex)
                {
                    bool included = viewerState.structureFactorIncludedSpecies[typeIndex];
                    const char typeLabel[] = {
                        static_cast<char>('a' + bx::min<uint8_t>(typeIndex, 25u)),
                        '\0',
                    };
                    const std::string checkboxLabel = std::string(typeLabel)
                                                      + "##SfkSpecies"
                                                      + std::to_string(typeIndex);
                    if (ImGui::Checkbox(checkboxLabel.c_str(), &included))
                    {
                        viewerState.structureFactorIncludedSpecies[typeIndex] = included;
                        viewerState.structureFactorInteractionLowResActive = false;
                        ++viewerState.structureFactorDataRevision;
                        if (viewerState.structureFactorDataRevision == 0u)
                        {
                            viewerState.structureFactorDataRevision = 1u;
                        }
                        markStructureFactorDirty(viewerState);
                        if (structureFactorAllowsAutomaticUpdates(
                                viewerState.structureFactorUpdateMode))
                        {
                            viewerState.structureFactorPendingCompute = true;
                        }
                    }
                    const bool continueSameLine =
                        typeIndex < viewerState.maxSeenParticleTypeIndex
                        && ((typeIndex + 1u) % 6u) != 0u;
                    if (continueSameLine)
                    {
                        ImGui::SameLine();
                    }
                }
            }

            ImGui::Text("Status: %s",
                        viewerState.structureFactorDirty ? "stale - recompute needed"
                                                         : "up to date");

            if (structureFactorResources != nullptr && !structureFactorResources->statusText.empty())
            {
                ImGui::TextWrapped("%s", structureFactorResources->statusText.c_str());
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
                                          particleFileType,
                                          viewerState.sizeDistributionUseVisibleOnly,
                                          viewerState.sizeDistributionBinCount);
            if (sizeDistribution.sampleCount == 0u)
            {
                ImGui::TextDisabled("No particles available for the current filter.");
            }
            else
            {
                const char *sizeQuantityLabel =
                    sizeDistributionQuantityLabel(particleFileType);
                ImGui::Text("Count: %zu", sizeDistribution.sampleCount);
                ImGui::Text("Mean %s: %.4f", sizeQuantityLabel, sizeDistribution.meanValue);
                ImGui::Text("SD: %.4f", sizeDistribution.standardDeviation);
                ImGui::Text("Polydispersity: %.4f",
                            sizeDistribution.meanValue > 1.0e-6f
                                ? (sizeDistribution.standardDeviation / sizeDistribution.meanValue)
                                : 0.0f);
                ImGui::Text("Range: [%.4f, %.4f]", sizeDistribution.minValue,
                            sizeDistribution.maxValue);

                const float plotWidth =
                    bx::max(1.0f, ImGui::GetContentRegionAvail().x - 6.0f);
                if (ImPlot::GetCurrentContext() != nullptr
                    && ImPlot::BeginPlot("##SizeDistributionPlot",
                                         ImVec2(plotWidth, 180.0f),
                                         ImPlotFlags_NoLegend))
                {
                    ImPlot::SetupAxes(sizeQuantityLabel, "Count",
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
                ImGui::TextDisabled("Histogram uses the current particle size measure for this shape (diameter for spheres/disks, edge length for cubes).");
            }
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Keyboard commands"))
    {
        if (ImGui::BeginTabBar("##KeyboardCommandsTabs"))
        {
            if (ImGui::BeginTabItem("Reference"))
            {
                ImGui::TextUnformatted("Global");
                ImGui::BulletText("F1: Toggle stats");
                ImGui::BulletText("F2 or Shift+U: Toggle controls panel");
                ImGui::BulletText("Q or Esc: Quit");

                ImGui::Spacing();
                ImGui::TextUnformatted("View and timeline");
                ImGui::BulletText("Enter or Numpad Enter: Reset view rotation");
                ImGui::BulletText("Ctrl + Arrow keys: Rotate view by 90 degrees");
                ImGui::BulletText("Left/Right: Previous/next frame");
                ImGui::BulletText("Shift + Left/Right: First/last frame");
                ImGui::BulletText("A: Align view to selection");

                ImGui::Spacing();
                ImGui::TextUnformatted("Rendering and modes");
                ImGui::BulletText("B: Toggle simulation box");
                ImGui::BulletText("W: Toggle wrap particles to box");
                ImGui::BulletText("M: Cycle color mode");
                ImGui::BulletText("Shift+M: Toggle mobility mode");
                ImGui::BulletText("Shift+B: Toggle bond mode");
                ImGui::BulletText("Shift+N: Toggle neighbor mode");
                ImGui::BulletText("S: Cycle lighting level");
                ImGui::BulletText("/ or Numpad Divide: Decrease particle size scale");
                ImGui::BulletText("Shift+8 or Numpad Multiply: Increase particle size scale");
                ImGui::BulletText("Shift+= or Numpad Add: Increase particle resolution");
                ImGui::BulletText("- or Numpad Subtract: Decrease particle resolution");

                ImGui::Spacing();
                ImGui::TextUnformatted("Selection and analysis");
                ImGui::BulletText("H: Hide selected");
                ImGui::BulletText("Shift+H: Reveal all particles");
                ImGui::BulletText("U: Clear selection");
                ImGui::BulletText("I: Invert selection");
                ImGui::BulletText("D: Print selected IDs");
                ImGui::BulletText("V: Print visible particle count");
                ImGui::BulletText("E: Run overlap check");
                ImGui::BulletText("Ctrl+B: Select particles bonded to selection");

                ImGui::Spacing();
                ImGui::TextUnformatted("Species visibility and cut plane");
                ImGui::BulletText("1-0 (or Numpad 1-0): Toggle species visibility");
                ImGui::BulletText("Shift+1-0: Show only that species");
                ImGui::BulletText(".: Step cut plane");
                ImGui::BulletText(",: Step cut plane (opposite direction)");
                ImGui::BulletText("Shift+.: Enable cut plane");
                ImGui::BulletText("Shift+,: Disable cut plane");

                ImGui::Spacing();
                ImGui::TextUnformatted("Capture");
                ImGui::BulletText("P: Screenshot");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    if (markPickDirty)
    {
        markPickBufferDirty(viewerState);
    }

    ImGui::End();
}