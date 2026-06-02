#include <common.h>
#include <command.h>
#include <fs.h>
#include <stdarg.h>
#include <linux/ctype.h>
#include "cmd_ved.h"

extern int  ved_file_load(struct ved_state *s, const char *path);
extern int  ved_file_save(struct ved_state *s);
extern int  ved_fs_probe(struct ved_fs *fs, const char *iftype,
			 const char *dev_part, const char *fstype);
extern int  ved_fs_str_to_type(const char *s);

extern void ved_term_detect(struct ved_state *s);
extern void ved_render(struct ved_state *s);
extern void ved_set_status(struct ved_state *s, int is_err,
			   const char *fmt, ...);
extern void ved_clear_status(struct ved_state *s);

extern void ved_move_up(struct ved_state *s, int n);
extern void ved_move_down(struct ved_state *s, int n);
extern void ved_move_left(struct ved_state *s, int n);
extern void ved_move_right(struct ved_state *s, int n);
extern void ved_goto_line(struct ved_state *s, int line);
extern void ved_goto_bol(struct ved_state *s);
extern void ved_goto_eol(struct ved_state *s);
extern void ved_word_forward(struct ved_state *s);
extern void ved_word_back(struct ved_state *s);
extern void ved_insert_char(struct ved_state *s, char c);
extern void ved_delete_char(struct ved_state *s);
extern void ved_backspace(struct ved_state *s);
extern void ved_insert_newline(struct ved_state *s);
extern void ved_open_line_below(struct ved_state *s);
extern void ved_open_line_above(struct ved_state *s);
extern void ved_delete_line(struct ved_state *s);
extern void ved_yank_line(struct ved_state *s);
extern void ved_put_line_below(struct ved_state *s);
extern void ved_delete_to_eol(struct ved_state *s);
extern void ved_scroll_adjust(struct ved_state *s);
extern void ved_clamp_col(struct ved_state *s);
extern int  ved_search_next(struct ved_state *s, const char *pat, int fwd);

static struct ved_state g_ved;
static int g_count_buf;


static int ved_read_key(void)
{
	int c;

	while (!tstc())
		;
	c = getchar();

	if (c != KEY_ESC)
		return c;

	if (!tstc())
		return KEY_ESC;

	c = getchar();
	if (c != '[' && c != 'O')
		return KEY_ESC;

	c = getchar();
	switch (c) {
	case 'A':    return KEY_ARROW_UP;
	case 'B':  return KEY_ARROW_DOWN;
	case 'C': return KEY_ARROW_RIGHT;
	case 'D':  return KEY_ARROW_LEFT;
	case 'H':  return KEY_HOME_SEQ;
	case 'F':   return KEY_END_SEQ;
	case '1': case '2': case '3':
	case '4': case '5': case '6': {
		int sub = c;
		int c4  = getchar();
		if (c4 == '~') {
			if (sub == '5') return KEY_PAGE_UP;
			if (sub == '6') return KEY_PAGE_DOWN;
			if (sub == '3') return KEY_DELETE;
		}
		return KEY_ESC;
	}
	default:
		return KEY_ESC;
	}
}

static void ved_do_save(struct ved_state *s)
{
	if (!s->filename[0]) {
		ved_set_status(s, 1, "No filename");
		return;
	}
	if (ved_file_save(s) == 0)
		ved_set_status(s, 0, "Saved: %s", s->filename);
	else
		ved_set_status(s, 1, "Save failed!");
}

