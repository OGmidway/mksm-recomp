#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <istream>
#include <stdexcept>
#include <string>

// Controller layout vocabulary is independent of the host API and controller brand.
namespace ps2::pad {
enum Button { South, East, West, North, Back, Start, LeftStick, RightStick,
              LeftShoulder, RightShoulder, Up, Right, Down, Left, ButtonCount };
struct State {
    bool connected = false;
    std::array<bool, ButtonCount> buttons{};
    // Sticks: -1..1, positive Y down. Triggers: 0..1, zero released.
    std::array<float, 6> axes{};
};
struct Binding { int button=-1, axis=-1; float sign=1; };
inline constexpr std::array<const char*,24> targets={
    "Select","L3","R3","Start","Up","Right","Down","Left",
    "L2","R2","L1","R1","Triangle","Circle","Cross","Square",
    "LLeft","LRight","LUp","LDown","RLeft","RRight","RUp","RDown"};
inline constexpr std::array<const char*,ButtonCount> buttonNames={
    "South","East","West","North","Back","Start","LeftStick","RightStick",
    "LeftShoulder","RightShoulder","DPadUp","DPadRight","DPadDown","DPadLeft"};
inline constexpr std::array<const char*,6> axisNames={"LeftX","LeftY","RightX","RightY","LeftTrigger","RightTrigger"};
struct Profile {
    int device=0;
    float deadzone=0.12f, scale=1.0f, triggerThreshold=0.20f;
    std::array<Binding,24> bindings{};
    Profile() {
        const int buttons[]={Back,LeftStick,RightStick,Start,Up,Right,Down,Left,-1,-1,
                             LeftShoulder,RightShoulder,North,East,South,West};
        for(unsigned i=0;i<16;++i)bindings[i].button=buttons[i];
        bindings[8]={-1,4,1};bindings[9]={-1,5,1};
        for(unsigned axis=0;axis<4;++axis){bindings[16+axis*2]={-1,int(axis),-1};bindings[17+axis*2]={-1,int(axis),1};}
    }
};
inline std::array<Profile,2> defaults() {std::array<Profile,2> p{};p[1].device=1;return p;}
inline std::string trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");
    return a==std::string::npos ? "" : s.substr(a,b-a+1);
}
inline Binding parseBinding(std::string value) {
    if(value.empty() || value=="None")return {};
    for(unsigned i=0;i<buttonNames.size();++i)if(value==buttonNames[i])return {int(i),-1,1};
    float sign=1;
    if(value.front()=='-' || value.front()=='+'){sign=value.front()=='-'?-1.0f:1.0f;value.erase(0,1);}
    for(unsigned i=0;i<axisNames.size();++i)if(value==axisNames[i])return {-1,int(i),sign};
    throw std::runtime_error("unknown binding: "+value);
}
inline float number(const std::string& s,float low,float high) {
    size_t used=0;float n=std::stof(s,&used);
    if(used!=s.size() || !std::isfinite(n) || n<low || n>high)throw std::runtime_error("invalid numeric setting: "+s);
    return n;
}
inline std::array<Profile,2> readProfiles(std::istream& stream) {
    auto profiles=defaults();int section=-1;std::string line;size_t bytes=0;
    while(std::getline(stream,line)) {
        bytes+=line.size()+1;if(bytes>65536)throw std::runtime_error("controller profile exceeds 64 KiB");
        line=trim(line);if(line.empty() || line[0]=='#' || line[0]==';')continue;
        if(line[0]=='[') {
            if(line=="[Pad1]")section=0;else if(line=="[Pad2]")section=1;
            else throw std::runtime_error("unknown controller section: "+line);
            continue;
        }
        const auto equal=line.find('=');
        if(section<0 || equal==std::string::npos)throw std::runtime_error("expected Pad section and key=value");
        const auto key=trim(line.substr(0,equal)),value=trim(line.substr(equal+1));auto& p=profiles[section];
        if(key=="Device") {float n=number(value,-1,3);if(n!=std::floor(n))throw std::runtime_error("Device must be an integer");p.device=int(n);}
        else if(key=="Deadzone")p.deadzone=number(value,0,0.95f);
        else if(key=="AxisScale")p.scale=number(value,0.01f,2);
        else if(key=="TriggerThreshold")p.triggerThreshold=number(value,0,1);
        else {
            auto found=std::find(targets.begin(),targets.end(),key);
            if(found==targets.end())throw std::runtime_error("unknown controller setting: "+key);
            p.bindings[size_t(found-targets.begin())]=parseBinding(value);
        }
    }
    return profiles;
}
inline float magnitude(const State& state,const Binding& binding) {
    if(!state.connected)return 0;
    if(binding.button>=0)return state.buttons[size_t(binding.button)]?1.0f:0.0f;
    if(binding.axis>=0) {
        const float v=state.axes[size_t(binding.axis)];
        return std::isfinite(v)?std::clamp(v*binding.sign,0.0f,1.0f):0.0f;
    }
    return 0;
}
inline uint8_t axisByte(float value) {
    return uint8_t(std::lround((std::clamp(value,-1.0f,1.0f)+1.0f)*127.5f));
}
inline std::array<uint8_t,32> encode(const Profile& p,const State& s) {
    std::array<uint8_t,32> out{};out[0]=1;out[1]=0x73;
    uint16_t buttons=0xffff;
    for(unsigned i=0;i<16;++i) {
        const float threshold=(i==8||i==9)?p.triggerThreshold:0.5f;
        const float value=magnitude(s,p.bindings[i]);
        if(value>0 && value>=threshold)buttons&=uint16_t(~(1u<<i));
    }
    out[2]=uint8_t(buttons);out[3]=uint8_t(buttons>>8);
    for(unsigned stick=0;stick<2;++stick) {
        float x=magnitude(s,p.bindings[17+stick*4])-magnitude(s,p.bindings[16+stick*4]);
        float y=magnitude(s,p.bindings[19+stick*4])-magnitude(s,p.bindings[18+stick*4]);
        const float length=std::sqrt(x*x+y*y);
        const float factor=length>p.deadzone?p.scale:0;
        const unsigned offset=stick==0?6:4;out[offset]=axisByte(x*factor);out[offset+1]=axisByte(y*factor);
    }
    return out;
}
} // namespace ps2::pad
