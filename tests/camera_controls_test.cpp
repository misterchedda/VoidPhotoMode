#include "camera/camera_controls.h"
#include <iostream>
#include <cstdlib>
using namespace DirectX;
void check(bool ok,const char* name){if(!ok){std::cerr<<name<<'\n';std::exit(1);}}
int main(){
    for(float yaw:{-170.f,0.f,80.f})for(float pitch:{-80.f,-60.f,-40.f,-20.f,0.f,20.f,40.f,60.f,80.f})for(float roll:{-175.f,-45.f,0.f,35.f,175.f}){
        XMVECTOR r=XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),XMConvertToRadians(-roll));
        XMVECTOR p=XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),XMConvertToRadians(pitch));
        XMVECTOR y=XMQuaternionRotationAxis(XMVectorSet(0,0,1,0),XMConvertToRadians(-yaw));
        XMVECTOR q=XMQuaternionMultiply(XMQuaternionMultiply(r,p),y);
        XMFLOAT4 stored;XMStoreFloat4(&stored,q);
        float found=CameraControls::rollDegrees(&stored.x);
        check(std::abs(found-roll)<0.002f,"Roll must retain its sign and angle across yaw and pitch");
        // Apply the same local-X correction used by the absolute roll command.
        q=XMQuaternionMultiply(XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),XMConvertToRadians(found)),q);
        XMStoreFloat4(&stored,q);
        check(std::abs(CameraControls::rollDegrees(&stored.x))<0.002f,"Roll zero must level the horizon");
    }
    XMFLOAT4 vertical;XMStoreFloat4(&vertical,XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),XM_PIDIV2));
    check(std::isfinite(CameraControls::rollDegrees(&vertical.x)),"Vertical camera must not produce NaN");
    float speed=1;
    for(int i=0;i<20;++i)speed=CameraControls::nextSpeed(speed,true);
    check(speed==16,"Speed upper bound");
    for(int i=0;i<20;++i)speed=CameraControls::nextSpeed(speed,false);
    check(speed==0.1f,"Speed lower bound");
    check(CameraControls::nextSpeed(0.3f,true)==0.5f&&CameraControls::nextSpeed(0.3f,false)==0.25f,"Custom speed must select adjacent preset");
    std::cout<<"PASS: 135 camera orientations, horizon reset, vertical view, speed bounds\n";
}