static void ved_exec_command(struct ved_state *s)
{
	char *cmd = s->cmd_buf;
	int   num = 0;
	int   has_num = 0;

	while (*cmd && isdigit((unsigned char)*cmd)) {
		num = num * 10 + (*cmd - '0');
		has_num = 1;
		cmd++;
	}

	if (!strcmp(cmd, "w")) {
		ved_do_save(s);
	} else if (!strncmp(cmd, "w ", 2)) {
		strncpy(s->filename, cmd + 2, VED_MAX_FILENAME - 1);
		ved_do_save(s);
	} else if (!strcmp(cmd, "q")) {
		if (s->dirty)
			ved_set_status(s, 1, "Unsaved changes! Use :q! to force");
		else
			s->quit = 1;
	} else if (!strcmp(cmd, "q!")) {
		s->quit = 1;
	} else if (!strcmp(cmd, "wq") || !strcmp(cmd, "x")) {
		ved_do_save(s);
		if (!s->dirty)
			s->quit = 1;
	} else if (!strcmp(cmd, "wq!")) {
		ved_do_save(s);
		s->quit = 1;
	} else if (has_num && !*cmd) {
		ved_goto_line(s, num - 1);
	} else if (!strcmp(s->cmd_buf, "$")) {
		ved_goto_line(s, s->num_lines - 1);
	} else if (!strncmp(cmd, "e ", 2)) {
		if (s->dirty) {
			ved_set_status(s, 1, "Unsaved changes! Use :e! <file>");
		} else {
			strncpy(s->filename, cmd + 2, VED_MAX_FILENAME - 1);
			if (ved_file_load(s, s->filename) == 0) {
				s->cur_row = 0; s->cur_col = 0;
				s->scroll_top = 0;
				ved_set_status(s, 0, "Opened: %s", s->filename);
			} else {
				ved_set_status(s, 1, "Cannot open: %s", s->filename);
			}
		}
	} else if (!strncmp(cmd, "e! ", 3)) {
		strncpy(s->filename, cmd + 3, VED_MAX_FILENAME - 1);
		if (ved_file_load(s, s->filename) == 0) {
			s->cur_row = 0; s->cur_col = 0;
			s->scroll_top = 0; s->dirty = 0;
			ved_set_status(s, 0, "Opened: %s", s->filename);
		} else {
			ved_set_status(s, 1, "Cannot open: %s", s->filename);
		}
	} else {
		ved_set_status(s, 1, "Unknown command: %s", s->cmd_buf);
	}
}

static void ved_handle_command_key(struct ved_state *s, int key)
{
	if (key == KEY_ENTER) {
		s->mode = MODE_NORMAL;
		ved_exec_command(s);
		s->cmd_buf[0] = '\0';
		s->cmd_len    = 0;
		return;
	}

	if (key == KEY_ESC) {
		s->mode = MODE_NORMAL;
		s->cmd_buf[0] = '\0';
		s->cmd_len    = 0;
		ved_clear_status(s);
		return;
	}

	if ((key == KEY_BACKSPACE || key == KEY_BS) && s->cmd_len > 0) {
		s->cmd_buf[--s->cmd_len] = '\0';
		if (s->cmd_len == 0)
			s->mode = MODE_NORMAL;
		return;
	}

	if (key >= 0x20 && key < 0x7f && s->cmd_len < VED_CMD_BUF_SIZE - 1) {
		s->cmd_buf[s->cmd_len++] = (char)key;
		s->cmd_buf[s->cmd_len]   = '\0';
	}
}

static void ved_handle_search_key(struct ved_state *s, int key)
{
	if (key == KEY_ENTER) {
		s->mode = MODE_NORMAL;
		if (s->search_len > 0) {
			if (ved_search_next(s, s->search_buf, 1) != 0)
				ved_set_status(s, 1, "Pattern not found: %s",
					       s->search_buf);
		}
		return;
	}

	if (key == KEY_ESC) {
		s->mode       = MODE_NORMAL;
		s->search_buf[0] = '\0';
		s->search_len = 0;
		return;
	}

	if ((key == KEY_BACKSPACE || key == KEY_BS) && s->search_len > 0) {
		s->search_buf[--s->search_len] = '\0';
		return;
	}

	if (key >= 0x20 && key < 0x7f &&
	    s->search_len < VED_CMD_BUF_SIZE - 1) {
		s->search_buf[s->search_len++] = (char)key;
		s->search_buf[s->search_len]   = '\0';
	}
}

