#ifndef DRAW_H
#define DRAW_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

#define DRAW_BUF_SIZE LT_MB(1)

extern char* draw_buf;
extern char* draw_buf_it;
extern char* draw_buf_end;

isz str_width(lstr_t str);

isz draw_write(void* usr, void* data, usz len);
isz draw_writef(char* fmt, ...);
isz draw_wrapped_text(lstr_t text, isz max_width, lstr_t pfx);

void draw_init(lt_alloc_t* alloc);
void draw_flush(void);

void draw_texted(lt_texted_t* ed, char* sel_clr, char* normal_clr);

#endif