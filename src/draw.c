#include "draw.h"

#include <lt/text.h>
#include <lt/ctype.h>
#include <lt/str.h>
#include <lt/mem.h>
#include <lt/io.h>
#include <lt/term.h>
#include <lt/texted.h>

#define LT_ANSI_SHORTEN_NAMES 1
#include <lt/ansi.h>

char* draw_buf = NULL;
char* draw_buf_it = NULL;
char* draw_buf_end = NULL;

void draw_init(lt_alloc_t* alloc) {
	draw_buf = lt_malloc(alloc, DRAW_BUF_SIZE);
	LT_ASSERT(draw_buf);
	draw_buf_it = draw_buf;
	draw_buf_end = draw_buf + DRAW_BUF_SIZE;
}

void draw_flush(void) {
	lt_term_write_direct(draw_buf, draw_buf_it - draw_buf);
	draw_buf_it = draw_buf;
}

isz draw_write(void* usr, void* data, usz len) {
	LT_ASSERT(draw_buf_end - draw_buf_it > len);
	memcpy(draw_buf_it, data, len);
	draw_buf_it += len;
	return len;
}

isz draw_writef(char* fmt, ...) {
	va_list argl;
	va_start(argl, fmt);
	isz bytes = lt_io_vprintf((lt_io_callback_t)draw_write, NULL, fmt, argl);
	va_end(argl);

	return bytes;
}

static
u8 is_param_byte(u8 c) {
	return (c >= 0x30) && (c <= 0x3F);
}



isz str_width(lstr_t str) {
	isz w = 0;
	char* it = str.str, *end = it + str.len;
	while (it < end) {
		u32 c;
		it += lt_utf8_decode(it, &c);

		if (it < end && c == 0x1B && *it == '[') {
			++it;
			while (it < end && is_param_byte(*it))
				++it;
			++it;
		}
		else {
			w += lt_glyph_width(c);
		}
	}
	return w;
}

isz draw_wrapped_text(lstr_t text, isz max_width, lstr_t pfx) {
	char* end = text.str + text.len;

	max_width -= pfx.len;
	if (max_width <= 0)
		max_width = 1;

	char* word_start = text.str;
	char* line_start = text.str;
	for (char* it = text.str; it < end; ++it) {
		if (*it == ' ')
			word_start = it + 1;

		if (*it == '\n') {
			draw_write(NULL, pfx.str, pfx.len);
			draw_write(NULL, line_start, it - line_start);
			draw_write(NULL, "\n", 1);

			line_start = it + 1;
			continue;
		}

		usz line_len = (it - line_start);

		if (str_width(lt_lsfrom_range(line_start, it)) >= max_width) {
			usz word_len = it - word_start;

			char* new_line_start = it;

			if (line_start != word_start) {
				new_line_start -= word_len;
				line_len -= word_len;
			}
			else
				word_start = it;

			draw_write(NULL, pfx.str, pfx.len);
			draw_write(NULL, line_start, line_len);
			draw_write(NULL, "\n", 1);

			line_start = new_line_start;
		}
	}

	usz remain = end - line_start;
	draw_write(NULL, pfx.str, pfx.len);
	draw_write(NULL, line_start, remain);
	draw_write(NULL, "\n", 1);

	return 0;
}

void draw_texted(lt_texted_t* ed, char* sel_clr, char* normal_clr) {
	usz x1, x2;
	lt_texted_get_selection(ed, &x1, NULL, &x2, NULL);
	lstr_t str = lt_texted_line_str(ed, 0);

	char* x1_csave = x1 == ed->cursor_x ? CSAVE : "";
	char* x2_csave = x2 == ed->cursor_x ? CSAVE : "";
	draw_writef("%s%S%s%s%S%s%s%S"RESET, normal_clr, LSTR(str.str, x1), x1_csave, sel_clr, LSTR(str.str + x1, x2 - x1), x2_csave, normal_clr, LSTR(str.str + x2, str.len - x2));
}