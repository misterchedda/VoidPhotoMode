#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <iostream>
int wmain(int argc,wchar_t**argv){
 if(argc!=2)return 1;
 auto module=LoadLibraryW(argv[1]);if(!module)return 2;
 for(auto name:{"DirectInput8Create","DllCanUnloadNow","DllGetClassObject","DllRegisterServer","DllUnregisterServer"})if(!GetProcAddress(module,name))return 3;
 auto create=reinterpret_cast<HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN)>(GetProcAddress(module,"DirectInput8Create"));
 IDirectInput8W* input=nullptr;HRESULT hr=create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr);
 if(FAILED(hr)||!input)return 4;
 input->Release();std::cout<<"PASS: proxy exports and real DirectInput forwarding\n";return 0;
}