static void ved_handle_insert_key(struct ved_state *s, int key)
{
	switch (key) {
	case KEY_ESC:
		s->mode = MODE_NORMAL;
		if (s->cur_col > 0)
			s->cur_col--;
		ved_clamp_col(s);
		break;
	case KEY_ENTER:
		ved_insert_newline(s);
		break;
	case KEY_BACKSPACE:
	case KEY_BS:
		ved_backspace(s);
		break;
	case KEY_CTRL_U:
		while (s->cur_col > 0)
			ved_backspace(s);
		break;
	case KEY_ARROW_UP:    ved_move_up(s, 1);    break;
	case KEY_ARROW_DOWN:  ved_move_down(s, 1);  break;
	case KEY_ARROW_LEFT:  ved_move_left(s, 1);  break;
	case KEY_ARROW_RIGHT: ved_move_right(s, 1); break;
	case KEY_HOME_SEQ:    ved_goto_bol(s);       break;
	case KEY_END_SEQ:     ved_goto_eol(s);       break;
	default:
		if (key >= 0x20 && key < 0x100)
			ved_insert_char(s, (char)key);
		break;
	}
}

static void ved_handle_normal_key(struct ved_state *s, int key)
{
	int n = g_count_buf > 0 ? g_count_buf : 1;

	ved_clear_status(s);

	if (key >= '1' && key <= '9' && g_count_buf == 0) {
		g_count_buf = key - '0';
		return;
	}
	if (key >= '0' && key <= '9' && g_count_buf > 0) {
		g_count_buf = g_count_buf * 10 + (key - '0');
		return;
	}

	switch (key) {
	case 'h': case KEY_ARROW_LEFT:  ved_move_left(s, n);   break;
	case 'l': case KEY_ARROW_RIGHT: ved_move_right(s, n);  break;
	case 'k': case KEY_ARROW_UP:    ved_move_up(s, n);     break;
	case 'j': case KEY_ARROW_DOWN:  ved_move_down(s, n);   break;

	case 'w': ved_word_forward(s); break;
	case 'b': ved_word_back(s);    break;

	case '0': ved_goto_bol(s); break;
	case '$': ved_goto_eol(s); break;

	case KEY_HOME_SEQ: ved_goto_bol(s); break;
	case KEY_END_SEQ:  ved_goto_eol(s); break;

	case 'g': {
		int next = ved_read_key();
		if (next == 'g')
			ved_goto_line(s, 0);
		break;
	}
	case 'G':
		ved_goto_line(s, g_count_buf > 0 ? g_count_buf - 1
						  : s->num_lines - 1);
		break;

	case KEY_PAGE_UP:
		ved_move_up(s, s->edit_rows);
		break;
	case KEY_PAGE_DOWN:
		ved_move_down(s, s->edit_rows);
		break;
	case KEY_CTRL_U:
		ved_move_up(s, s->edit_rows / 2);
		break;

	case 'i': s->mode = MODE_INSERT; break;
	case 'I':
		ved_goto_bol(s);
		s->mode = MODE_INSERT;
		break;
	case 'a':
		if (s->line_len[s->cur_row] > 0)
			s->cur_col++;
		s->mode = MODE_INSERT;
		break;
	case 'A':
		s->cur_col = s->line_len[s->cur_row];
		s->mode    = MODE_INSERT;
		break;
	case 'o': ved_open_line_below(s); break;
	case 'O': ved_open_line_above(s); break;

	case 'x':
	case KEY_DELETE:
		ved_delete_char(s);
		break;
	case 'X':
		if (s->cur_col > 0) {
			s->cur_col--;
			ved_delete_char(s);
		}
		break;

	case 'd': {
		int next = ved_read_key();
		if (next == 'd') {
			int i;
			for (i = 0; i < n; i++)
				ved_delete_line(s);
		} else if (next == '$') {
			ved_delete_to_eol(s);
		}
		break;
	}
	case 'D': ved_delete_to_eol(s); break;

	case 'y': {
		int next = ved_read_key();
		if (next == 'y')
			ved_yank_line(s);
		break;
	}
	case 'Y': ved_yank_line(s);     break;
	case 'p': ved_put_line_below(s); break;

	case 'r': {
		int next = ved_read_key();
		if (next >= 0x20 && next < 0x7f) {
			int row = s->cur_row;
			int col = s->cur_col;
			if (col < s->line_len[row]) {
				s->lines[row][col] = (char)next;
				s->dirty = 1;
			}
		}
		break;
	}

	case '/':
		s->mode          = MODE_SEARCH;
		s->search_buf[0] = '\0';
		s->search_len    = 0;
		break;
	case 'n':
		if (ved_search_next(s, s->search_buf, 1) != 0)
			ved_set_status(s, 1, "Pattern not found");
		break;
	case 'N':
		if (ved_search_next(s, s->search_buf, 0) != 0)
			ved_set_status(s, 1, "Pattern not found");
		break;

	case ':':
		s->mode       = MODE_COMMAND;
		s->cmd_buf[0] = '\0';
		s->cmd_len    = 0;
		break;

	case KEY_CTRL_S:
		ved_do_save(s);
		break;

	case KEY_CTRL_C:
		if (s->dirty)
			ved_set_status(s, 1, "Unsaved! Use :q! or :wq");
		else
			s->quit = 1;
		break;
	}

	g_count_buf = 0;
}

