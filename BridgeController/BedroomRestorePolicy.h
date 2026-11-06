#pragma once
// Pure C++ logic used by the on-device restore engine and native unit tests.
// No Arduino or network dependencies.
enum BedroomRestoreDecision {
  BEDROOM_ALREADY_RESTORED,
  BEDROOM_RESTORE_ALLOWED,
  BEDROOM_RESTORE_BLOCKED
};
constexpr unsigned int BEDROOM_MAX_RESTORE_ATTEMPTS = 3;
inline BedroomRestoreDecision bedroomRestorationDecision(
    bool matchesOriginal, bool hasExpected, bool matchesLastCommand, bool explicitForce) {
  if (matchesOriginal) return BEDROOM_ALREADY_RESTORED;
  if (explicitForce || (hasExpected && matchesLastCommand))
    return BEDROOM_RESTORE_ALLOWED;
  return BEDROOM_RESTORE_BLOCKED;
}
