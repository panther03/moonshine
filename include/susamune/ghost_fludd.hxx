#pragma once
#include "susamune/ghost_fludd.h"
#include "JSystem/JGeometry/JGMVec.hxx"
namespace JDrama { class TGraphics; }

namespace GhostFludd {
void beginFrame();
void capture(SusamuneGhostFluddSample &sample);
void direction(const SusamuneGhostFluddSample &sample, TVec3f &out);
void draw(JDrama::TGraphics *graphics);
}
