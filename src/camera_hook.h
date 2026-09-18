// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace tt3_ht {

// Places the five detours at the RVAs the active build profile pins:
//
// - Renderer::DoUpdate, only to know which renderer is building its views.
// - ViewContext::InitFromCamera. After the engine has built the main view from
//   the camera, the view matrix, the eye and the frustum are rebuilt from a
//   head-tracked copy of the camera's world transform. The camera itself, and
//   everything the game reads from it, is never written.
// - The GameFSM update, to read each flow machine's state for the gate.
// - The rider's helmet placement and Actor::SetWorldMatrix, to carry the
//   helmet shell with the head in the helmet view.
//
// Every exported target is checked against the export table first. Returns
// false, with nothing left patched, when a target disagrees or MinHook fails.
bool InstallCameraHook();

}  // namespace tt3_ht
