#include <cassert>
#include "../BridgeController/BedroomRestorePolicy.h"
int main(){
  assert(BEDROOM_MAX_RESTORE_ATTEMPTS==3);
  assert(bedroomRestorationDecision(true,false,false,false)==BEDROOM_ALREADY_RESTORED);
  assert(bedroomRestorationDecision(true,true,false,true)==BEDROOM_ALREADY_RESTORED);
  assert(bedroomRestorationDecision(false,true,true,false)==BEDROOM_RESTORE_ALLOWED);
  assert(bedroomRestorationDecision(false,true,false,false)==BEDROOM_RESTORE_BLOCKED);
  assert(bedroomRestorationDecision(false,false,false,false)==BEDROOM_RESTORE_BLOCKED);
  assert(bedroomRestorationDecision(false,false,false,true)==BEDROOM_RESTORE_ALLOWED);
  assert(bedroomRestorationDecision(false,true,false,true)==BEDROOM_RESTORE_ALLOWED);
}