static void ved_run(struct ved_state *s)
{
	int key;

	printf(T_CLEAR);
	ved_term_detect(s);

	while (!s->quit) {
		ved_render(s);
		if (s->status_msg[0])
			ved_clear_status(s);

		key = ved_read_key();

		switch (s->mode) {
		case MODE_NORMAL:
			ved_handle_normal_key(s, key);
			break;
		case MODE_INSERT:
			ved_handle_insert_key(s, key);
			break;
		case MODE_COMMAND:
			ved_handle_command_key(s, key);
			break;
		case MODE_SEARCH:
			ved_handle_search_key(s, key);
			break;
		}
	}

	printf(T_CLEAR);
	T_GOTO(1, 1);
	T_SHOW_CURSOR;
}

static int do_ved(struct cmd_tbl *cmdtp, int flag,
		  int argc, char *const argv[])
{
	struct ved_state *s = &g_ved;
	const char *fstype;

	if (argc < 4) {
		printf("Usage: ved <iftype> <dev:part> <filename> [fstype]\n");
		return CMD_RET_USAGE;
	}

	memset(s, 0, sizeof(*s));
	g_count_buf = 0;

	fstype = (argc > 4) ? argv[4] : NULL;

	if (ved_fs_probe(&s->fs, argv[1], argv[2], fstype) < 0) {
		printf("ved: cannot access %s %s\n", argv[1], argv[2]);
		return CMD_RET_FAILURE;
	}

	strncpy(s->filename, argv[3], VED_MAX_FILENAME - 1);

	if (ved_file_load(s, s->filename) < 0) {
		printf("ved: cannot load %s\n", s->filename);
		return CMD_RET_FAILURE;
	}

	ved_run(s);
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	ved, 5, 0, do_ved,
	"vim-like file editor",
	"<iftype> <dev:part> <filename> [fstype]\n"
	"  iftype  : mmc usb virtio nvme sata\n"
	"  dev:part: 0:1  1:2  ...\n"
	"  filename: full path on filesystem\n"
	"  fstype  : fat ext4  (optional, auto-detect)\n"
	"Normal:  hjkl arrows w b 0 $ gg G\n"
	"         i I a A o O  x dd D  yy p  r  / n N\n"
	"         :w :q :wq :x :q! :N  Ctrl+S Ctrl+C"
);
