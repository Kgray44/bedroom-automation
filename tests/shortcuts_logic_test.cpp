#include <cassert>
#include <cstdint>
#include "../BridgeController/ShortcutLogic.h"
int main(){
  assert(dlWeekdayBit(1)==1);assert(dlWeekdayBit(2)==2);
  assert(dlWeekdayBit(5)==16);assert(dlWeekdayBit(6)==32);assert(dlWeekdayBit(0)==64);
  assert(dlWeekdayBit(7)==0);
  for(int d=0;d<7;d++){int mask=dlWeekdayBit(d);assert(mask==64 || (mask&31) || mask==32);}
  assert(dlBrightnessPercent(80,50,true)==40);
  assert(dlBrightnessPercent(80,50,false)==80);
  assert(dlBrightnessPercent(65,100,true)==0);
  assert(dlBrightnessPercent(150,50,true)==50);
  assert(dlBrightnessPercent(-5,50,true)==0);
  assert(!dlExpectedReadback(3,3,true,true,false));
  assert(!dlExpectedReadback(4,3,true,false,true));
  assert(dlExpectedReadback(4,3,true,true,false));
  assert(dlExpectedReadback(4,3,false,false,true));
  assert(!dlExpectedReadback(4,3,true,true,true));
  return 0;
}
