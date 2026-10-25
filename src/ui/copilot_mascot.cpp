#include "copilot_mascot.h"

#include "theme.h"

// Original pixel-art pilot inspired by the Copilot icon: helmet, goggles, glowing face screen.
// One grid cell is CELL x CELL pixels. Right half is the mirror of the left, with shading flipped.
static constexpr int16_t CELL = 5;
static constexpr int16_t HEAD_COLS = 28;
static constexpr int16_t HEAD_ROWS = 23;
static constexpr int16_t HEAD_W = HEAD_COLS * CELL;
static constexpr int16_t HEAD_X = (MASCOT_W - HEAD_W) / 2;
static constexpr int16_t HEAD_Y = 30;

static const char HEAD[HEAD_ROWS][HEAD_COLS + 1] = {
  "..........KKKKKKKK..........",
  "........KKLLHHHHhhKK........",
  "......KKLLHHHHHHHHhhKK......",
  ".....KLLHHHHHHHHHHHHhhK.....",
  "....KLHHHHHHHHHHHHHHHHhK....",
  "....KHHKKKKKHHHHKKKKKHHK....",
  "...KLHKWWWWWKHHKWWWWWKHhK...",
  "...KHKWBbBBBWKKWBBBBBWKHK...",
  "..KKHKWbBBBBWKKWBBBBBWKHKK..",
  ".KWKHKWBBBBBWKKWBBBBBWKHKWK.",
  ".KWKHHKWWWWWKHHKWWWWWKHHKWK.",
  ".KWKHHHKKKKKHHHHKKKKKHHHKWK.",
  "..KKHHHHHHHHHHHHHHHHHHHHKK..",
  "..KHHKKKKKKKKKKKKKKKKKKHHK..",
  "..KHKFFFFFFFFFFFFFFFFFFKHK..",
  "..KHKFFFFFFFFFFFFFFFFFFKHK..",
  "..KHKFFFFFFFFFFFFFFFFFFKHK..",
  "..KHKFFFFFFFFFFFFFFFFFFKHK..",
  "..KHKFFFFFFFFFFFFFFFFFFKHK..",
  "..KHHKFFFFFFFFFFFFFFFFKHHK..",
  "..KhHHKKKKKKKKKKKKKKKKHHhK..",
  "...KhhHHHHHHHHHHHHHHHHhhK...",
  ".....KKKKKKKKKKKKKKKKKK.....",
};

struct Palette
{
  uint16_t outline, head, shade, light, frame, lens, glint, face, glow;
};

static constexpr Palette NORMAL = {
  rgb565(27, 16, 48), rgb565(137, 87, 229), rgb565(102, 57, 186), rgb565(180, 142, 247),
  rgb565(230, 237, 243), rgb565(47, 129, 247), rgb565(205, 228, 255), rgb565(13, 17, 23),
  rgb565(126, 231, 255)};

static constexpr Palette BURNT = {
  rgb565(20, 14, 30), rgb565(91, 62, 143), rgb565(67, 48, 106), rgb565(122, 98, 168),
  rgb565(139, 148, 158), rgb565(48, 54, 61), rgb565(72, 79, 88), rgb565(22, 27, 34),
  rgb565(110, 118, 129)};

static constexpr uint16_t LENS_ALARM = rgb565(248, 81, 73);
static constexpr uint16_t FACE_ALARM = rgb565(45, 15, 18);
static constexpr uint16_t BLUSH = rgb565(247, 120, 186);
static constexpr uint16_t SWEAT = rgb565(121, 192, 255);
static constexpr uint16_t SPARKLE = rgb565(255, 211, 61);
static constexpr uint16_t SMOKE_LIGHT = rgb565(201, 209, 217);
static constexpr uint16_t SMOKE_MID = rgb565(150, 158, 168);
static constexpr uint16_t SMOKE_DARK = rgb565(105, 112, 122);
static constexpr uint16_t EYE_SHINE = rgb565(255, 255, 255);

// Head-relative painter; all feature coordinates below are in head pixels (0..HEAD_W-1).
struct Painter
{
  TFT_eSprite &canvas;
  int16_t ox;
  int16_t oy;

  void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) const
  {
    canvas.fillRect(ox + x, oy + y, w, h, color);
  }

  // Draws the rect and its mirror image across the head's vertical centre line.
  void sym(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) const
  {
    rect(x, y, w, h, color);
    rect(HEAD_W - x - w, y, w, h, color);
  }
};

