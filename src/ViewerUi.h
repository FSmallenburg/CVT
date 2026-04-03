#pragma once

#include "ParticleSystem.h"
#include "TrajectoryReader.h"
#include "ViewerSupport.h"

#include <cstddef>
#include <cstdint>
#include <string>

const char *particleTypeName(TrajectoryReader::FileType fileType);
const char *colorModeName(ColorMode colorMode);
float lightingScaleFromIndex(uint8_t lightingLevelIndex);
size_t countVisibleParticles(const ParticleSystem &particleSystem);
void printVisibleParticleCount(const ParticleSystem &particleSystem);
uint16_t computeUiPanelWidth(uint16_t windowWidth, bool showUi);
uint16_t computeRenderViewportWidth(uint16_t windowWidth, bool showUi);
bool isInRenderViewport(const ViewerState &viewerState, double mouseX, double mouseY);
void drawViewerControls(ViewerState &viewerState, ParticleSystem &particleSystem,
                        const BondDiagramResources *bondDiagramResources,
                        const StructureFactorResources *structureFactorResources,
                        TrajectoryReader::FileType particleFileType,
                        const std::string &loadedPath,
                        size_t currentFrame, size_t totalFrames,
                        uint16_t windowWidth, uint16_t windowHeight,
                        float cutPlaneMinSceneZ, float cutPlaneMaxSceneZ);