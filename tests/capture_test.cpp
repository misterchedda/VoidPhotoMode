#include "capture/capture_io.h"
#include "capture/partial_file.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <iostream>
#include <cstdlib>
#include <fstream>
using Microsoft::WRL::ComPtr;
void check(bool value,const std::string& reason){if(!value){std::cerr<<reason<<'\n';std::exit(1);}}
int main(){
 check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM initialization");
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"D3D test device");
 auto directory=std::filesystem::temp_directory_path()/(L"VoidPhotoCaptureTest-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(directory);
 auto interrupted=directory/L"exception.partial";
 {std::ofstream reserve(interrupted);reserve<<"incomplete";}
 try {PhotoCapture::PartialFile cleanup(interrupted);throw std::bad_alloc();}
 catch(const std::bad_alloc&) {}
 check(!std::filesystem::exists(interrupted),"Exception unwinding removes reserved partial file");
 std::string error;
 // Odd width forces a padded GPU row pitch; deliberately nonopaque source alpha.
 for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}){
  constexpr UINT width=7,height=3;std::vector<BYTE> source(width*height*4),expected(width*height*3);
  bool bgra=format==DXGI_FORMAT_B8G8R8A8_UNORM;
  for(UINT i=0;i<width*height;++i){BYTE r=BYTE(i*11),g=BYTE(250-i*7),b=BYTE(5+i*3);source[4*i]=bgra?b:r;source[4*i+1]=g;source[4*i+2]=bgra?r:b;source[4*i+3]=BYTE(i);expected[3*i]=r;expected[3*i+1]=g;expected[3*i+2]=b;}
  D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.Format=format;desc.SampleDesc.Count=1;
  D3D11_SUBRESOURCE_DATA data{source.data(),width*4,0};ComPtr<ID3D11Texture2D> texture;
  check(SUCCEEDED(device->CreateTexture2D(&desc,&data,&texture)),"Create padded test texture");
  PhotoCapture::Pixels pixels;check(PhotoCapture::readTexture(texture.Get(),pixels,error),error);check(pixels.rgb==expected,"RGB conversion and row pitch preserve exact pixels");
  auto file=directory/(std::to_wstring(format)+L".png");check(PhotoCapture::writePng(pixels,file,error),error);
  ComPtr<IWICImagingFactory> factory;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"Decoder factory");
  ComPtr<IWICBitmapDecoder> decoder;check(SUCCEEDED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)),"Decode PNG");
  ComPtr<IWICBitmapFrameDecode> frame;decoder->GetFrame(0,&frame);UINT w=0,h=0;frame->GetSize(&w,&h);check(w==width&&h==height,"PNG exact dimensions");
  ComPtr<IWICFormatConverter> converter;factory->CreateFormatConverter(&converter);check(SUCCEEDED(converter->Initialize(frame.Get(),GUID_WICPixelFormat24bppRGB,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)),"RGB decoder conversion");
  std::vector<BYTE> decoded(expected.size());check(SUCCEEDED(converter->CopyPixels(nullptr,width*3,UINT(decoded.size()),decoded.data())),"Read decoded pixels");check(decoded==expected,"PNG round trip is lossless and correctly oriented");
  ComPtr<IWICMetadataQueryReader> metadata;frame->GetMetadataQueryReader(&metadata);PROPVARIANT intent{};check(SUCCEEDED(metadata->GetMetadataByName(L"/sRGB/RenderingIntent",&intent))&&intent.vt==VT_UI1&&intent.bVal==0,"PNG is tagged sRGB");PropVariantClear(&intent);
  check(!PhotoCapture::writePng(pixels,file,error),"Existing captures must not be overwritten");
  check(!std::filesystem::exists(file.wstring()+L".partial"),"Failed publication removes its partial file");
  metadata.Reset();converter.Reset();frame.Reset();decoder.Reset();std::filesystem::remove(file);
 }
 D3D11_TEXTURE2D_DESC hdr{};hdr.Width=hdr.Height=4;hdr.MipLevels=hdr.ArraySize=1;hdr.SampleDesc.Count=1;hdr.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;ComPtr<ID3D11Texture2D> texture;device->CreateTexture2D(&hdr,nullptr,&texture);
 PhotoCapture::Pixels pixels;check(!PhotoCapture::readTexture(texture.Get(),pixels,error),"Unsupported HDR rejected");
 pixels.width=2;pixels.height=2;pixels.rgb.resize(1);check(!PhotoCapture::writePng(pixels,directory/L"invalid.png",error),"Malformed pixel buffer rejected");
 pixels.width=1;pixels.height=1;pixels.rgb={1,2,3};check(!PhotoCapture::writePng(pixels,directory/L"missing"/L"fail.png",error),"Unavailable output fails safely");
 std::filesystem::remove(directory);CoUninitialize();std::cout<<"PASS: GPU row pitch/channel order, exact PNG round trip, sRGB metadata, dimensions, no overwrite and failure cleanup\n";
}


