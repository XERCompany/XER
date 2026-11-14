#include "retouch_tool.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace image {

void RetouchTool::CloneStamp(ImageBuffer& img,
                              int sx, int sy, int dx, int dy, int radius) {
    if (!img.IsValid()) return;
    int w=img.Width(), h=img.Height();
    int bpp=BytesPerPixel(img.Format());
    uint8_t* d=img.Data();
    for (int oy=-radius;oy<=radius;++oy)
        for (int ox=-radius;ox<=radius;++ox) {
            if (ox*ox+oy*oy > radius*radius) continue;
            int tx=dx+ox, ty=dy+oy, fx=sx+ox, fy=sy+oy;
            if (tx<0||tx>=w||ty<0||ty>=h) continue;
            if (fx<0||fx>=w||fy<0||fy>=h) continue;
            std::memcpy(d+(ty*w+tx)*bpp, d+(fy*w+fx)*bpp, bpp);
        }
}

void RetouchTool::SpotRemove(ImageBuffer& img, int cx, int cy, int radius) {
    if (!img.IsValid()) return;
    int w=img.Width(), h=img.Height();
    uint8_t* d=img.Data();
    // Compute average color of border ring
    uint64_t sr=0,sg=0,sb=0,sa=0; int cnt=0;
    for (int oy=-radius-1;oy<=radius+1;++oy)
        for (int ox=-radius-1;ox<=radius+1;++ox) {
            int r2=ox*ox+oy*oy;
            int or2=(radius+1)*(radius+1);
            if (r2>or2 || r2<=(radius)*(radius)) continue;
            int px=cx+ox, py=cy+oy;
            if (px<0||px>=w||py<0||py>=h) continue;
            sr+=d[(py*w+px)*4+0]; sg+=d[(py*w+px)*4+1];
            sb+=d[(py*w+px)*4+2]; sa+=d[(py*w+px)*4+3];
            ++cnt;
        }
    if (!cnt) return;
    uint8_t ar=static_cast<uint8_t>(sr/cnt), ag=static_cast<uint8_t>(sg/cnt),
            ab=static_cast<uint8_t>(sb/cnt), aa=static_cast<uint8_t>(sa/cnt);
    for (int oy=-radius;oy<=radius;++oy)
        for (int ox=-radius;ox<=radius;++ox) {
            if (ox*ox+oy*oy>radius*radius) continue;
            int px=cx+ox, py=cy+oy;
            if (px<0||px>=w||py<0||py>=h) continue;
            d[(py*w+px)*4+0]=ar; d[(py*w+px)*4+1]=ag;
            d[(py*w+px)*4+2]=ab; d[(py*w+px)*4+3]=aa;
        }
}

void RetouchTool::HealingBrush(ImageBuffer& img,
                                int sx, int sy, int dx, int dy, int radius) {
    // Simple: clone + blend at 50% alpha with destination
    if (!img.IsValid()) return;
    int w=img.Width(), h=img.Height();
    uint8_t* d=img.Data();
    for (int oy=-radius;oy<=radius;++oy)
        for (int ox=-radius;ox<=radius;++ox) {
            if (ox*ox+oy*oy>radius*radius) continue;
            int tx=dx+ox,ty=dy+oy,fx=sx+ox,fy=sy+oy;
            if (tx<0||tx>=w||ty<0||ty>=h||fx<0||fx>=w||fy<0||fy>=h) continue;
            for (int c=0;c<4;++c)
                d[(ty*w+tx)*4+c]=(d[(ty*w+tx)*4+c]+d[(fy*w+fx)*4+c])/2;
        }
}

void RetouchTool::Inpaint(ImageBuffer& img, const ImageBuffer& mask, int iter) {
    if (!img.IsValid() || !mask.IsValid()) return;
    int w=img.Width(), h=img.Height();
    const uint8_t* m=mask.Data();
    uint8_t* d=img.Data();
    for (int it=0;it<iter;++it)
        for (int y=1;y<h-1;++y)
            for (int x=1;x<w-1;++x) {
                if (m[(y*w+x)*4] < 128) continue;
                for (int c=0;c<3;++c)
                    d[(y*w+x)*4+c]=(d[((y-1)*w+x)*4+c]+d[((y+1)*w+x)*4+c]+
                                    d[(y*w+x-1)*4+c]+d[(y*w+x+1)*4+c])/4;
            }
}

}  // namespace image
