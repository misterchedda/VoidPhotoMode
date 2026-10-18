#include "camera/focus_controls.h"
#include <cmath>
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    for(float distance:{0.1f,1.f,5.f,1000.f})for(float width:{0.05f,1.f,100.f}){
        auto value=[&](uintptr_t site){return FocusControls::parameter(site,123.f,distance,width);};
        // Reproduce the normal branch's four CoC distance breakpoints. A manual
        // setting must yield ordered finite near/far ramps, even at the closest
        // distance with a very wide focus region.
        float center=value(0x3140bb),near=value(0x3140d2),sharp=value(0x3140e9);
        float near0=std::fmax(0.f,center-near-sharp-0.001f);
        float near1=std::fmax(0.f,center-sharp);
        float far0=center+value(0x31411d),far1=far0+value(0x314134);
        check(std::isfinite(far1)&&near0<=near1&&near1<=far0&&far0<far1,"Invalid focus ramps");
        check(value(0x31429b)==0&&value(0x314100)==0,"Manual focus must disable tracking and center bias");
        check(value(0x314327)==123&&value(0x1234)==123,"Unrelated lens and engine reads must remain authored");
    }
    std::cout<<"PASS: focus-ramp bounds, autofocus/bias suppression, unrelated parameter preservation\n";
}
