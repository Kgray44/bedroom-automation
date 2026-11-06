#include <cassert>
#include "../BridgeController/NightLogic.h"
int main() {
  constexpr int h18=18*60, h7=7*60;
  static_assert(!inTimeWindowMinutes(17*60+59,h18,h7), "before night");
  static_assert(inTimeWindowMinutes(18*60,h18,h7), "night begins");
  static_assert(inTimeWindowMinutes(23*60+59,h18,h7), "before midnight");
  static_assert(inTimeWindowMinutes(0,h18,h7), "after midnight");
  static_assert(inTimeWindowMinutes(6*60+59,h18,h7), "night ending");
  static_assert(!inTimeWindowMinutes(7*60,h18,h7), "night ends");
  static_assert(!inTimeWindowMinutes(12*60,h18,h7), "daytime");
  static_assert(inTimeWindowMinutes(13*60,12*60,15*60), "normal same-day window");
  static_assert(!inTimeWindowMinutes(15*60,12*60,15*60), "normal window ends");
  static_assert(applyBrightnessReduction(255,0)==255, "day full brightness");
  static_assert(applyBrightnessReduction(255,50)==128, "night half brightness");
  static_assert(applyBrightnessReduction(40,50)==20, "bedroom half brightness");
  static_assert(applyBrightnessReduction(255,100)==0, "fully suppressed");
  static_assert(applyBrightnessReduction(1,50)==1, "rounding");
  assert(inTimeWindowMinutes(1,h18,h7));
}
