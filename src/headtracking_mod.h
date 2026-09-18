// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "camera_transform.h"

namespace tt3_ht {

void Initialize();

// The head-tracked version of the player camera's clean world transform for
// the frame being built, or false when the view should stay exactly as the
// engine made it.
//
// The first call in a frame advances the tracking pipeline and evaluates the
// gate against `graphics_camera`. Every later call in the same frame reuses
// that pose and that decision, so the helmet the game places early in the
// frame and the view built later in it are moved by the same head pose. A later
// call for a different camera than the one the decision was made for is
// refused.
bool TrackedCameraForFrame(std::uintptr_t graphics_camera,
                           const float clean[kMatrix34Floats],
                           float tracked[kMatrix34Floats]);

// Marks the end of the frame's view building, so the next call to
// TrackedCameraForFrame advances the pipeline again.
void EndFrame();

}  // namespace tt3_ht
