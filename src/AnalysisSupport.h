#pragma once

#include "SimulationBox.h"
#include "ViewerSupport.h"

#include <unordered_set>

/// Sets per-particle colors on @p particleSystem according to @p colorMode.
///
/// @p statsCache is updated in-place when the mode requires per-frame
/// statistics (e.g. ParticleSize). @p supportsOrientationMode and
/// @p uniformUsesOrientation control whether orientation data drives the
/// Orientation/Uniform modes for the current particle type.
void applyColorMode(ParticleSystem &particleSystem,
                    ColorMode colorMode,
                    bool supportsOrientationMode,
                    bool uniformUsesOrientation,
                    ParticleColorStatsCache &statsCache,
                    const ViewerState &viewerState);

/// Runs the full neighbor + bond-orientational-order analysis pipeline and
/// stores the results on @p particleSystem. Also marks dependent render
/// systems dirty in @p viewerState. Must be called after findNearestNeighbors().
void computeAnalysisResults(ViewerState &viewerState,
                            ParticleSystem &particleSystem);

/// Recolors @p particleSystem using the analysis color mode stored in
/// @p viewerState. The analysis results are read from the same particle system.
void applyAnalysisColorMode(ParticleSystem &particleSystem,
                            const ViewerState &viewerState);

/// Recolors @p targetParticleSystem using analysis results sourced from
/// @p analysisSourceParticleSystem. Used when a second particle system
/// (e.g. a bond system) should mirror the analysis coloring of the primary one.
void applyAnalysisColorMode(ParticleSystem &targetParticleSystem,
                            const ParticleSystem &analysisSourceParticleSystem,
                            const ViewerState &viewerState);

/// Prints bond-order parameters (Q4, Q6, …) for all selected particles to the
/// log, preceded by a summary line.
void printSelectedBondOrderParameters(const ViewerState &viewerState,
                                      const ParticleSystem &particleSystem);

/// Clears the cached neighbor lists and analysis results and marks dependent
/// state dirty so they are recomputed on the next active frame.
void invalidateNeighborAnalysis(ViewerState &viewerState,
                                ParticleSystem &particleSystem);

/// Computes the nearest-neighbor list for all particles using the cutoff
/// factor from @p viewerState and periodic boundary conditions from
/// @p simulationBox. Results are stored on @p particleSystem.
void findNearestNeighbors(const ViewerState &viewerState,
                          const SimulationBox &simulationBox,
                          ParticleSystem &particleSystem);

/// Computes Frank-Kasper bond assignments from @p particleSystem and stores
/// the results on @p targetParticleSystem. The two systems may be identical or
/// different (e.g. a bond-only render system).
void calculateFrankKasperBonds(const ParticleSystem &particleSystem,
                               ParticleSystem &targetParticleSystem);

/// Computes bond assignments between neighboring pairs where both particles
/// have exactly 12 nearest neighbors and the pair shares exactly 5 common
/// neighbors, then stores the result on @p targetParticleSystem.
void calculateTwelveCoordinatedNeighborBonds(const ParticleSystem &particleSystem,
                                             ParticleSystem &targetParticleSystem);

/// Computes per-frame radial distribution function g(r) from particle centers
/// using the current RDF settings in @p viewerState and writes results back to
/// @p viewerState (overall curve + type-pair curves).
void computeRadialDistributionFunction(ViewerState &viewerState,
                                       const SimulationBox &simulationBox,
                                       const ParticleSystem &particleSystem);
