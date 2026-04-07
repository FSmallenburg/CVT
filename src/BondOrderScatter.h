#pragma once

#include "ViewerSupport.h"

#include <string>

bool bondOrderScatterModeUsesPca(BondOrderScatterMode scatterMode);
bool bondOrderScatterModeUsesAveragedValues(BondOrderScatterMode scatterMode);
std::string bondOrderAxisLabel(bool isTwoDimensional,
                               uint8_t order,
                               bool useAveragedValues);
std::string bondOrderPcaSourceLabel(bool isTwoDimensional,
                                    bool useAveragedValues);
BondOrderScatterData &getBondOrderScatterData(const ParticleSystem &particleSystem,
                                              ViewerState &viewerState);
