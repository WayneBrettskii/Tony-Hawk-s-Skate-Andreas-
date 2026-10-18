#include "../gta/arsenal_logic.h"
#include <cstdio>
#include <cstdlib>
using namespace mp::gta::arsenal;
void Check(bool pass, const char* name) {
    if (!pass) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); }
}
int main() {
    for(int count=1;count<=13;++count) {
        for(int slot=0;slot<count;++slot) {
            float a=slot*6.2831853f/count;
            Check(WheelSlot(std::sin(a),-std::cos(a),count,0)==slot,"sector centre");
        }
        Check(WheelSlot(0,0,count,count-1)==count-1,"dead zone retains selection");
        Check(WheelSlot(.01f,-1,count,0)==0,"wrap clockwise at top");
        Check(WheelSlot(-.01f,-1,count,0)==0,"wrap anticlockwise at top");
    }
    Check(WheelSlot(0,0,0,0)==-1,"empty inventory");
    Check(WheelSlot(0,0,3,7)==0,"invalid previous selection");
    Check(WheelSlot(1,0,4,0)==1 && WheelSlot(0,1,4,0)==2 && WheelSlot(-1,0,4,0)==3,"stick orientation");
    int count=0;
    for(int type=-100;type<100;++type) if(WeaponType(type)) ++count;
    Check(count==44,"valid weapon count");
    Check(!WeaponType(19)&&!WeaponType(20)&&!WeaponType(21),"internal projectile IDs excluded");
    Check(Wrap(-1,44)==43&&Wrap(44,44)==0&&Wrap(1,0)==0,"menu wrapping");
    std::puts("PASS: wheel sectors/dead zone/wrap, input orientation, weapon bounds and menu wrapping");
}
