// Host-side render of src/chars/*.h for visual checks without flashing.
// Build: c++ -std=c++17 -I src tools/render_chars.cpp -o /tmp/render_chars
// Run:   /tmp/render_chars out.ppm   (rows = characters, columns = poses)
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

constexpr int W = 135, H = 150;
struct Canvas {
  std::vector<uint16_t> px = std::vector<uint16_t>(W * H);
  void fillRect(int x, int y, int w, int h, uint16_t c) {
    for (int j = std::max(y, 0); j < std::min(y + h, H); ++j)
      for (int i = std::max(x, 0); i < std::min(x + w, W); ++i) px[j * W + i] = c;
  }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    auto edge = [](int ax, int ay, int bx, int by, int x, int y) {
      return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
    };
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        int a = edge(x0, y0, x1, y1, x, y), b = edge(x1, y1, x2, y2, x, y),
            d = edge(x2, y2, x0, y0, x, y);
        if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) px[y * W + x] = c;
      }
  }
} screen;
const uint16_t TFT_WHITE = 0xFFFF, BG = 0xF75B, INK = 0x31A6, CLAY = 0xDC0D,
               SHADE = 0xBB2B, SOFT = 0xE6B7;
struct Pose { int ox, oy, look; bool sleeping, happy, blink; };
struct Character { const char* title; uint16_t hand; void (*draw)(const Pose&); };
void pixel(int x, int y, int w, int h, uint16_t c, int ox, int oy) {
  screen.fillRect(ox + x * 7, oy + y * 7, w * 7, h * 7, c);
}
#include "chars/clawd.h"
#if __has_include("chars/pikachu.h")
#include "chars/pikachu.h"
#endif
#if __has_include("chars/gengar.h")
#include "chars/gengar.h"
#endif
#if __has_include("chars/snorlax.h")
#include "chars/snorlax.h"
#endif
#if __has_include("chars/totoro.h")
#include "chars/totoro.h"
#endif

int main(int argc, char** argv) {
  std::vector<const Character*> chars = {&CLAWD};
#if __has_include("chars/pikachu.h")
  chars.push_back(&PIKACHU);
#endif
#if __has_include("chars/gengar.h")
  chars.push_back(&GENGAR);
#endif
#if __has_include("chars/snorlax.h")
  chars.push_back(&SNORLAX);
#endif
#if __has_include("chars/totoro.h")
  chars.push_back(&TOTORO);
#endif
  // Same placement as pet(): oy 78 on screen; this canvas starts at screen y 40.
  const Pose poses[] = {{15, 38, 0, false, false, false}, {15, 38, 2, false, false, false},
                        {15, 38, 0, true, false, false}, {15, 32, 0, false, true, false}};
  const int cols = 4, rows = chars.size();
  std::vector<uint16_t> sheet(cols * W * rows * H);
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      screen.fillRect(0, 0, W, H, BG);
      screen.fillRect(0, 5, W, 1, SOFT);  // Screen y 45: bottom of the status label.
      screen.fillRect(32 + 0, 102, 72, 4, SOFT);  // Desk shadow (screen y 142).
      chars[r]->draw(poses[c]);
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
          sheet[(r * H + y) * cols * W + c * W + x] = screen.px[y * W + x];
    }
  FILE* f = fopen(argc > 1 ? argv[1] : "chars.ppm", "wb");
  fprintf(f, "P6 %d %d 255\n", cols * W, rows * H);
  for (uint16_t v : sheet) {
    unsigned char rgb[3] = {(unsigned char)((v >> 11) * 255 / 31),
                            (unsigned char)(((v >> 5) & 63) * 255 / 63),
                            (unsigned char)((v & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
