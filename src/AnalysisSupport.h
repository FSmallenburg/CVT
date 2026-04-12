#pragma once

#include "SimulationBox.h"
#include "ViewerSupport.h"

#include <unordered_set>

void applyColorMode(ParticleSystem &particleSystem,
                    ColorMode colorMode,
                    bool supportsOrientationMode,
                    bool uniformUsesOrientation,
                    ParticleColorStatsCache &statsCache);

void computeAnalysisResults(ViewerState &viewerState,
                            ParticleSystem &particleSystem);

void applyAnalysisColorMode(ParticleSystem &particleSystem,
                            const ViewerState &viewerState);

void applyAnalysisColorMode(ParticleSystem &targetParticleSystem,
                            const ParticleSystem &analysisSourceParticleSystem,
                            const ViewerState &viewerState);

void printSelectedBondOrderParameters(const ViewerState &viewerState,
                                      const ParticleSystem &particleSystem);

void invalidateNeighborAnalysis(ViewerState &viewerState,
                                ParticleSystem &particleSystem);

void findNearestNeighbors(const ViewerState &viewerState,
                          const SimulationBox &simulationBox,
                          ParticleSystem &particleSystem);
