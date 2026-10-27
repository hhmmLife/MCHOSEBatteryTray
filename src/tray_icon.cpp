#include "tray_icon.hpp"
#include <cmath>
#include <vector>

namespace mchose {
namespace {
struct Rgb {unsigned r,g,b;};
bool InBolt(float x,float y){
    constexpr float points[][2]={{18,14},{12,22},{16,22},{14,29},{21,19},{17,19}};
    bool inside=false;
    for(int i=0,j=5;i<6;j=i++){
        float xi=points[i][0],yi=points[i][1],xj=points[j][0],yj=points[j][1];
        if((yi>y)!=(yj>y)&&x<(xj-xi)*(y-yi)/(yj-yi)+xi)inside=!inside;
    }
    return inside;
}
}
HICON MakeIcon(int size,int percent,bool valid,bool charging,bool dark){
    if(size<=0||size>256)return nullptr;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;
    info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!color)return nullptr;
    auto pixels=static_cast<DWORD*>(bits);
    const Rgb base=dark?Rgb{85,92,100}:Rgb{196,202,207};
    const Rgb edge=dark?Rgb{228,237,243}:Rgb{40,50,58};
    const Rgb fill=percent>=50?Rgb{25,230,181}:percent>=25?Rgb{245,190,45}:Rgb{248,83,88};
    constexpr int samples=4,total=samples*samples;
    float top=31-30*std::clamp(percent,0,100)/100.0f;
    for(int py=0;py<size;++py)for(int px=0;px<size;++px){
        unsigned red=0,green=0,blue=0,coverage=0;
        for(int sy=0;sy<samples;++sy)for(int sx=0;sx<samples;++sx){
            float x=(px+(sx+.5f)/samples)*32/size,y=(py+(sy+.5f)/samples)*32/size;
            float dx=x-16,dy=y-std::clamp(y,10.0f,22.0f);
            float distance=std::sqrt(dx*dx+dy*dy)-9;
            if(distance>0)continue;
            Rgb value=valid&&y>=top?fill:base;
            if(distance>=-1.5f||std::abs(y-11)<.75f||(std::abs(x-16)<.75f&&y<10)||
               (x>=14.8f&&x<=17.2f&&y>=4&&y<=8))value=edge;
            if(valid&&charging&&InBolt(x,y))value={25,35,41};
            red+=value.r;green+=value.g;blue+=value.b;++coverage;
        }
        // Averaging against transparent black produces premultiplied alpha.
        pixels[py*size+px]=((coverage*255/total)<<24)|((red/total)<<16)|((green/total)<<8)|(blue/total);
    }
    std::vector<BYTE> maskBits(((size+15)/16)*2*size,0);
    HBITMAP mask=CreateBitmap(size,size,1,1,maskBits.data());HICON icon=nullptr;
    if(mask){ICONINFO iconInfo{};iconInfo.fIcon=TRUE;iconInfo.hbmColor=color;iconInfo.hbmMask=mask;icon=CreateIconIndirect(&iconInfo);DeleteObject(mask);}
    DeleteObject(color);return icon;
}
}
