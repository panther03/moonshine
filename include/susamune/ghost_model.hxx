#pragma once

#include "JSystem/JGeometry/JGMVec.hxx"

class TMarDirector;

namespace GhostModel {

void init();
void beginFrame();
void beforeStageSetup();
void onStageSetup(TMarDirector *director);
bool preserveSavestateBindings(bool (*keep)(const void *word));
void onSavestateLoaded();
bool available();
bool submitted(bool secondary = false);
bool emissionPoint(unsigned runner, unsigned nozzle, unsigned emitter, TVec3f &position);

}  // namespace GhostModel
