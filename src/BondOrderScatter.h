#pragma once

#include "ViewerSupport.h"

#include <string>

/// Returns true when @p scatterMode uses principal-component analysis to
/// project the bond-order vectors (as opposed to raw axis projections).
bool bondOrderScatterModeUsesPca(BondOrderScatterMode scatterMode);

/// Returns true when @p scatterMode uses the averaged (Q̄) bond-order values
/// rather than the local (Q) values.
bool bondOrderScatterModeUsesAveragedValues(BondOrderScatterMode scatterMode);

/// Returns a human-readable axis label for the bond-order scatter plot
/// (e.g. "Q4" or "q̄6") at the given @p order.
std::string bondOrderAxisLabel(bool isTwoDimensional,
                               uint8_t order,
                               bool useAveragedValues);

/// Returns the data-source description shown in the scatter-plot legend
/// (e.g. "Q PCA" or "Q̄ raw").
std::string bondOrderPcaSourceLabel(bool isTwoDimensional,
                                    bool useAveragedValues);

/// Returns (and lazily rebuilds if dirty) the cached @p BondOrderScatterData
/// stored in @p viewerState for the given @p particleSystem.
BondOrderScatterData &getBondOrderScatterData(const ParticleSystem &particleSystem,
                                              ViewerState &viewerState);
