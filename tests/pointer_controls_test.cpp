#include "camera/pointer_controls.h"
#include <cstdlib>
#include <iostream>
void check(bool ok,const char* why){if(!ok){std::cerr<<why;std::exit(1);}}
int main(){
 PointerControls p;
 auto m=p.frame(true,false,100,50);check(!m.lookX&&!m.pointerX,"entry discards stale motion");
 m=p.frame(true,false,20,10);check(m.pointerX==20&&!m.lookX,"pointer cannot rotate camera");
 m=p.frame(true,true,100,50);check(m.looking&&!m.lookX&&!m.pointerX,"RMB capture boundary discards mixed motion");
 m=p.frame(true,true,20,10);check(m.lookX==20&&!m.pointerX,"RMB only rotates camera");
 m=p.frame(true,false,100,50);check(!m.lookX&&!m.pointerX,"RMB release cannot jump either cursor or camera");
 m=p.frame(true,false,5,6);check(m.pointerX==5&&!m.lookX,"cursor resumes from saved position");
 p.frame(false,true,900,900);m=p.frame(true,true,900,900);check(!m.lookX,"alt-tab return discards accumulated motion");
 check(PointerControls::wheelSpeed(1,120)>1,"wheel up speeds up");
 check(PointerControls::wheelSpeed(1,-120)<1,"wheel down slows down");
 check(std::abs(PointerControls::wheelSpeed(PointerControls::wheelSpeed(1,60),60)-1.25f)<0.00001f,"high resolution wheel composes correctly");
 check(PointerControls::wheelSpeed(16,120)==16&&PointerControls::wheelSpeed(0.1f,-120)==0.1f,"wheel speed remains bounded");
 std::cout<<"PASS: pointer ownership, gesture boundaries, focus loss, proportional wheel speed\n";
}
