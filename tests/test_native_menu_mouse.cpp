#include "runtime/native_menu_mouse.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int failures=0;
void check(bool result,const char* name) {
    if(!result){std::fprintf(stderr,"FAIL: %s\n",name);++failures;}
}
bool near(double a,double b){return std::abs(a-b)<1e-8;}
}
int main() {
    using namespace Simpsons;
    check(nativeMenuMouseEvent(0,1,0)==-1,"no menu suppresses input");
    check(nativeMenuMouseEvent(1,1,0)==-1,"blank space cannot accept previous selection");
    check(nativeMenuMouseEvent(106,1,0)==6,"row accepts original selection");
    check(nativeMenuMouseEvent(107,1,0)==7,"cancel footer uses back");
    check(nativeMenuMouseEvent(102,1,0)==2,"left half of editable row decreases");
    check(nativeMenuMouseEvent(103,1,0)==3,"right half of editable row increases");
    check(nativeMenuMouseEvent(110,1,0)==10,"title click uses Start");
    check(nativeMenuMouseEvent(113,1,0)==13,"delete footer uses X");
    check(nativeMenuMouseEvent(1,2,0)==7,"right click backs out from blank menu space");
    check(nativeMenuMouseEvent(103,3,0)==7,"simultaneous clicks prioritize back");
    check(nativeMenuMouseEvent(102,0,120)==3,"wheel up increases editable value");
    check(nativeMenuMouseEvent(103,0,-120)==2,"wheel down decreases editable value");
    check(nativeMenuMouseEvent(106,0,120)==4,"wheel navigates menu up");
    check(nativeMenuMouseEvent(1,0,-120)==5,"wheel navigates down from blank space");
    // Presentation fits the entire frozen scene before Apt's centered 16:9 UI.
    const auto normal=nativeMenuPoint(640,360,1280,720,512,448);
    check(normal.inside&&near(normal.x,256)&&near(normal.y,224),"normal viewport center");
    const auto resized=nativeMenuPoint(960,540,1920,1080,512,448);
    check(resized.inside&&near(resized.x,normal.x)&&near(resized.y,normal.y),"client resize retains authored point");
    const auto wideCenter=nativeMenuPoint(640,360,1280,720,512,448,32.0/9.0);
    check(wideCenter.inside&&near(wideCenter.x,256)&&near(wideCenter.y,224),"ultrawide scene center");
    const auto wideEdge=nativeMenuPoint(320,180,1280,720,512,448,32.0/9.0);
    check(wideEdge.inside&&near(wideEdge.x,0)&&near(wideEdge.y,0),"ultrawide scene UI top left");
    check(!nativeMenuPoint(319,360,1280,720,512,448,32.0/9.0).inside,"ultrawide UI left bar rejected");
    check(!nativeMenuPoint(960,360,1280,720,512,448,32.0/9.0).inside,"ultrawide UI right edge excluded");
    check(!nativeMenuPoint(640,179,1280,720,512,448,32.0/9.0).inside,"presentation top bar rejected");
    const auto narrow=nativeMenuPoint(640,360,1280,720,512,448,4.0/3.0);
    check(narrow.inside&&near(narrow.x,256)&&near(narrow.y,224),"narrow render target center");
    check(!nativeMenuPoint(159,360,1280,720,512,448,4.0/3.0).inside,"narrow scene presentation side bar rejected");
    check(!nativeMenuPoint(640,89,1280,720,512,448,4.0/3.0).inside,"narrow scene UI top bar rejected");
    const auto portrait=nativeMenuPoint(540,960,1080,1920,512,448);
    check(portrait.inside&&near(portrait.x,256)&&near(portrait.y,224),"portrait client center");
    check(!nativeMenuPoint(540,100,1080,1920,512,448).inside,"portrait letterbox rejected");
    check(!nativeMenuPoint(-1,100,1280,720,512,448).inside,"negative client coordinate");
    check(!nativeMenuPoint(1280,100,1280,720,512,448).inside,"client right boundary");
    check(!nativeMenuPoint(640,720,1280,720,512,448).inside,"client bottom boundary");
    check(!nativeMenuPoint(640,360,0,720,512,448).inside,"zero client extent");
    check(!nativeMenuPoint(640,360,1280,720,0,448).inside,"zero authored extent");
    check(!nativeMenuPoint(640,360,1280,720,512,448,std::numeric_limits<double>::infinity()).inside,"invalid render aspect");
    return failures?1:0;
}