static uint16_t gridColor(char ch, const Palette &pal, uint16_t lens, uint16_t face)
{
  switch (ch)
  {
  case 'K': return pal.outline;
  case 'H': return pal.head;
  case 'h': return pal.shade;
  case 'L': return pal.light;
  case 'W': return pal.frame;
  case 'B': return lens;
  case 'b': return pal.glint;
  case 'F': return face;
  default: return pal.head;
  }
}

static void drawHead(const Painter &p, const Palette &pal, uint16_t lens, uint16_t face)
{
  for (int16_t row = 0; row < HEAD_ROWS; ++row)
  {
    int16_t col = 0;
    while (col < HEAD_COLS)
    {
      const char ch = HEAD[row][col];
      int16_t end = col + 1;
      while (end < HEAD_COLS && HEAD[row][end] == ch)
        ++end;
      if (ch != '.')
        p.rect(col * CELL, row * CELL, (end - col) * CELL, CELL, gridColor(ch, pal, lens, face));
      col = end;
    }
  }
}

// Eye boxes: left eye spans x 43..52, right eye mirrors it. Face screen interior is x 25..114, y 70..99.
static void pillEyes(const Painter &p, int16_t top, int16_t height, uint16_t color, bool shine)
{
  p.sym(45, top, 6, height, color);
  if (height > 4)
    p.sym(43, top + 2, 10, height - 4, color);
  else
    p.sym(43, top, 10, height, color);
  if (shine && height >= 9)
  {
    p.rect(45, top + 2, 3, 3, EYE_SHINE);
    p.rect(HEAD_W - 53 + 2, top + 2, 3, 3, EYE_SHINE);
  }
}

static bool blinking(uint32_t frame, uint32_t period, uint32_t offset = 0)
{
  return (frame + offset) % period < 2;
}

static void sweatDrop(TFT_eSprite &c, int16_t x, int16_t y)
{
  c.fillRect(x + 2, y, 2, 2, SWEAT);
  c.fillRect(x + 1, y + 2, 4, 2, SWEAT);
  c.fillRect(x, y + 4, 6, 5, SWEAT);
  c.fillRect(x + 1, y + 9, 4, 2, SWEAT);
  c.fillRect(x + 1, y + 5, 2, 2, EYE_SHINE);
}

static void sparkle(TFT_eSprite &c, int16_t x, int16_t y, int16_t size, uint16_t color)
{
  c.fillRect(x - 1, y - size, 2, size * 2, color);
  c.fillRect(x - size, y - 1, size * 2, 2, color);
  if (size > 3)
    c.fillRect(x - 2, y - 2, 4, 4, color);
}

static void exclamation(TFT_eSprite &c, int16_t x, int16_t y, uint16_t color)
{
  c.fillRect(x, y, 5, 13, color);
  c.fillRect(x, y + 16, 5, 5, color);
}

static void smokePuff(TFT_eSprite &c, int16_t x, int16_t y, int16_t r, uint16_t color, uint16_t highlight)
{
  c.fillRect(x - r, y - r / 2, r * 2, r, color);
  c.fillRect(x - r / 2, y - r, r, r * 2, color);
  const int16_t hr = r / 2;
  if (hr >= 2)
  {
    c.fillRect(x - r / 2 - hr / 2, y - r / 2 - hr / 2, hr, hr, highlight);
  }
}

static void drawSmoke(TFT_eSprite &c, const Painter &p, uint32_t frame)
{
  // Puffs pour out of both eyes (and a little from the helmet), growing and darkening as they rise.
  static const int16_t SOURCES[][2] = {{48, 82}, {92, 82}, {48, 82}, {92, 82}, {70, 6}, {48, 82}, {92, 82}};
  static const int8_t DRIFT[] = {0, 1, 2, 2, 1, 0, -1, -2, -2, -1};
  constexpr uint8_t PUFFS = sizeof(SOURCES) / sizeof(SOURCES[0]);
  constexpr uint32_t LIFE = 42;
  for (uint8_t i = 0; i < PUFFS; ++i)
  {
    const uint32_t t = (frame + i * (LIFE / PUFFS)) % LIFE;
    const int16_t spread = (i % 2 ? 1 : -1) * static_cast<int16_t>(t / 3);
    const int16_t x = p.ox + SOURCES[i][0] + DRIFT[(t / 3 + i) % 10] * 2 + spread;
    const int16_t y = p.oy + SOURCES[i][1] - static_cast<int16_t>(t * 3);
    const int16_t r = 4 + static_cast<int16_t>(t / 4);
    uint16_t color = SMOKE_LIGHT;
    uint16_t highlight = EYE_SHINE;
    if (t >= LIFE * 3 / 4)
    {
      color = SMOKE_DARK;
      highlight = SMOKE_MID;
    }
    else if (t >= LIFE / 2)
    {
      color = SMOKE_MID;
      highlight = SMOKE_LIGHT;
    }
    if (y + r > 0)
      smokePuff(c, x, y, r, color, highlight);
  }
}

