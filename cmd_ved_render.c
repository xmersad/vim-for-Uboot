#include <common.h>
#include <stdarg.h>
#include <linux/ctype.h>
#include "cmd_ved.h"

static int ved_term_rows = TERM_ROWS;
static int ved_term_cols  = TERM_COLS;

void ved_term_detect(struct ved_state *s)
{
	s->term_rows = ved_term_rows;
	s->term_cols  = ved_term_cols;
	s->edit_rows  = s->term_rows - 2;
}

static void ved_draw_line(struct ved_state *s, int screen_row, int file_row)
{
	T_GOTO(screen_row + 1, 1);
	printf(T_CLRLINE);

	if (file_row >= s->num_lines) {
		printf(T_DIM "~" T_RESET);
		return;
	}

	char   *line = s->lines[file_row];
	int     len  = s->line_len[file_row];
	int     cols = s->term_cols;

	printf(T_DIM "%4d " T_RESET, file_row + 1);
	cols -= 5;

	if (len > cols)
		len = cols;

	for (int i = 0; i < len; i++) {
		char c = line[i];
		if (c < 0x20)
			printf(T_REVERSE "^%c" T_RESET, c + 0x40);
		else
			putc(c);
	}
}

static void ved_draw_status(struct ved_state *s)
{
	T_GOTO(s->term_rows - 1, 1);
	printf(T_CLRLINE T_REVERSE);

	const char *mode_str = "NORMAL";
	if (s->mode == MODE_INSERT)  mode_str = "INSERT";
	if (s->mode == MODE_COMMAND) mode_str = "COMMAND";
	if (s->mode == MODE_SEARCH)  mode_str = "SEARCH";

	char left[128];
	snprintf(left, sizeof(left), " %s%s | %s ",
		 mode_str,
		 s->dirty ? " [+]" : "",
		 s->filename[0] ? s->filename : "[No Name]");

	char right[64];
	snprintf(right, sizeof(right), " %d/%d : %d ",
		 s->cur_row + 1, s->num_lines, s->cur_col + 1);

	int pad = s->term_cols - (int)strlen(left) - (int)strlen(right);
	printf("%s", left);
	for (int i = 0; i < pad && i < 256; i++)
		putc(' ');
	printf("%s" T_RESET, right);
}

static void ved_draw_cmdline(struct ved_state *s)
{
	T_GOTO(s->term_rows, 1);
	printf(T_CLRLINE);

	if (s->status_msg[0]) {
		if (s->status_is_err)
			printf(T_REVERSE " ERROR: %s " T_RESET, s->status_msg);
		else
			printf(" %s", s->status_msg);
		return;
	}

	if (s->mode == MODE_COMMAND) {
		printf(":%s", s->cmd_buf);
	} else if (s->mode == MODE_SEARCH) {
		printf("/%s", s->search_buf);
	}
}

void ved_render(struct ved_state *s)
{
	T_HIDE_CURSOR;

	for (int sr = 0; sr < s->edit_rows; sr++)
		ved_draw_line(s, sr, s->scroll_top + sr);

	ved_draw_status(s);
	ved_draw_cmdline(s);

	int cursor_row = s->cur_row - s->scroll_top + 1;
	int cursor_col = s->cur_col + 5 + 1;

	if (s->mode == MODE_COMMAND)
		T_GOTO(s->term_rows, s->cmd_len + 2);
	else if (s->mode == MODE_SEARCH)
		T_GOTO(s->term_rows, s->search_len + 2);
	else
		T_GOTO(cursor_row, cursor_col);

	T_SHOW_CURSOR;
}

void ved_set_status(struct ved_state *s, int is_err, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s->status_msg, sizeof(s->status_msg), fmt, ap);
	va_end(ap);
	s->status_is_err = is_err;
}

void ved_clear_status(struct ved_state *s)
{
	s->status_msg[0] = '\0';
	s->status_is_err = 0;
}
