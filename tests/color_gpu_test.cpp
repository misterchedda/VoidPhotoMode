#include "effects/color_fx.h"
#include <d3d11.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <vector>
#include <array>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cmath>
using Microsoft::WRL::ComPtr;
void check(bool b,const char* m){if(!b){std::cerr<<m<<'\n';std::exit(1);}}
int main(int argc,char**){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
 check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)),"Create hardware device");
 ComPtr<ID3D11DeviceContext> immediate=c;
 if(argc>1){c.Reset();check(SUCCEEDED(d->CreateDeferredContext(0,&c)),"Create deferred test context");}
 auto flush=[&](){if(c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED){ComPtr<ID3D11CommandList> list;check(SUCCEEDED(c->FinishCommandList(TRUE,&list)),"Finish color command list");immediate->ExecuteCommandList(list.Get(),TRUE);}};
 check(MH_Initialize()==MH_OK,"Initialize hooks");std::string error;
 check(ColorFx::install(error),error.c_str());check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"Enable hook");
 std::vector<float> data(32*32*32*4);
 for(size_t i=0;i<data.size();i+=4){data[i]=0.8f;data[i+1]=0.4f;data[i+2]=0.2f;data[i+3]=1;}
 for(int i=4;i<7;++i)data[i]=0.02f; // A dark voxel exercises shadow contrast.
 D3D11_TEXTURE3D_DESC td{};td.Width=td.Height=td.Depth=32;td.MipLevels=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA init{data.data(),32*16,32*32*16};ComPtr<ID3D11Texture3D> src;ComPtr<ID3D11ShaderResourceView> srv;
 check(SUCCEEDED(d->CreateTexture3D(&td,&init,&src)),"Create test cube");check(SUCCEEDED(d->CreateShaderResourceView(src.Get(),nullptr,&srv)),"Create cube view");
 unsigned char wrapper[0x38]{};for(int off:{0x18,0x1c,0x20})*reinterpret_cast<int*>(wrapper+off)=32;
 *reinterpret_cast<void**>(wrapper+0x30)=srv.Get();ColorFx::begin();ColorFx::trackCube(wrapper);
 auto bound=[&](){ComPtr<ID3D11ShaderResourceView> result;c->PSSetShaderResources(7,1,srv.GetAddressOf());c->PSGetShaderResources(7,1,&result);return result;};
 check(bound().Get()==srv.Get(),"Neutral must be exact pass-through");
 check(!ColorFx::set(0,NAN)&&!ColorFx::set(1,0)&&!ColorFx::set(2,2),"Invalid values rejected");
 auto pixel=[&](int index=0){auto view=bound();check(view.Get()!=srv.Get(),"Adjusted cube must be private");ComPtr<ID3D11Resource> r;view->GetResource(&r);ComPtr<ID3D11Texture3D> tex;r.As(&tex);D3D11_TEXTURE3D_DESC out{};tex->GetDesc(&out);out.Usage=D3D11_USAGE_STAGING;out.CPUAccessFlags=D3D11_CPU_ACCESS_READ;out.BindFlags=0;ComPtr<ID3D11Texture3D> staging;check(SUCCEEDED(d->CreateTexture3D(&out,nullptr,&staging)),"Staging creation");flush();immediate->CopyResource(staging.Get(),tex.Get());D3D11_MAPPED_SUBRESOURCE m{};check(SUCCEEDED(immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)),"Read GPU result");auto h=static_cast<unsigned short*>(m.pData)+index*4;std::array<float,3> rgb;for(int i=0;i<3;++i)rgb[i]=DirectX::PackedVector::XMConvertHalfToFloat(h[i]);immediate->Unmap(staging.Get(),0);return rgb;};
 ColorFx::set(0,0);auto gray=pixel();check(std::abs(gray[0]-gray[1])<0.001f&&std::abs(gray[1]-gray[2])<0.001f,"Saturation zero is grayscale");
 ColorFx::reset();ColorFx::set(2,1);auto warm=pixel();check(warm[0]>0.8f&&warm[2]<0.2f,"Warmth increases red and reduces blue");
 ColorFx::reset();ColorFx::set(3,1);auto tint=pixel();check(tint[1]<0.4f,"Positive tint reduces green");
 ColorFx::reset();ColorFx::set(1,1.5f);auto contrast=pixel();check(contrast[0]>0.8f&&contrast[1]>0.4f,"Contrast raises values above linear middle gray");
 auto shadow=pixel(1);check(shadow[0]>0&&shadow[0]<0.02f,"Contrast darkens shadows without clipping them to black");
 ComPtr<ID3D11ComputeShader> shader;UINT count=0;c->CSGetShader(&shader,nullptr,&count);check(!shader,"Compute shader state restored");
 ComPtr<ID3D11ShaderResourceView> cs;c->CSGetShaderResources(0,1,&cs);check(!cs,"Compute SRV restored");
 ComPtr<ID3D11UnorderedAccessView> uav;c->CSGetUnorderedAccessViews(0,1,&uav);check(!uav,"Compute UAV restored");
 ColorFx::reset();ColorFx::set(0,0);
 auto previousPs=bound();
 c->CSSetShaderResources(7,1,srv.GetAddressOf());ComPtr<ID3D11ShaderResourceView> adjustedCs;c->CSGetShaderResources(7,1,&adjustedCs);
 ComPtr<ID3D11ShaderResourceView> preservedPs;c->PSGetShaderResources(7,1,&preservedPs);
 check(preservedPs.Get()==previousPs.Get(),"Transform preserves previously bound pixel-stage LUT");
 check(adjustedCs.Get()!=srv.Get(),"Compute-stage cube binding is adjusted without recursion");
 ColorFx::end();check(bound().Get()==srv.Get(),"Exit restores original cube binding");
 // The source cube retains its authored pixels after every transform.
 td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.BindFlags=0;ComPtr<ID3D11Texture3D> staging;d->CreateTexture3D(&td,nullptr,&staging);flush();immediate->CopyResource(staging.Get(),src.Get());D3D11_MAPPED_SUBRESOURCE m{};immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&m);check(std::memcmp(m.pData,data.data(),16)==0,"Authored cube remains unchanged");immediate->Unmap(staging.Get(),0);
 MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();std::cout<<"PASS: GPU color transform, identity, limits, original cube preservation, compute state and exit\n";
}
