#pragma once
#include <stdint.h>

// Pure, host-testable Night Mode arithmetic (HH:MM values converted to minutes).
// Start is inclusive, end is exclusive. Settings validation forbids equal times.
constexpr bool inTimeWindowMinutes(uint16_t current, uint16_t start, uint16_t end) {
  return start < end ? (current >= start && current < end)
                     : (current >= start || current < end);
}

// percent=0 leaves brightness unchanged; percent=100 turns indicators off.
// Round to the nearest 8-bit PWM duty level instead of truncating every time.
constexpr uint8_t applyBrightnessReduction(uint8_t base, uint8_t percent) {
  return static_cast<uint8_t>((static_cast<uint16_t>(base) * (100U - percent) + 50U) / 100U);
}
