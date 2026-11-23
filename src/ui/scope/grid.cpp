#include "openosc/ui/scope/painter.hpp"

namespace openosc::plot {
void Painter::grid() {
    const auto &options = model_.options();
    const auto mode = options.grid;
    if (mode == Grid::None || !options.gridBrightness) // At 0 % the grid is the background: nothing to draw.
        return;
    const int w = lv_area_get_width(&area_) - 1, h = lv_area_get_height(&area_) - 1;
    // Its brightness (GitHub #86) is one opaque colour over the background, as the grid at 100 %.
    Path path(layer_, area_, ColorShade::toward(palette_.grid, palette_.background, options.gridBrightness), 255, 1,
              model_.statistics());
    if (mode != Grid::Axes) {
        for (unsigned i = 0; i <= 10; ++i) {
            const int x = i * w / 10;
            path.vertex(x, 0);
            path.vertex(x, h);
            path.gap();
        }
        for (unsigned i = 0; i <= 8; ++i) {
            const int y = i * h / 8;
            path.vertex(0, y);
            path.vertex(w, y);
            path.gap();
        }
    } else {
        path.vertex(0, h / 2);
        path.vertex(w, h / 2);
        path.gap();
        path.vertex(w / 2, 0);
        path.vertex(w / 2, h);
        path.gap();
    }
    if (mode == Grid::Full) {
        for (int i = 1; i < 50; ++i)
            if (i % 5) {
                const int x = i * w / 50;
                path.vertex(x, h / 2 - 2);
                path.vertex(x, h / 2 + 2);
                path.gap();
            }
        for (int i = 1; i < 40; ++i)
            if (i % 5) {
                const int y = i * h / 40;
                path.vertex(w / 2 - 2, y);
                path.vertex(w / 2 + 2, y);
                path.gap();
            }
    }
}
} // namespace openosc::plot
