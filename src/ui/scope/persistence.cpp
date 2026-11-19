#include "openosc/ui/scope/persistence.hpp"
#include "openosc/ui/scope/model.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace openosc::plot {
void Persistence::clear() { std::memset(&pixels_,0,sizeof pixels_); duration_=0; }
void Persistence::advance(uint32_t now, uint16_t duration) {
    if (duration_ != duration) { clear(); duration_=duration; tickAt_=now; }
    if (!duration || duration==InfinitePersistence) return;
    const unsigned interval=(duration+13)/14;
    const unsigned ticks=uint32_t(now-tickAt_)/interval;
    if (!ticks) return;
    tickAt_ += ticks*interval;
    for (auto &channel:pixels_.finite) for (auto &pair:channel) {
        const unsigned low=pair&15, high=pair>>4;
        pair=uint8_t((low>ticks ? low-ticks : 0) | ((high>ticks ? high-ticks : 0)<<4));
    }
}
bool Persistence::pixel(unsigned c,unsigned x,unsigned y) const {
    if (x>=width_ || y>=height_ || c>=3) return false;
    if (duration_==InfinitePersistence) return pixels_.infinite[c][y*40+x/8] & (1U<<(x&7));
    return c<2 && (pixels_.finite[c][y*160+x/2] & (15U<<((x&1)*4)));
}
void Persistence::mark(unsigned c,unsigned x,unsigned y) {
    if (duration_==InfinitePersistence) pixels_.infinite[c][y*40+x/8] |= uint8_t(1U<<(x&7));
    else if (c<2) pixels_.finite[c][y*160+x/2] |= uint8_t(15U<<((x&1)*4));
}
void Persistence::resize(unsigned width,unsigned height) {
    width=std::min(width,MaximumWidth); height=std::min(height,MaximumHeight);
    if (width==width_ && height==height_) return;
    width_=width; height_=height; clear();
}
void Persistence::line(unsigned c,int x1,int y1,int x2,int y2) {
    if (c>=3 || !width_ || !height_) return;
    const int right=width_-1,bottom=height_-1;
    const auto code=[&](int x,int y) { return (x<0?1:x>right?2:0)|(y<0?4:y>bottom?8:0); };
    // Clip before Bresenham: heavily clipped traces never make an unbounded
    // off-screen walk and never wrap into another row/channel's bitmap.
    for (;;) {
        const int a=code(x1,y1),b=code(x2,y2);
        if (!(a|b)) break;
        if (a&b) return;
        const int out=a?a:b; int x,y;
        if (out&12) {
            y=out&4 ? 0 : bottom;
            x=x1+int64_t(x2-x1)*(y-y1)/(y2-y1);
        } else {
            x=out&1 ? 0 : right;
            y=y1+int64_t(y2-y1)*(x-x1)/(x2-x1);
        }
        if (a) { x1=x; y1=y; } else { x2=x; y2=y; }
    }
    const int dx=std::abs(x2-x1),dy=-std::abs(y2-y1),sx=x1<x2?1:-1,sy=y1<y2?1:-1;
    int error=dx+dy;
    for (;;) {
        mark(c,x1,y1);
        if (x1==x2 && y1==y2) break;
        const int twice=2*error;
        if (twice>=dy) { error+=dy; x1+=sx; }
        if (twice<=dx) { error+=dx; y1+=sy; }
    }
}
void Model::accumulate() {
    auto &p=storage_.persistence;
    p.resize(viewport_.width,viewport_.height);
    if (options_.mode==Mode::XY) {
        Point previous;
        for (unsigned i=0; i<xyCount_; ++i) {
            const auto point=storage_.xy[i];
            if (point.x==Missing || point.y==Missing) { previous={}; continue; }
            if (previous.x!=Missing && options_.style!=Style::Points) p.line(0,previous.x,previous.y,point.x,point.y);
            else p.line(0,point.x,point.y,point.x,point.y);
            previous=point;
        }
    } else for (unsigned c=0; c<3; ++c) {
        if (c==2 && options_.persistenceMs!=InfinitePersistence) continue;
        const auto &trace=storage_.traces[c];
        if (!trace.enabled) continue;
        int previous=Missing;
        for (unsigned x=0; x<viewport_.width; ++x) {
            const auto &column=trace.columns[x];
            if (column.first==Missing) { previous=Missing; continue; }
            if (options_.style==Style::Points) p.line(c,x,column.last,x,column.last);
            else {
                if (previous!=Missing) p.line(c,x-1,previous,x,column.first);
                p.line(c,x,std::min({column.first,column.a,column.b,column.last}),
                    x,std::max({column.first,column.a,column.b,column.last}));
            }
            previous=column.last;
        }
    }
}
} // namespace openosc::plot
