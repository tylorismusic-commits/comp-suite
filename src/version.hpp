// Comp Suite — version.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace comp {

// Keep these in step with `project(comp_suite VERSION ...)` in CMakeLists.txt.
// The CLAP descriptor and the LV2 metadata both read them from here so the two
// formats cannot advertise different versions.
constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 0;
constexpr int kVersionMicro = 0;
constexpr const char* kVersion = "1.0.0";

} // namespace comp
