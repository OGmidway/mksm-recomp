#include "runtime/ps2_pad.h"
#include "runtime/pad_profile.h"
#include "ps2_host_backend.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {
using namespace ps2::pad;
const std::array<Profile,2>& controllerProfiles() {
    static const auto profiles=[] {
        const char* path=std::getenv("PS2_CONTROLLER_CONFIG");
        if(!path || !*path)return defaults();
        try {
            std::ifstream file(path,std::ios::binary|std::ios::ate);
            if(!file || file.tellg()>65536)throw std::runtime_error("missing or oversized profile");
            file.seekg(0);auto result=readProfiles(file);
            std::cerr << "[pad] profile loaded: " << path << "\n";
            return result;
        } catch(const std::exception& error) {
            std::cerr << "[pad] profile error: " << error.what() << "; using default mappings\n";
            return defaults();
        }
    }();
    return profiles;
}
State sampleController(int ordinal,int port) {
    State state{};int device=-1;
    if(ordinal>=0)for(int index=0,count=0;index<4;++index)
        if(IsGamepadAvailable(index) && count++==ordinal){device=index;break;}
    static std::array<int,2> previous={-2,-2};static unsigned messages=0;
    if(previous[port]!=device) {
        previous[port]=device;
        if(messages++<16)std::cerr << "[pad] port=" << port+1 << " host=" << device << " name="
            << (device>=0?GetGamepadName(device):"disconnected") << "\n";
    }
    if(device<0)return state;
    state.connected=true;
    const int buttons[]={GAMEPAD_BUTTON_RIGHT_FACE_DOWN,GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,
        GAMEPAD_BUTTON_RIGHT_FACE_LEFT,GAMEPAD_BUTTON_RIGHT_FACE_UP,GAMEPAD_BUTTON_MIDDLE_LEFT,
        GAMEPAD_BUTTON_MIDDLE_RIGHT,GAMEPAD_BUTTON_LEFT_THUMB,GAMEPAD_BUTTON_RIGHT_THUMB,
        GAMEPAD_BUTTON_LEFT_TRIGGER_1,GAMEPAD_BUTTON_RIGHT_TRIGGER_1,GAMEPAD_BUTTON_LEFT_FACE_UP,
        GAMEPAD_BUTTON_LEFT_FACE_RIGHT,GAMEPAD_BUTTON_LEFT_FACE_DOWN,GAMEPAD_BUTTON_LEFT_FACE_LEFT};
    for(unsigned i=0;i<ButtonCount;++i)state.buttons[i]=IsGamepadButtonDown(device,buttons[i]);
    const int axes[]={GAMEPAD_AXIS_LEFT_X,GAMEPAD_AXIS_LEFT_Y,GAMEPAD_AXIS_RIGHT_X,GAMEPAD_AXIS_RIGHT_Y,
                      GAMEPAD_AXIS_LEFT_TRIGGER,GAMEPAD_AXIS_RIGHT_TRIGGER};
    const int count=GetGamepadAxisCount(device);
    for(unsigned i=0;i<6;++i)if(axes[i]<count) {
        const float value=GetGamepadAxisMovement(device,axes[i]);
        state.axes[i]=i<4?value:(value+1.0f)*0.5f;
    }
    return state;
}
constexpr uint16_t PAD_SELECT=1, PAD_L3=2, PAD_R3=4, PAD_START=8,
 PAD_UP=0x10, PAD_RIGHT=0x20, PAD_DOWN=0x40, PAD_LEFT=0x80,
 PAD_L2=0x100, PAD_R2=0x200, PAD_L1=0x400, PAD_R1=0x800,
 PAD_TRIANGLE=0x1000, PAD_CIRCLE=0x2000, PAD_CROSS=0x4000, PAD_SQUARE=0x8000;
}

bool PSPadBackend::readState(int port,int slot,uint8_t* data,size_t size) {
    if(!data || size<32 || port<0 || port>1 || slot!=0)return false;
    const auto& profile=controllerProfiles()[port];
    const auto state=IsWindowReady()?sampleController(profile.device,port):ps2::pad::State{};
    const auto encoded=ps2::pad::encode(profile,state);std::memcpy(data,encoded.data(),32);
    if(!IsWindowReady())return true;
    uint16_t btns=uint16_t(data[2])|(uint16_t(data[3])<<8);
    auto clearBit=[&](uint16_t mask){btns&=uint16_t(~mask);};
    if (port == 0)
    {
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
            clearBit(PAD_UP);
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
            clearBit(PAD_DOWN);
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
            clearBit(PAD_LEFT);
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
            clearBit(PAD_RIGHT);
        if (IsKeyDown(KEY_X) || IsKeyDown(KEY_SPACE))
            clearBit(PAD_CROSS);
        if (IsKeyDown(KEY_C) || IsKeyDown(KEY_ESCAPE))
            clearBit(PAD_CIRCLE);
        if (IsKeyDown(KEY_Z) || IsKeyDown(KEY_KP_0))
            clearBit(PAD_SQUARE);
        if (IsKeyDown(KEY_V) || IsKeyDown(KEY_KP_1))
            clearBit(PAD_TRIANGLE);
        if (IsKeyDown(KEY_Q))
            clearBit(PAD_L1);
        if (IsKeyDown(KEY_E))
            clearBit(PAD_R1);
        if (IsKeyDown(KEY_LEFT_SHIFT))
            clearBit(PAD_L2);
        if (IsKeyDown(KEY_RIGHT_SHIFT))
            clearBit(PAD_R2);
        if (IsKeyDown(KEY_ENTER))
            clearBit(PAD_START);
        if (IsKeyDown(KEY_TAB))
            clearBit(PAD_SELECT);
    }

    data[2] = static_cast<uint8_t>(btns & 0xFF);
    data[3] = static_cast<uint8_t>(btns >> 8);
    return true;
}
