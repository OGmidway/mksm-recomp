#include "runtime/pad_profile.h"
#include <sstream>
#include <limits>
static bool checkControllerProfiles() {
 using namespace ps2::pad;
 std::istringstream config("[Pad1]\nDevice=0\nDeadzone=0\nAxisScale=1.33\nTriggerThreshold=0\nCross=South\n[Pad2]\nDevice=1\nCross=East\n");
 auto profiles=readProfiles(config);State state{};state.connected=true;
 auto neutral=encode(profiles[0],state);
 if(neutral[2]!=255 || neutral[3]!=255 || neutral[4]!=128 || neutral[6]!=128)return false;
 state.buttons[South]=true;
 if(encode(profiles[0],state)[3]!=0xbf || encode(profiles[1],state)[3]!=0xff)return false;
 state.buttons[South]=false;state.buttons[East]=true;
 if(encode(profiles[1],state)[3]!=0x9f)return false; // Remap Cross, retain Circle.
 state.connected=false;if(encode(profiles[0],state)!=neutral)return false;
 state={};state.connected=true;state.axes[4]=0.01f;
 if(encode(profiles[0],state)[3]!=0xfe)return false;
 profiles[0].triggerThreshold=.2f;
 if(encode(profiles[0],state)[3]!=255)return false;
 state.axes[4]=.5f;if(encode(profiles[0],state)[3]!=0xfe)return false;
 state.axes[4]=std::numeric_limits<float>::quiet_NaN();
 if(encode(profiles[0],state)[3]!=255)return false;
 struct Case {float deadzone,scale,x,y;unsigned expectedX,expectedY;};
 const Case cases[]={
#include "controller_profile_cases.inc"
 };
 for(const auto& test:cases) {
  Profile p;p.deadzone=test.deadzone;p.scale=test.scale;State s{};s.connected=true;s.axes[0]=test.x;s.axes[1]=test.y;
  const auto data=encode(p,s);if(data[6]!=test.expectedX || data[7]!=test.expectedY)return false;
 }
 for(const char* invalid:{"[Pad1]\nDeadzone=nan", "[Pad1]\nDevice=4", "[Pad1]\nDevice=0.5", "[Pad1]\nCross=JoyButton99", "[Other]\nDevice=0", "[Pad1]\nAxisScale=1junk"}) {
  try {std::istringstream bad(invalid);readProfiles(bad);return false;}catch(const std::exception&){}
 }
 return true;
}