static void drawHappy(TFT_eSprite &c, const Painter &p, const Palette &pal, uint32_t frame)
{
  // ^ ^ eyes, blush and a wide smile
  p.sym(39, 84, 4, 4, pal.glow);
  p.sym(42, 80, 4, 4, pal.glow);
  p.sym(45, 77, 6, 4, pal.glow);
  p.sym(50, 80, 4, 4, pal.glow);
  p.sym(53, 84, 4, 4, pal.glow);
  p.sym(30, 90, 8, 4, BLUSH);
  p.sym(60, 88, 4, 4, pal.glow);
  p.rect(63, 92, 14, 4, pal.glow);

  static const int16_t SPOTS[][2] = {{14, 22}, {136, 30}, {8, 104}, {142, 118}};
  for (uint8_t i = 0; i < 4; ++i)
  {
    const uint32_t phase = (frame / 2 + i * 3) % 12;
    if (phase < 6)
    {
      const int16_t size = phase < 3 ? 2 + phase : 7 - phase;
      sparkle(c, SPOTS[i][0], SPOTS[i][1], size, i % 2 ? SPARKLE : EYE_SHINE);
    }
  }
}

static void drawCruising(const Painter &p, const Palette &pal, uint32_t frame)
{
  if (blinking(frame, 42))
    p.sym(43, 84, 10, 4, pal.glow);
  else
    pillEyes(p, 75, 15, pal.glow, true);
  p.sym(62, 90, 4, 3, pal.glow);
  p.rect(65, 93, 10, 3, pal.glow);
}

static void drawFocused(TFT_eSprite &c, const Painter &p, const Palette &pal, uint32_t frame)
{
  // Brows angled down towards the centre: determined.
  p.sym(38, 72, 6, 3, pal.glow);
  p.sym(44, 74, 6, 3, pal.glow);
  p.sym(50, 76, 5, 3, pal.glow);
  if (blinking(frame, 48, 20))
    p.sym(43, 84, 10, 3, pal.glow);
  else
    pillEyes(p, 80, 9, pal.glow, true);
  p.rect(62, 92, 16, 3, pal.glow);

  const uint32_t t = frame % 30;
  if (t < 24)
    sweatDrop(c, p.ox + 116, p.oy + 14 + static_cast<int16_t>(t * 3 / 2));
}

static void drawWorried(TFT_eSprite &c, const Painter &p, const Palette &pal, uint32_t frame)
{
  // Brows raised towards the centre: anxious.
  p.sym(38, 76, 6, 3, pal.glow);
  p.sym(44, 74, 6, 3, pal.glow);
  p.sym(50, 72, 5, 3, pal.glow);
  if (blinking(frame, 30, 7))
  {
    p.sym(43, 85, 10, 3, pal.glow);
  }
  else
  {
    pillEyes(p, 79, 11, pal.glow, false);
    static const int8_t GLANCE[] = {0, 0, -2, -2, 0, 0, 2, 2};
    const int16_t dx = GLANCE[(frame / 4) % 8];
    p.rect(46 + dx, 82, 4, 5, pal.outline);
    p.rect(HEAD_W - 50 + dx, 82, 4, 5, pal.outline);
  }
  for (int16_t i = 0; i < 5; ++i)
    p.rect(60 + i * 4, (i % 2) ? 90 : 93, 4, 3, pal.glow);

  const uint32_t t = frame % 20;
  if (t < 16)
  {
    sweatDrop(c, p.ox + 116, p.oy + 12 + static_cast<int16_t>(t * 2));
    sweatDrop(c, p.ox + HEAD_W - 122, p.oy + 22 + static_cast<int16_t>(((t + 8) % 16) * 2));
  }
}

