#pragma once

// The degree/radian conversions, in one place. Three translation units carried
// their own identical copies before, which is three chances for one of them to
// be written with a different number of digits than the others - and the whole
// mod's job is to agree with itself about what an angle means.

namespace headtracking {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

}  // namespace headtracking
