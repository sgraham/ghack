#include "sys.h"

#include <stdlib.h>
#include <math.h>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define false 0
#define true -1

typedef unsigned char Color;

typedef struct Bitmap {
  int w, h;  // Width and height in pixels.
  int clip;  // Is clipping turned on.
  int cl, cr, ct, cb;  // Clip left, right, top, bottom values.
  void* data;
  unsigned char* line[];
} Bitmap;

static unsigned char screen_bitmap[sizeof(Bitmap) + 256 * 256];

// TODO(scottmg): Separate driver functions that depend on target bitmap type,
// etc. Maybe put these in a function table if we support multiple
// simulanteously, or for in-memory bitmaps with different formats, etc.

Color gfx_GetPixel(Bitmap* bmp, int x, int y) {
  return ((Color*)bmp->data)[y * bmp->w + x];
}

void gfx_PutPixel(Bitmap* bmp, int x, int y, Color color) {
  if (bmp->clip) {
    if (x < bmp->cl)
      return;
    if (x >= bmp->cr)
      return;
    if (y < bmp->ct)
      return;
    if (x >= bmp->cb)
      return;
  }
  ((Color*)bmp->data)[y * bmp->w + x] = color;
}

void gfx_Clear(Bitmap* bmp, Color color) {
  for (int i = 0; i < bmp->w * bmp->h; ++i) {
    ((Color*)bmp->data)[i] = color;
  }
}