static void drawPanic(TFT_eSprite &c, const Painter &p, const Palette &pal, uint32_t frame)
{
  // O O eyes and an open mouth
  p.sym(40, 75, 16, 15, pal.glow);
  p.sym(44, 79, 8, 7, pal.face);
  p.sym(46, 81, 3, 3, EYE_SHINE);
  p.rect(63, 89, 14, 10, pal.glow);
  p.rect(66, 92, 8, 5, pal.face);

  if ((frame / 3) % 2 == 0)
  {
    exclamation(c, 12, 4, LENS_ALARM);
    exclamation(c, MASCOT_W - 17, 4, LENS_ALARM);
  }
  const uint32_t t = frame % 12;
  sweatDrop(c, p.ox + 118 + static_cast<int16_t>(t), p.oy + 18 - static_cast<int16_t>(t) + static_cast<int16_t>(t * t / 6));
  sweatDrop(c, p.ox + HEAD_W - 124 - static_cast<int16_t>(t), p.oy + 18 - static_cast<int16_t>(t) + static_cast<int16_t>(t * t / 6));
}

static void drawBurntOut(TFT_eSprite &c, const Painter &p, const Palette &pal, uint32_t frame)
{
  // Grey X eyes with smoke pouring out of them
  p.sym(40, 76, 4, 4, pal.glow);
  p.sym(50, 76, 4, 4, pal.glow);
  p.sym(44, 80, 6, 4, pal.glow);
  p.sym(40, 84, 4, 4, pal.glow);
  p.sym(50, 84, 4, 4, pal.glow);
  p.rect(60, 94, 5, 3, pal.glow);
  p.rect(65, 92, 5, 3, pal.glow);
  p.rect(70, 94, 5, 3, pal.glow);
  p.rect(75, 92, 5, 3, pal.glow);

  // Cracked left lens
  p.rect(38, 36, 2, 4, EYE_SHINE);
  p.rect(40, 40, 2, 3, EYE_SHINE);
  p.rect(42, 42, 4, 2, EYE_SHINE);
  p.rect(46, 44, 2, 4, EYE_SHINE);
  p.rect(48, 41, 3, 2, EYE_SHINE);

  drawSmoke(c, p, frame);
}

void mascotRender(TFT_eSprite &canvas, Mood mood, uint32_t frame, uint16_t background)
{
  static const int8_t BOB[] = {0, 1, 2, 3, 3, 2, 1, 0};

  int16_t dx = 0;
  int16_t dy = 0;
  const Palette *pal = &NORMAL;
  uint16_t lens = NORMAL.lens;
  uint16_t face = NORMAL.face;

  switch (mood)
  {
  case Mood::Happy: dy = -BOB[(frame / 2) % 8]; break;
  case Mood::Cruising: dy = -BOB[(frame / 3) % 8]; break;
  case Mood::Focused: dy = -BOB[(frame / 4) % 8] / 2; break;
  case Mood::Worried: dy = -BOB[frame % 8] / 2; dx = (frame % 16 < 2) ? 1 : 0; break;
  case Mood::Panic:
    dx = (frame % 2) ? 3 : -3;
    dy = -BOB[(frame * 2) % 8] / 2;
    if ((frame / 3) % 2 == 0)
    {
      lens = LENS_ALARM;
      face = FACE_ALARM;
    }
    break;
  case Mood::BurntOut:
    pal = &BURNT;
    lens = BURNT.lens;
    face = BURNT.face;
    dy = 4;
    break;
  }

  canvas.fillSprite(background);
  const Painter p{canvas, static_cast<int16_t>(HEAD_X + dx), static_cast<int16_t>(HEAD_Y + dy)};
  drawHead(p, *pal, lens, face);

  switch (mood)
  {
  case Mood::Happy: drawHappy(canvas, p, *pal, frame); break;
  case Mood::Cruising: drawCruising(p, *pal, frame); break;
  case Mood::Focused: drawFocused(canvas, p, *pal, frame); break;
  case Mood::Worried: drawWorried(canvas, p, *pal, frame); break;
  case Mood::Panic: drawPanic(canvas, p, *pal, frame); break;
  case Mood::BurntOut: drawBurntOut(canvas, p, *pal, frame); break;
  }
}
