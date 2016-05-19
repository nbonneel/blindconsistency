
#ifndef _allegro_emu_h
#define _allegro_emu_h

struct PATCHBITMAP {  // BITMAP messes up with wingdi.h on windows ; changed to PATCHBITMAP
  int w, h;
  unsigned char **line;
  unsigned char *data;
  PATCHBITMAP(int ww = 1, int hh = 1): w(ww), h(hh) {}
};

inline int _getpixel32(PATCHBITMAP *a, int x, int y) { return ((int *)a->line[y])[x]; }
inline void _putpixel32(PATCHBITMAP *a, int x, int y, int c) { ((int *)a->line[y])[x] = c; }

inline int getr32(int c) { return c&255; }
inline int getg32(int c) { return (c>>8)&255; }
inline int getb32(int c) { return (c>>16)&255; }

PATCHBITMAP *create_bitmap(int w, int h);
void blit(PATCHBITMAP *a, PATCHBITMAP *b, int ax, int ay, int bx, int by, int w, int h);
void destroy_bitmap(PATCHBITMAP *bmp);
typedef int patchfixed; // also messes up
patchfixed fixmul(patchfixed a, patchfixed b);
void clear(PATCHBITMAP *bmp);
void clear_to_color(PATCHBITMAP *bmp, int c);

/*
unsigned makecol(int r, int g, int b);
int bitmap_mask_color(PATCHBITMAP *bmp);
*/

int bitmap_color_depth(PATCHBITMAP *bmp);
PATCHBITMAP *create_bitmap_ex(int depth, int w, int h);

#endif
