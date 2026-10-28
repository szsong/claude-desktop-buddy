#include "../buddy.h"
#include "../buddy_common.h"
#include "../hw.h"
#include <string.h>

extern TFT_eSprite spr;

// Clawd, the Claude Code mascot, drawn solid like the block-character logo:
//
//    ▐▛███▜▌        ..############..     one cell of this grid is one quarter
//   ▝▜█████▛▘   →   ..##o######o##..     of a block character (o = eye hole)
//     ▘▘ ▝▝         ################
//                   ..############..
//                   ...#.#....#.#...
//
// A terminal cell is twice as tall as it is wide, so a quarter is 4 x 8 px:
// 16 x 5 quarters = 64 x 40 px (doubled at 2x). Eyes and mouth are holes cut
// out of the body, so expressions can change their shape.
namespace clawd {

static const uint16_t ORANGE = 0xDBAA;   // Claude orange, #DA7756

enum Arms { A_OUT, A_UP, A_DOWN, A_WAVE_A, A_WAVE_B, A_TYPE_A, A_TYPE_B,
            A_HUG, A_FLAIL_A, A_FLAIL_B, A_LEFT_UP };
enum Legs { L_STAND, L_STEP_A, L_STEP_B, L_TUCK, L_KICK, L_WOBBLE };
enum Eye  { E_OPEN, E_BLINK, E_SHUT, E_HAPPY, E_WIDE, E_LEFT, E_RIGHT, E_UP,
            E_DOWN, E_CROSS, E_SPIRAL, E_HEART };
enum Mouth { M_NONE, M_SMILE, M_OPEN, M_FLAT, M_SMALL, M_WAVY };

struct Pose {
  uint8_t arms, legs, eyeL, eyeR, mouth, blush;
  int8_t dx, dy;    // body offset in px (TUCK poses sit 6 px lower)
};

// Arms: up to two quarters per side, as {col, row}; {-1,-1} = unused.
static const int8_t ARMS[][4][2] = {
  /* OUT     */ { {0,2}, {1,2}, {14,2}, {15,2} },
  /* UP      */ { {0,0}, {1,1}, {14,1}, {15,0} },
  /* DOWN    */ { {1,2}, {1,3}, {14,2}, {14,3} },
  /* WAVE_A  */ { {0,2}, {1,2}, {14,1}, {15,0} },
  /* WAVE_B  */ { {0,2}, {1,2}, {14,1}, {15,1} },
  /* TYPE_A  */ { {0,3}, {1,3}, {14,2}, {15,2} },
  /* TYPE_B  */ { {0,2}, {1,2}, {14,3}, {15,3} },
  /* HUG     */ { {1,2}, {-1,-1}, {14,2}, {-1,-1} },
  /* FLAIL_A */ { {0,0}, {1,1}, {14,2}, {14,3} },
  /* FLAIL_B */ { {1,2}, {1,3}, {14,1}, {15,0} },
  /* LEFT_UP */ { {0,0}, {1,1}, {14,2}, {15,2} },
};
// Legs: the four columns on the bottom row; -1 = none.
static const int8_t LEGS[][4] = {
  /* STAND  */ { 3, 5, 10, 12 },
  /* STEP_A */ { 2, 4, 11, 13 },
  /* STEP_B */ { 4, 6,  9, 11 },
  /* TUCK   */ { -1, -1, -1, -1 },
  /* KICK   */ { 1, 3, 12, 14 },
  /* WOBBLE */ { 4, 5, 10, 11 },
};

// Eyes: '#' = hole, drawn from the eye quarter's top-left plus an offset.
// At rest an eye is the whole 4 x 8 quarter, a tall slit like the logo's.
#define SLIT "####", "####", "####", "####", "####", "####", "####", "####"
struct EyeArt { int8_t dx, dy; const char* rows[10]; };
static const EyeArt EYES[] = {
  /* OPEN   */ {  0,  0, { SLIT } },
  /* BLINK  */ {  0,  4, { "####", "####" } },
  /* SHUT   */ {  0,  5, { "####", "####" } },
  /* HAPPY  */ {  0,  2, { ".##.", "####", "#..#", "#..#" } },          // ^ ^
  /* WIDE   */ { -1, -2, { "######", "######", "######", "######", "######",
                           "######", "######", "######", "######", "######" } },
  /* LEFT   */ { -2,  0, { SLIT } },
  /* RIGHT  */ {  2,  0, { SLIT } },
  /* UP     */ {  0, -2, { SLIT } },
  /* DOWN   */ {  0,  2, { SLIT } },
  /* CROSS  */ {  0,  1, { "#..#", "#..#", ".##.", ".##.", "#..#", "#..#" } },
  /* SPIRAL */ {  0,  0, { "####", "#..#", "#..#", "#..#", "#..#", "#..#", "#..#", "####" } },
  /* HEART  */ {  0,  2, { ".#.#.", "#####", "#####", ".###.", "..#.." } },
};
#undef SLIT
// The mouth (Clawd has none at rest) is a hole centred under the eyes.
struct MouthArt { int8_t x, y; const char* rows[4]; };
static const MouthArt MOUTHS[] = {
  /* NONE  */ {  0,  0, { nullptr } },
  /* SMILE */ { 28, 17, { "#......#", "##....##", ".######." } },
  /* OPEN  */ { 29, 16, { ".####.", "######", "######", ".####." } },
  /* FLAT  */ { 29, 18, { "######" } },
  /* SMALL */ { 30, 17, { ".##.", "####", ".##." } },
  /* WAVY  */ { 27, 17, { "##..##..##", "..##..##.." } },
};

static int U, OX, OY;   // px per design pixel (the pet's scale); top-left vs the pet anchor

static void rect(int x, int y, int w, int h, uint16_t c) {
  buddyFillRect(OX + x * U, OY + y * U, w * U, h * U, c);
}
static void quarter(int col, int row) { rect(col * 4, row * 8, 4, 8, ORANGE); }
static void holes(int x0, int y0, const char* const* rows, int n) {
  for (int y = 0; y < n && rows[y]; y++)
    for (int x = 0; rows[y][x]; x++)
      if (rows[y][x] == '#') rect(x0 + x, y0 + y, 1, 1, BUDDY_BG);
}

static void draw(const Pose& p, int dx = 0, int dy = 0) {
  U = buddyScale();
  OX = (p.dx + dx - 32) * U;
  OY = (p.dy + dy + 6) * U;

  rect(8, 0, 48, 32, ORANGE);                       // the body: quarters 2..13 x 0..3
  for (auto& a : ARMS[p.arms]) if (a[0] >= 0) quarter(a[0], a[1]);
  for (int8_t c : LEGS[p.legs]) if (c >= 0) quarter(c, 4);

  const EyeArt& l = EYES[p.eyeL];
  const EyeArt& r = EYES[p.eyeR];
  holes(16 + l.dx, 8 + l.dy, l.rows, 10);           // eye quarters: columns 4 and 11, row 1
  holes(44 + r.dx, 8 + r.dy, r.rows, 10);
  const MouthArt& m = MOUTHS[p.mouth];
  if (m.rows[0]) holes(m.x, m.y, m.rows, 4);
  if (p.blush) { rect(10, 18, 4, 2, BUDDY_HEART); rect(50, 18, 4, 2, BUDDY_HEART); }
}

// Particles go just above the head (text coordinates, like the other species).
static const int HEAD_Y = BUDDY_Y_BASE - 12;

// ─── SLEEP ───  ~24s cycle, 6 poses: sits down, snores, stirs, peeks
static void doSleep(uint32_t t) {
  static const Pose P[] = {
    { A_DOWN, L_TUCK, E_BLINK, E_BLINK, M_NONE,  0, 0, 6 },   // dozing off
    { A_DOWN, L_TUCK, E_SHUT,  E_SHUT,  M_NONE,  0, 0, 6 },   // asleep
    { A_DOWN, L_TUCK, E_SHUT,  E_SHUT,  M_SMALL, 0, 0, 6 },   // breathe in
    { A_DOWN, L_TUCK, E_SHUT,  E_SHUT,  M_OPEN,  0, 0, 4 },   // snore, swells
    { A_DOWN, L_TUCK, E_SHUT,  E_SHUT,  M_WAVY,  0, 0, 6 },   // mumbles
    { A_DOWN, L_TUCK, E_OPEN,  E_SHUT,  M_NONE,  0, 0, 6 },   // peeks with one eye
  };
  static const uint8_t SEQ[] = {
    0,1,1,2,3,2,1,1,2,3,2,1,
    4,1,1,
    2,3,2,1,
    5,5,0,1,1
  };
  draw(P[SEQ[(t / 5) % sizeof(SEQ)]]);

  // z's drift up and to the right
  int p1 = t % 10, p2 = (t + 4) % 10, p3 = (t + 7) % 10;
  buddySetColor(BUDDY_DIM);
  buddySetCursor(BUDDY_X_CENTER + 24 + p1, HEAD_Y + 10 - p1 * 2);
  buddyPrint("z");
  buddySetColor(ORANGE);
  buddySetCursor(BUDDY_X_CENTER + 32 + p2, HEAD_Y + 8 - p2);
  buddyPrint("Z");
  buddySetColor(BUDDY_DIM);
  buddySetCursor(BUDDY_X_CENTER + 18 + p3 / 2, HEAD_Y + 4 - p3 / 2);
  buddyPrint("z");
}

// ─── IDLE ───  ~34s cycle, 10 poses: looks around, blinks, waves, shuffles, hums
static void doIdle(uint32_t t) {
  static const Pose P[] = {
    { A_OUT,    L_STAND,  E_OPEN,  E_OPEN,  M_NONE,  0,  0, 0 },   // the logo pose
    { A_OUT,    L_STAND,  E_BLINK, E_BLINK, M_NONE,  0,  0, 0 },
    { A_OUT,    L_STAND,  E_LEFT,  E_LEFT,  M_NONE,  0,  0, 0 },
    { A_OUT,    L_STAND,  E_RIGHT, E_RIGHT, M_NONE,  0,  0, 0 },
    { A_WAVE_A, L_STAND,  E_OPEN,  E_OPEN,  M_SMILE, 0,  0, 0 },   // waves
    { A_WAVE_B, L_STAND,  E_OPEN,  E_OPEN,  M_SMILE, 0,  0, 0 },
    { A_OUT,    L_STEP_A, E_OPEN,  E_OPEN,  M_NONE,  0, -2, 0 },   // shuffles
    { A_OUT,    L_STEP_B, E_OPEN,  E_OPEN,  M_NONE,  0,  2, 0 },
    { A_OUT,    L_STAND,  E_HAPPY, E_HAPPY, M_SMALL, 0,  0, 0 },   // hums
    { A_OUT,    L_STAND,  E_UP,    E_UP,    M_NONE,  0,  0, 0 },   // looks up at the spark
  };
  static const uint8_t SEQ[] = {
    0,0,0,1,0,0,2,2,0,3,3,0,
    1,0,
    6,7,6,7,0,
    4,5,4,5,0,
    0,8,8,0,
    9,9,0,1,0
  };
  uint8_t pose = SEQ[(t / 5) % sizeof(SEQ)];
  draw(P[pose]);

  if (pose == 9) {          // the Claude spark above its head
    buddySetColor((t & 2) ? BUDDY_YEL : ORANGE);
    buddySetCursor(BUDDY_X_CENTER - 3, HEAD_Y + (int)((t / 2) % 2));
    buddyPrint((t & 2) ? "*" : "+");
  } else if (pose == 8) {   // a note floats up while it hums
    int ph = t % 6;
    buddySetColor(BUDDY_DIM);
    buddySetCursor(BUDDY_X_CENTER + 26 + ph, HEAD_Y + 6 - ph);
    buddyPrint("~");
  }
}

// ─── BUSY ───  ~16s cycle, 7 poses: reads, types, thinks, has an idea
static void doBusy(uint32_t t) {
  static const Pose P[] = {
    { A_OUT,    L_STAND, E_LEFT,  E_LEFT,  M_FLAT,  0, 0,  0 },   // reads...
    { A_OUT,    L_STAND, E_RIGHT, E_RIGHT, M_FLAT,  0, 0,  0 },   // ...across
    { A_TYPE_A, L_STAND, E_DOWN,  E_DOWN,  M_FLAT,  0, 0,  0 },   // types
    { A_TYPE_B, L_STAND, E_DOWN,  E_DOWN,  M_FLAT,  0, 0,  0 },
    { A_OUT,    L_STAND, E_UP,    E_UP,    M_SMALL, 0, 0,  0 },   // thinks
    { A_WAVE_A, L_STAND, E_WIDE,  E_WIDE,  M_OPEN,  0, 0, -3 },   // aha!
    { A_OUT,    L_STAND, E_HAPPY, E_HAPPY, M_NONE,  0, 0,  2 },   // nods, done
  };
  static const uint8_t SEQ[] = {
    2,3,2,3,2,3, 0,1,0,1, 4,4,4, 2,3,2,3, 4,4,5,5, 2,3,2,3, 6
  };
  uint8_t pose = SEQ[(t / 3) % sizeof(SEQ)];
  draw(P[pose]);

  // Claude Code's spinner: a spark that grows and shrinks above its shoulder
  static const char* const SPIN[] = { ".", "+", "x", "*", "x", "+" };
  buddySetColor(ORANGE);
  buddySetCursor(BUDDY_X_CENTER + 30, HEAD_Y + 2);
  buddyPrint(SPIN[t % 6]);

  if (pose == 4) {            // thought dots build up over its head
    static const char* const DOTS[] = { ".  ", ".. ", "..." };
    buddySetColor(BUDDY_DIM);
    buddySetCursor(BUDDY_X_CENTER - 8, HEAD_Y + 2);
    buddyPrint(DOTS[(t / 2) % 3]);
  } else if (pose == 5) {     // the idea
    buddySetColor(BUDDY_YEL);
    buddySetCursor(BUDDY_X_CENTER - 3, HEAD_Y);
    buddyPrint("!");
  }
}

// ─── ATTENTION ───  ~21s cycle, 7 poses: flags you down, looks for you, hops
static void doAttention(uint32_t t) {
  static const Pose P[] = {
    { A_OUT,     L_STAND, E_WIDE,  E_WIDE,  M_OPEN, 0, 0,  0 },   // alert
    { A_FLAIL_A, L_STAND, E_WIDE,  E_WIDE,  M_OPEN, 0, 0,  0 },   // waves you over
    { A_FLAIL_B, L_STAND, E_WIDE,  E_WIDE,  M_OPEN, 0, 0,  0 },
    { A_OUT,     L_STAND, E_LEFT,  E_LEFT,  M_OPEN, 0, 0,  0 },   // where are you?
    { A_OUT,     L_STAND, E_RIGHT, E_RIGHT, M_OPEN, 0, 0,  0 },
    { A_UP,      L_KICK,  E_WIDE,  E_WIDE,  M_OPEN, 0, 0, -6 },   // hops
    { A_DOWN,    L_STAND, E_OPEN,  E_OPEN,  M_FLAT, 0, 0,  0 },   // waits
  };
  static const uint8_t SEQ[] = {
    0,1,2,1,2,0, 3,0,4,0, 5,0,5,0, 1,2,1,2, 6,6,0
  };
  uint8_t pose = SEQ[(t / 5) % sizeof(SEQ)];
  draw(P[pose], pose == 0 ? ((t & 1) ? 1 : -1) : 0);   // jittery when alert

  if ((t / 2) & 1) {
    buddySetColor(BUDDY_YEL);
    buddySetCursor(BUDDY_X_CENTER - 12, HEAD_Y + 2);
    buddyPrint("!");
  }
  if ((t / 3) & 1) {
    buddySetColor(BUDDY_RED);
    buddySetCursor(BUDDY_X_CENTER + 8, HEAD_Y);
    buddyPrint("!");
  }
}

// ─── CELEBRATE ───  ~10s cycle, 6 poses: crouch, jump, spin, strike a pose
static void doCelebrate(uint32_t t) {
  static const Pose P[] = {
    { A_DOWN,    L_TUCK,  E_HAPPY, E_HAPPY, M_SMILE, 0,  0, 6 },   // crouch
    { A_UP,      L_STAND, E_HAPPY, E_HAPPY, M_SMILE, 0,  0, 0 },   // jump
    { A_UP,      L_KICK,  E_WIDE,  E_WIDE,  M_OPEN,  0,  0, 0 },   // top of the jump
    { A_WAVE_A,  L_STAND, E_HAPPY, E_HAPPY, M_SMILE, 0, -3, 0 },   // spin...
    { A_LEFT_UP, L_STAND, E_HAPPY, E_HAPPY, M_SMILE, 0,  3, 0 },
    { A_WAVE_A,  L_STAND, E_OPEN,  E_BLINK, M_SMILE, 0,  0, 0 },   // wink
  };
  static const uint8_t SEQ[]    = { 0,1,2,1,0, 3,4,3,4, 0,1,2,1,0, 5,5 };
  static const int8_t  Y_SHIFT[] = { 0,-4,-8,-4,0, 0,0,0,0, 0,-4,-8,-4,0, 0,0 };
  uint8_t beat = (t / 3) % sizeof(SEQ);
  draw(P[SEQ[beat]], 0, Y_SHIFT[beat]);

  // Confetti, with a few Claude sparks in it
  static const uint16_t cols[] = { BUDDY_YEL, ORANGE, BUDDY_CYAN, BUDDY_WHITE, BUDDY_GREEN };
  for (int i = 0; i < 6; i++) {
    int phase = (t * 2 + i * 11) % 22;
    int x = BUDDY_X_CENTER - 40 + i * 16;
    int y = HEAD_Y - 6 + phase;
    if (y > BUDDY_Y_BASE + 20 || y < 0) continue;
    buddySetColor(cols[i % 5]);
    buddySetCursor(x, y);
    buddyPrint((i + (int)(t / 2)) & 1 ? "+" : "*");
  }
}

// ─── DIZZY ───  ~11s cycle, 5 poses: wobbles, eyes spin, flops down
static void doDizzy(uint32_t t) {
  static const Pose P[] = {
    { A_OUT,     L_WOBBLE, E_SPIRAL, E_SPIRAL, M_WAVY, 0, 0, 0 },
    { A_OUT,     L_WOBBLE, E_CROSS,  E_CROSS,  M_WAVY, 0, 0, 0 },
    { A_FLAIL_A, L_STEP_A, E_SPIRAL, E_CROSS,  M_WAVY, 0, 0, 0 },   // reels
    { A_FLAIL_B, L_STEP_B, E_CROSS,  E_SPIRAL, M_WAVY, 0, 0, 0 },
    { A_DOWN,    L_TUCK,   E_CROSS,  E_CROSS,  M_FLAT, 0, 0, 6 },   // flops down
  };
  static const uint8_t SEQ[]    = { 0,1,0,1, 2,3, 0,1,0,1, 4,4, 2,3 };
  static const int8_t  X_SHIFT[] = { -3,3,-3,3, 0,0, -3,3,-3,3, 0,0, 0,0 };
  uint8_t beat = (t / 4) % sizeof(SEQ);
  draw(P[SEQ[beat]], X_SHIFT[beat]);

  // Stars and question marks circle its head
  static const int8_t OX_[] = { 0, 10, 14, 10, 0, -10, -14, -10 };
  static const int8_t OY_[] = { -4, -3, 0, 3, 4, 3, 0, -3 };
  uint8_t p1 = t % 8, p2 = (t + 4) % 8;
  buddySetColor(BUDDY_YEL);
  buddySetCursor(BUDDY_X_CENTER + OX_[p1] - 3, HEAD_Y + 4 + OY_[p1]);
  buddyPrint("*");
  buddySetColor(ORANGE);
  buddySetCursor(BUDDY_X_CENTER + OX_[p2] - 3, HEAD_Y + 4 + OY_[p2]);
  buddyPrint("?");
}

// ─── HEART ───  ~20s cycle, 5 poses: beams, blushes, heart eyes, hugs itself
static void doHeart(uint32_t t) {
  static const Pose P[] = {
    { A_OUT, L_STAND, E_HAPPY, E_HAPPY, M_SMILE, 0, 0, 0 },   // beams
    { A_OUT, L_STAND, E_HAPPY, E_HAPPY, M_SMILE, 1, 0, 0 },   // blushes
    { A_OUT, L_STAND, E_HEART, E_HEART, M_SMILE, 0, 0, 0 },   // heart eyes
    { A_HUG, L_STAND, E_HAPPY, E_HAPPY, M_SMILE, 1, 0, 0 },   // hugs itself
    { A_OUT, L_STAND, E_BLINK, E_BLINK, M_SMALL, 0, 0, 0 },   // happy sigh
  };
  static const uint8_t SEQ[] = {
    0,0,1,0, 2,2,0, 1,0,4, 0,0,3,3, 0,1,0,2, 1,0
  };
  static const int8_t Y_BOB[] = { 0,-1,0,-1, 0,-1,0, -1,0,0, -1,0,0,0, -1,0,-1,0, -1,0 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  draw(P[SEQ[beat]], 0, Y_BOB[beat]);

  // Hearts float up around it
  buddySetColor(BUDDY_HEART);
  for (int i = 0; i < 5; i++) {
    int phase = (t + i * 4) % 16;
    int y = BUDDY_Y_BASE - 2 - phase;
    if (y < 0) continue;
    int x = BUDDY_X_CENTER - 34 + i * 16 + ((phase / 3) & 1) * 2 - 1;
    buddySetCursor(x, y);
    buddyPrint("v");
  }
}

}  // namespace clawd

extern const Species CLAWD_SPECIES = {
  "clawd",
  clawd::ORANGE,
  { clawd::doSleep, clawd::doIdle, clawd::doBusy, clawd::doAttention,
    clawd::doCelebrate, clawd::doDizzy, clawd::doHeart }
};
