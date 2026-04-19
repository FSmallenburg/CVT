#pragma once

#include "ParticleSystem.h"
#include "SimulationBox.h"
#include "TrajectoryReader.h"
#include "ViewerSupport.h"

#include <cstddef>
#include <cstdint>
#include <string>

/// Returns the human-readable name for @p fileType (e.g. "Sphere", "Patchy").
const char *particleTypeName(TrajectoryReader::FileType fileType);

/// Returns the display name for @p colorMode, taking @p viewerState into
/// account for modes that vary by dimensionality (e.g. "Orientation 2D").
std::string colorModeName(ColorMode colorMode, const ViewerState &viewerState);

/// Converts a discrete lighting level index (0–29) to a linear scale factor
/// passed to the lighting shader uniform.
float lightingScaleFromIndex(uint8_t lightingLevelIndex);

/// Returns the number of particles whose visible flag is currently true.
size_t countVisibleParticles(const ParticleSystem &particleSystem);

/// Prints the visible particle count and total particle count to the log.
void printVisibleParticleCount(const ParticleSystem &particleSystem);

/// Returns the pixel width of the ImGui side panel given the total
/// @p windowWidth and whether the UI is shown.
uint16_t computeUiPanelWidth(uint16_t windowWidth, bool showUi);

/// Returns the pixel width available for 3-D rendering (window width minus
/// the UI panel width).
uint16_t computeRenderViewportWidth(uint16_t windowWidth, bool showUi);

/// Returns true when (@p mouseX, @p mouseY) falls inside the 3-D render
/// viewport (i.e. not over the UI panel).
bool isInRenderViewport(const ViewerState &viewerState, double mouseX, double mouseY);

/// Builds and submits the entire ImGui side panel for one frame.
/// All pending-action flags in @p viewerState are set directly by the
/// widgets when the user interacts with them.
void drawViewerControls(ViewerState &viewerState, ParticleSystem &particleSystem,
                        const BondDiagramResources *bondDiagramResources,
                        const StructureFactorResources *structureFactorResources,
                        TrajectoryReader::FileType particleFileType,
                        const SimulationBox &simulationBox,
                        const std::string &loadedPath,
                        size_t currentFrame, size_t totalFrames,
                        uint16_t windowWidth, uint16_t windowHeight,
                        float cutPlaneMinSceneZ, float cutPlaneMaxSceneZ);