#include "openosc/ui/scope/painter.hpp"

namespace openosc::plot {
void Painter::persistence() {
    const auto &o=model_.options();
    const auto &viewport=model_.viewport();
    const auto &bitmap=model_.storage().persistence;
    for (unsigned c=0; c<(o.mode==Mode::XY ? 1U : 3U); ++c) {
        if (c<2 && !o.channels[c].visible) continue;
        if (c==2 && (o.math==Math::Off || o.persistenceMs!=InfinitePersistence)) continue;
        Path path(layer_,area_,c<2 ? palette_.channels[c] : palette_.math,
            c<2 ? o.channels[c].opacity/3 : 85,1,model_.statistics());
        for (unsigned y=0; y<viewport.height; ++y) {
            unsigned x=0;
            while (x<viewport.width) {
                while (x<viewport.width && !bitmap.pixel(c,x,y)) ++x;
                if (x==viewport.width) break;
                const unsigned start=x++;
                while (x<viewport.width && bitmap.pixel(c,x,y)) ++x;
                path.vertex(start,y); path.vertex(x,y); path.gap();
            }
        }
    }
}
} // namespace openosc::plot
