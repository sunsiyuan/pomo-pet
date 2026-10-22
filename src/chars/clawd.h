// Clawd: broad clay body, two side arms, four short feet.
void drawClawd(const Pose& p) {
  pixel(2, 0, 11, 7, CLAY, p.ox, p.oy);
  pixel(2, 6, 11, 1, SHADE, p.ox, p.oy);
  int armY = p.happy ? 1 : 3;
  pixel(0, armY, 2, 2, CLAY, p.ox, p.oy);
  pixel(13, armY, 2, 2, CLAY, p.ox, p.oy);
  for (int x : {3, 5, 9, 11}) pixel(x, 7, 1, 2, CLAY, p.ox, p.oy);
  if (p.sleeping || p.blink) {
    screen.fillRect(p.ox + 28, p.oy + 25, 10, 3, INK);
    screen.fillRect(p.ox + 70, p.oy + 25, 10, 3, INK);
  } else if (p.happy) {
    for (int x : {p.ox + 28, p.ox + 70}) {
      screen.fillRect(x, p.oy + 18, 7, 3, INK);
      screen.fillRect(x - 3, p.oy + 21, 3, 5, INK);
      screen.fillRect(x + 7, p.oy + 21, 3, 5, INK);
    }
  } else {
    screen.fillRect(p.ox + 28 + p.look, p.oy + 14, 7, 14, INK);
    screen.fillRect(p.ox + 70 + p.look, p.oy + 14, 7, 14, INK);
  }
}
const Character CLAWD = {"CLAWD / POMO", CLAY, drawClawd};