// Calculates all the points along a line between x1,y1 and x2,y2 calling the
// supplied function for each one. |d| is passed through.
static void do_line(Bitmap* bmp,
                    int x1,
                    int y1,
                    int x2,
                    int y2,
                    Color d,
                    void (*proc)(Bitmap*, int, int, Color)) {
  int dx = x2 - x1;
  int dy = y2 - y1;
  int i1, i2;
  int x, y;
  int dd;

#define DO_LINE(pri_sign, pri_c, pri_cond, sec_sign, sec_c, sec_cond) \
  {                                                                   \
    if (d##pri_c == 0) {                                              \
      proc(bmp, x1, y1, d);                                           \
      return;                                                         \
    }                                                                 \
                                                                      \
    i1 = 2 * d##sec_c;                                                \
    dd = i1 - (sec_sign(pri_sign d##pri_c));                          \
    i2 = dd - (sec_sign(pri_sign d##pri_c));                          \
                                                                      \
    x = x1;                                                           \
    y = y1;                                                           \
                                                                      \
    while (pri_c pri_cond pri_c##2) {                                 \
      proc(bmp, x, y, d);                                             \
                                                                      \
      if (dd sec_cond 0) {                                            \
        sec_c sec_sign## = 1;                                         \
        dd += i2;                                                     \
      } else                                                          \
        dd += i1;                                                     \
                                                                      \
      pri_c pri_sign## = 1;                                           \
    }                                                                 \
  }

  if (dx >= 0) {
    if (dy >= 0) {
      if (dx >= dy) {
        /* (x1 <= x2) && (y1 <= y2) && (dx >= dy) */
        DO_LINE(+, x, <=, +, y, >= );
      } else {
        /* (x1 <= x2) && (y1 <= y2) && (dx < dy) */
        DO_LINE(+, y, <=, +, x, >= );
      }
    } else {
      if (dx >= -dy) {
        /* (x1 <= x2) && (y1 > y2) && (dx >= dy) */
        DO_LINE(+, x, <=, -, y, <= );
      } else {
        /* (x1 <= x2) && (y1 > y2) && (dx < dy) */
        DO_LINE(-, y, >=, +, x, >= );
      }
    }
  } else {
    if (dy >= 0) {
      if (-dx >= dy) {
        /* (x1 > x2) && (y1 <= y2) && (dx >= dy) */
        DO_LINE(-, x, >=, +, y, >= );
      } else {
        /* (x1 > x2) && (y1 <= y2) && (dx < dy) */
        DO_LINE(+, y, <=, -, x, <= );
      }
    } else {
      if (-dx >= -dy) {
        /* (x1 > x2) && (y1 > y2) && (dx >= dy) */
        DO_LINE(-, x, >=, -, y, <= );
      } else {
        /* (x1 > x2) && (y1 > y2) && (dx < dy) */
        DO_LINE(-, y, >=, -, x, <= );
      }
    }
  }

#undef DO_LINE
}

void gfx_Line(Bitmap* bmp, int x1, int y1, int x2, int y2, Color color) {
  int sx, sy, dx, dy, t;

  /* TODO(scottmg): Special case for fasterness.
  if (x1 == x2) {
    gfx_VLine(bmp, x1, y1, y2, color);
    return;
  }

  if (y1 == y2) {
    gfx_HLine(bmp, x1, y1, x2, color);
    return;
  }
  */

  // Check if the line needs clipping.
  if (bmp->clip) {
    sx = x1;
    sy = y1;
    dx = x2;
    dy = y2;

    if (sx > dx) {
      t = sx;
      sx = dx;
      dx = t;
    }

    if (sy > dy) {
      t = sy;
      sy = dy;
      dy = t;
    }

    if ((sx >= bmp->cr) || (sy >= bmp->cb) || (dx < bmp->cl) || (dy < bmp->ct))
      return;

    if ((sx >= bmp->cl) && (sy >= bmp->ct) && (dx < bmp->cr) && (dy < bmp->cb))
      bmp->clip = false;

    t = true;
  } else
    t = false;

  do_line(bmp, x1, y1, x2, y2, color, gfx_PutPixel);

  bmp->clip = t;
}

// Calculates a set of pixels for the bezier spline defined by the four points
// specified in the points array. The required resolution is specified by the
// num_points parameter, which controls how many output pixels will be stored in
// the x and y arrays.
static void calc_spline(int points[8], int num_points, int* out_x, int* out_y) {
  // Derivatives of x(t) and y(t).
  float x, dx, ddx, dddx;
  float y, dy, ddy, dddy;
  int i;

  // Temp variables used in the setup.
  float dt, dt2, dt3;
  float xdt2_term, xdt3_term;
  float ydt2_term, ydt3_term;

  dt = 1.f / (num_points - 1);
  dt2 = (dt * dt);
  dt3 = (dt2 * dt);

  // X coordinates.
  xdt2_term = 3.f * (points[4] - 2.f * points[2] + points[0]);
  xdt3_term = points[6] + 3.f * (-points[4] + points[2]) - points[0];

  xdt2_term = dt2 * xdt2_term;
  xdt3_term = dt3 * xdt3_term;

  dddx = 6.f * xdt3_term;
  ddx = -6.f * xdt3_term + 2.f * xdt2_term;
  dx = xdt3_term - xdt2_term + 3.f * dt * (points[2] - points[0]);
  x = (float)points[0];

  out_x[0] = points[0];

  x += .5;
  for (i = 1; i < num_points; i++) {
    ddx += dddx;
    dx += ddx;
    x += dx;

    out_x[i] = (int)x;
  }

  // Y coordinates.
  ydt2_term = 3.f * (points[5] - 2.f * points[3] + points[1]);
  ydt3_term = points[7] + 3.f * (-points[5] + points[3]) - points[1];

  ydt2_term = dt2 * ydt2_term;
  ydt3_term = dt3 * ydt3_term;

  dddy = 6.f * ydt3_term;
  ddy = -6.f * ydt3_term + 2.f * ydt2_term;
  dy = ydt3_term - ydt2_term + dt * 3.f * (points[3] - points[1]);
  y = (float)points[1];

  out_y[0] = points[1];

  y += .5;

  for (i = 1; i < num_points; i++) {
    ddy += dddy;
    dy += ddy;
    y += dy;

    out_y[i] = (int)y;
  }
}

// The 4th order Bezier curve is a cubic curve passing through the first and
// fourth point. The curve does not pass through the middle two points. They are
// merely guide points which control the shape of the curve. The curve is
// tangent to the lines joining points 1 and 2 and points 3 and 4.
void gfx_Spline(Bitmap* bmp, int points[8], Color color) {
#define MAX_POINTS 64

  int x_points[MAX_POINTS], y_points[MAX_POINTS];
  int i;
  int num_points;

#define DIST(x, y) (sqrt((x) * (x) + (y) * (y)))
  num_points = (int)(sqrt(DIST(points[2] - points[0], points[3] - points[1]) +
                          DIST(points[4] - points[2], points[5] - points[3]) +
                          DIST(points[6] - points[4], points[7] - points[5])) *
                     1.2);
#undef DIST

  if (num_points > MAX_POINTS)
    num_points = MAX_POINTS;

  calc_spline(points, num_points, x_points, y_points);

  for (i = 1; i < num_points; i++) {
    gfx_Line(
        bmp, x_points[i - 1], y_points[i - 1], x_points[i], y_points[i], color);
  }
}

// Calculates a set of pixels for the bezier spline defined by the four points
// specified in the points array. The required resolution is specified by the
// num_points parameter, which controls how many output pixels will be stored in
// the x and y arrays.
static void calc_splinef(float points[8],
                         int num_points,
                         float* out_x,
                         float* out_y) {
  // Derivatives of x(t) and y(t).
  float x, dx, ddx, dddx;
  float y, dy, ddy, dddy;
  int i;

  // Temp variables used in the setup.
  float dt, dt2, dt3;
  float xdt2_term, xdt3_term;
  float ydt2_term, ydt3_term;

  dt = 1.f / (num_points - 1);
  dt2 = (dt * dt);
  dt3 = (dt2 * dt);

  // X coordinates.
  xdt2_term = 3.f * (points[4] - 2.f * points[2] + points[0]);
  xdt3_term = points[6] + 3.f * (-points[4] + points[2]) - points[0];

  xdt2_term = dt2 * xdt2_term;
  xdt3_term = dt3 * xdt3_term;

  dddx = 6.f * xdt3_term;
  ddx = -6.f * xdt3_term + 2.f * xdt2_term;
  dx = xdt3_term - xdt2_term + 3.f * dt * (points[2] - points[0]);
  x = (float)points[0];

  out_x[0] = points[0];

  x += .5;
  for (i = 1; i < num_points; i++) {
    ddx += dddx;
    dx += ddx;
    x += dx;

    out_x[i] = x;
  }

  // Y coordinates.
  ydt2_term = 3.f * (points[5] - 2.f * points[3] + points[1]);
  ydt3_term = points[7] + 3.f * (-points[5] + points[3]) - points[1];

  ydt2_term = dt2 * ydt2_term;
  ydt3_term = dt3 * ydt3_term;

  dddy = 6.f * ydt3_term;
  ddy = -6.f * ydt3_term + 2.f * ydt2_term;
  dy = ydt3_term - ydt2_term + dt * 3.f * (points[3] - points[1]);
  y = points[1];

  out_y[0] = points[1];

  y += .5;

  for (i = 1; i < num_points; i++) {
    ddy += dddy;
    dy += ddy;
    y += dy;

    out_y[i] = y;
  }
}

// The 4th order Bezier curve is a cubic curve passing through the first and
// fourth point. The curve does not pass through the middle two points. They are
// merely guide points which control the shape of the curve. The curve is
// tangent to the lines joining points 1 and 2 and points 3 and 4.
void gfx_Splinef(Bitmap* bmp, float points[8], Color color) {
#define MAX_POINTS 64

  float x_points[MAX_POINTS], y_points[MAX_POINTS];
  int i;
  int num_points;

#define DIST(x, y) (sqrt((x) * (x) + (y) * (y)))
  num_points = (int)(sqrt(DIST(points[2] - points[0], points[3] - points[1]) +
                          DIST(points[4] - points[2], points[5] - points[3]) +
                          DIST(points[6] - points[4], points[7] - points[5])) *
                     1.2);
#undef DIST

  if (num_points > MAX_POINTS)
    num_points = MAX_POINTS;

  calc_splinef(points, num_points, x_points, y_points);

  for (i = 1; i < num_points; i++) {
    gfx_Line(bmp,
             (int)x_points[i - 1],
             (int)y_points[i - 1],
             (int)x_points[i],
             (int)y_points[i],
             color);
  }
}

void DrawCross(Bitmap* bmp, int x, int y, Color c) {
  gfx_Line(bmp, x - 3, y - 3, x + 3, y + 3, c);
  gfx_Line(bmp, x + 3, y - 3, x - 3, y + 3, c);
}

int main(void) {
  // TODO(scottmg): Fixed point transcendentals.
  float theta = 0;

  int splinex = 0;
  int splinedx = 1;

  Bitmap* screen = (Bitmap*)screen_bitmap;
  screen->w = 256;
  screen->h = 256;
  screen->clip = 1;
  screen->cl = 0;
  screen->cr = 256;
  screen->ct = 0;
  screen->cb = 256;
  screen->data = &screen->line[0];

  NSVGimage* images[] = {
    nsvgParseFromFile("bell.svg", "pt", 6),
    nsvgParseFromFile("arrow-with-circle-left.svg", "pt", 6),
    nsvgParseFromFile("bug.svg", "pt", 6),
    nsvgParseFromFile("camera.svg", "pt", 6),
    nsvgParseFromFile("thumbs-up.svg", "pt", 6),
  };
  int image_index = 0;

  sys_gfx_open("coin", 256, 256);
  for (;;) {
    gfx_Clear(screen, 0);
    for (int i = 0; i < 10; ++i) {
      gfx_Line(screen,
              128,
              128,
              128 + (int)((80 + i * 4) * cosf(theta + 0.15f * i)),
              128 + (int)((80 + i * 4) * sinf(theta + 0.15f * i)),
              4);
    }
    theta += 0.01f;

    int points[8] = {
        splinex,
        4,
        (int)(64*cosf(theta*3.f) + 64),
        (int)(64*sinf(theta*3.f) + 64),
        (int)(-64*cosf(theta*3.f) + 192),
        (int)(-64*sinf(theta*3.f) + 192),
        256 - splinex,
        254,
    };
    splinex += splinedx;
    if (splinex == 255)
      splinedx = -1;
    else if (splinex == 1)
      splinedx = 1;
    gfx_Spline(screen, points, 3);

    DrawCross(screen, points[0], points[1], 7);
    DrawCross(screen, points[2], points[3], 7);
    DrawCross(screen, points[4], points[5], 7);
    DrawCross(screen, points[6], points[7], 7);

    NSVGimage* image = images[(image_index++)/30];
    if (image_index == sizeof(images)/sizeof(images[0]) * 30)
      image_index = 0;
    for (NSVGshape* shape = image->shapes; shape != NULL; shape = shape->next) {
      for (NSVGpath* path = shape->paths; path != NULL; path = path->next) {
        for (int i = 0; i < path->npts - 1; i += 3) {
          float* p = &path->pts[i * 2];
          gfx_Splinef(screen, &p[0], 2);
        }
      }
    }

    sys_gfx_update(screen->data);
  }

	/*nsvgDelete(image);*/
}
