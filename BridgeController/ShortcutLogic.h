#pragma once
#include <stdint.h>
// Pure helpers for quick host-side regression tests.
inline uint8_t dlWeekdayBit(int tmWday) { // C tm_wday: Sun=0, Mon=1
  return (tmWday>=0 && tmWday<=6) ? static_cast<uint8_t>(1U<<((tmWday+6)%7)) : 0;
}
inline int dlBrightnessPercent(int pct,int nightReduction,bool nightActive) {
  if(pct<0)pct=0;
  if(pct>100)pct=100;
  if(nightReduction<0)nightReduction=0;
  if(nightReduction>100)nightReduction=100;
  return nightActive ? (pct*(100-nightReduction)+50)/100 : pct;
}
inline bool dlExpectedReadback(uint32_t receivedSeq,uint32_t commandSeq,bool desiredEveryone,bool actualEveryone,bool actualRoommates){
  return receivedSeq>commandSeq && (desiredEveryone ? (actualEveryone&&!actualRoommates) : (actualRoommates&&!actualEveryone));
}
