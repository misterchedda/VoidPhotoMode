#include "ui/hud_patch.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstring>
#include <cstdlib>
void check(bool b,const char* m){if(!b){std::cerr<<m<<'\n';std::exit(1);}}
uint64_t hash(const std::vector<unsigned char>& v){uint64_t h=14695981039346656037ull;for(auto c:v)h=(h^c)*1099511628211ull;return h;}
template<class T>void append(std::vector<unsigned char>& v,T n){auto p=reinterpret_cast<unsigned char*>(&n);v.insert(v.end(),p,p+sizeof(T));}
int main(int argc,char**argv){
 std::vector<unsigned char> original={1,2,3,4},target={1,2,9,4},patch={'V','P','D','1'},result={7};
 append(patch,uint32_t(4));append(patch,uint32_t(4));append(patch,hash(original));append(patch,hash(target));
 patch.push_back(0);append(patch,uint32_t(0));append(patch,uint32_t(2));patch.push_back(1);append(patch,uint32_t(1));patch.push_back(9);patch.push_back(0);append(patch,uint32_t(3));append(patch,uint32_t(1));
 check(HudPatch::decode(original.data(),original.size(),patch.data(),patch.size(),result)&&result==target,"copy/literal reconstruction");
 for(size_t n=0;n<patch.size();++n){result={7};check(!HudPatch::decode(original.data(),original.size(),patch.data(),n,result)&&result==std::vector<unsigned char>{7},"truncated patch preserves output");}
 for(size_t i=0;i<patch.size();++i){auto corrupt=patch;corrupt[i]^=0xff;result={7};check(!HudPatch::decode(original.data(),original.size(),corrupt.data(),corrupt.size(),result)&&result==std::vector<unsigned char>{7},"corruption rejected without changing output");}
 original[0]=8;check(!HudPatch::decode(original.data(),original.size(),patch.data(),patch.size(),result),"wrong original rejected");
 if(argc==3){std::ifstream a(argv[1],std::ios::binary),b(argv[2],std::ios::binary);std::vector<unsigned char> src((std::istreambuf_iterator<char>(a)),{}),dst((std::istreambuf_iterator<char>(b)),{});check(!src.empty()&&!dst.empty(),"local HUD fixtures available");check(HudPatch::apply(src.data(),src.size(),result)&&result==dst,"embedded patch reconstructs exact approved HUD");}
 std::cout<<"PASS: bounds, corruption, wrong original, output preservation and HUD reconstruction\n";
}
