#include "../src/tray_icon.hpp"
#include <vector>
#include <fstream>
#include <iostream>
using namespace mchose;
static int checks=0;
static void Check(bool value,const char* name){++checks;if(!value){std::cerr<<"FAIL: "<<name<<"\n";std::exit(1);}}
static std::vector<DWORD> ReadIcon(HICON icon,int size){
    ICONINFO info{};GetIconInfo(icon,&info);
    BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);format.bmiHeader.biWidth=size;format.bmiHeader.biHeight=-size;
    format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;format.bmiHeader.biCompression=BI_RGB;
    std::vector<DWORD> pixels(size*size);HDC dc=GetDC(nullptr);GetDIBits(dc,info.hbmColor,0,size,pixels.data(),&format,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);
    DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);return pixels;
}
int wmain(int argc,wchar_t** argv){
    Check(!MakeIcon(0,50,true,false,false)&&!MakeIcon(257,50,true,false,false),"invalid sizes rejected");
    for(int size:{16,20,24,32,48,64,256}){
        HICON icon=MakeIcon(size,82,true,false,false);Check(icon!=nullptr,"DPI size renders");
        auto pixels=ReadIcon(icon,size);Check((pixels[0]>>24)==0,"transparent corners");
        bool valid=true;for(auto pixel:pixels){unsigned a=pixel>>24;if(((pixel>>16)&255)>a||((pixel>>8)&255)>a||(pixel&255)>a)valid=false;}
        Check(valid,"alpha is premultiplied");DestroyIcon(icon);
    }
    HICON low=MakeIcon(32,24,true,false,false),high=MakeIcon(32,100,true,false,false),offline=MakeIcon(32,100,false,false,false);
    auto l=ReadIcon(low,32),h=ReadIcon(high,32),o=ReadIcon(offline,32);
    auto center=20*32+16;Check(l[center]!=h[center]&&h[center]!=o[center],"level and offline states differ");
    DestroyIcon(low);DestroyIcon(high);DestroyIcon(offline);
    auto before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<200;++i){HICON icon=MakeIcon(24,i%101,true,i%2,true);DestroyIcon(icon);}
    auto after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);Check(after==before,"repeated redraws release GDI objects");
    if(argc>1){
        constexpr int width=560,height=220;BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        format.bmiHeader.biWidth=width;format.bmiHeader.biHeight=-height;format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;
        void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&format,DIB_RGB_COLORS,&bits,nullptr,0);HDC dc=CreateCompatibleDC(nullptr);
        auto old=SelectObject(dc,bitmap);const int levels[]={0,24,25,50,100,82,82};
        for(int row=0;row<2;++row){RECT rect{0,row*110,width,(row+1)*110};HBRUSH bg=CreateSolidBrush(row?RGB(30,35,42):RGB(250,251,252));FillRect(dc,&rect,bg);DeleteObject(bg);
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,row?RGB(240,240,240):RGB(35,35,35));
            for(int col=0;col<7;++col){auto icon=MakeIcon(32,levels[col],col!=6,col==5,row!=0);
                DrawIconEx(dc,col*80+24,row*110+8,icon,32,32,0,nullptr,DI_NORMAL);
                DrawIconEx(dc,col*80+32,row*110+46,icon,16,16,0,nullptr,DI_NORMAL);DestroyIcon(icon);
                std::wstring label=col==6?L"offline":col==5?L"charge":std::to_wstring(levels[col])+L"%";TextOutW(dc,col*80+15,row*110+78,label.c_str(),int(label.size()));
            }
        }
        GdiFlush();BITMAPFILEHEADER file{};file.bfType=0x4D42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+width*height*4;
        std::ofstream out(fs::path(argv[1]),std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&format.bmiHeader),sizeof(BITMAPINFOHEADER));out.write(static_cast<char*>(bits),width*height*4);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    }
    std::cout<<"PASS: "<<checks<<" icon and GDI lifetime checks\n";
}
