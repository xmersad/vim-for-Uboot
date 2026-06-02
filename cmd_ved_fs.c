#include <common.h>
#include <command.h>
#include <fs.h>
#include <mapmem.h>
#include <linux/ctype.h>
#include "cmd_ved.h"

int ved_fs_str_to_type(const char *s)
{
	if (!strcmp(s, "fat") || !strcmp(s, "vfat"))
		return FS_TYPE_FAT;
	if (!strcmp(s, "ext4"))
		return FS_TYPE_EXT;
	if (!strcmp(s, "squashfs"))
		return FS_TYPE_SQUASHFS;
	return FS_TYPE_ANY;
}

int ved_fs_probe(struct ved_fs *fs, const char *iftype,
		 const char *dev_part, const char *fstype)
{
	int fs_type;

	if (!iftype || !dev_part)
		return -1;

	strncpy(fs->iftype,   iftype,   sizeof(fs->iftype)   - 1);
	strncpy(fs->dev_part, dev_part, sizeof(fs->dev_part) - 1);
	fs->iftype[sizeof(fs->iftype) - 1]     = '\0';
	fs->dev_part[sizeof(fs->dev_part) - 1] = '\0';

	if (fstype) {
		strncpy(fs->fstype, fstype, sizeof(fs->fstype) - 1);
		fs->fstype[sizeof(fs->fstype) - 1] = '\0';
		fs_type = ved_fs_str_to_type(fstype);
	} else {
		fs->fstype[0] = '\0';
		fs_type = FS_TYPE_ANY;
	}

	if (fs_set_blk_dev(fs->iftype, fs->dev_part, fs_type) != 0) {
		fs->valid = 0;
		return -1;
	}

	fs->valid = 1;
	return 0;
}

static int ved_fs_set(struct ved_fs *fs)
{
	int fs_type;

	if (!fs->valid)
		return -1;

	fs_type = fs->fstype[0] ? ved_fs_str_to_type(fs->fstype) : FS_TYPE_ANY;

	return fs_set_blk_dev(fs->iftype, fs->dev_part, fs_type);
}

int ved_file_load(struct ved_state *s, const char *path)
{
	loff_t  len_read = 0;
	ulong   addr     = VED_LOAD_ADDR;
	char   *buf      = (char *)map_sysmem(addr, VED_FILE_BUF_SIZE);
	int     ret;

	if (ved_fs_set(&s->fs) != 0) {
		unmap_sysmem(buf);
		return -1;
	}

	ret = fs_read(path, addr, 0, 0, &len_read);
	if (ret < 0) {
		unmap_sysmem(buf);
		if (ret == -ENOENT || len_read == 0) {
			s->num_lines    = 1;
			s->lines[0][0]  = '\0';
			s->line_len[0]  = 0;
			return 0;
		}
		return ret;
	}

	buf[len_read] = '\0';

	s->num_lines = 0;
	char *p          = buf;
	char *line_start = buf;

	while (*p && s->num_lines < VED_MAX_LINES - 1) {
		if (*p == '\n') {
			int len = (int)(p - line_start);
			if (len > 0 && line_start[len - 1] == '\r')
				len--;
			if (len >= VED_MAX_LINE_LEN)
				len = VED_MAX_LINE_LEN - 1;
			memcpy(s->lines[s->num_lines], line_start, len);
			s->lines[s->num_lines][len] = '\0';
			s->line_len[s->num_lines]   = len;
			s->num_lines++;
			line_start = p + 1;
		}
		p++;
	}

	if (p > line_start || s->num_lines == 0) {
		int len = (int)(p - line_start);
		if (len >= VED_MAX_LINE_LEN)
			len = VED_MAX_LINE_LEN - 1;
		memcpy(s->lines[s->num_lines], line_start, len);
		s->lines[s->num_lines][len] = '\0';
		s->line_len[s->num_lines]   = len;
		s->num_lines++;
	}

	if (s->num_lines == 0) {
		s->num_lines   = 1;
		s->lines[0][0] = '\0';
		s->line_len[0] = 0;
	}

	unmap_sysmem(buf);
	return 0;
}

int ved_file_save(struct ved_state *s)
{
	ulong   addr = VED_LOAD_ADDR;
	char   *buf  = (char *)map_sysmem(addr, VED_FILE_BUF_SIZE);
	loff_t  size = 0;
	loff_t  written = 0;
	int     ret;
	int     i;

	for (i = 0; i < s->num_lines; i++) {
		int len = s->line_len[i];
		memcpy(buf + size, s->lines[i], len);
		size += len;
		buf[size++] = '\n';
	}

	if (ved_fs_set(&s->fs) != 0) {
		unmap_sysmem(buf);
		return -1;
	}

	ret = fs_write(s->filename, addr, 0, size, &written);
	unmap_sysmem(buf);

	if (ret < 0)
		return ret;

	s->dirty = 0;
	return 0;
}
