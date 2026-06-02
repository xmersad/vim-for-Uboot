#ifndef __CMD_VED_H
#define __CMD_VED_H

#define VED_MAX_LINES     4096
#define VED_MAX_LINE_LEN  512
#define VED_MAX_FILENAME  256
#define VED_FILE_BUF_SIZE (VED_MAX_LINES * VED_MAX_LINE_LEN)
#define VED_CMD_BUF_SIZE  128

#define VED_LOAD_ADDR     CONFIG_SYS_LOAD_ADDR

#define MODE_NORMAL   0
#define MODE_INSERT   1
#define MODE_COMMAND  2
#define MODE_SEARCH   3

#define KEY_ESC       0x1B
#define KEY_ENTER     0x0D
#define KEY_BACKSPACE 0x7F
#define KEY_BS        0x08
#define KEY_CTRL_C    0x03
#define KEY_CTRL_S    0x13
#define KEY_CTRL_U    0x15

/* Extended keys (> 0xFF, returned by ved_read_key) */
#define KEY_ARROW_UP    0x141
#define KEY_ARROW_DOWN  0x142
#define KEY_ARROW_RIGHT 0x143
#define KEY_ARROW_LEFT  0x144
#define KEY_HOME_SEQ    0x148
#define KEY_END_SEQ     0x146
#define KEY_PAGE_UP     0x200
#define KEY_PAGE_DOWN   0x201
#define KEY_DELETE      0x202

#define T_CLEAR       "\033[2J"
#define T_CLRLINE     "\033[2K"
#define T_RESET       "\033[0m"
#define T_REVERSE     "\033[7m"
#define T_BOLD        "\033[1m"
#define T_DIM         "\033[2m"
#define T_GOTO(r,c)   printf("\033[%d;%dH", (r), (c))
#define T_HIDE_CURSOR printf("\033[?25l")
#define T_SHOW_CURSOR printf("\033[?25h")

#define TERM_ROWS 24
#define TERM_COLS 80

struct ved_fs {
	char  iftype[16];
	char  dev_part[16];
	char  fstype[8];
	int   valid;
};

struct ved_state {
	char  lines[VED_MAX_LINES][VED_MAX_LINE_LEN];
	int   line_len[VED_MAX_LINES];
	int   num_lines;

	int   cur_row;
	int   cur_col;
	int   scroll_top;

	int   mode;
	int   dirty;
	int   quit;

	char  filename[VED_MAX_FILENAME];
	char  status_msg[256];
	int   status_is_err;

	char  cmd_buf[VED_CMD_BUF_SIZE];
	int   cmd_len;

	char  search_buf[VED_CMD_BUF_SIZE];
	int   search_len;

	char  yank_line[VED_MAX_LINE_LEN];
	int   yank_valid;

	struct ved_fs fs;

	int   term_rows;
	int   term_cols;
	int   edit_rows;
};

#endif
