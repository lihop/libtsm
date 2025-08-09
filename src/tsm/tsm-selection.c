/*
 * libtsm - Screen Selections
 *
 * Copyright (c) 2019-2020 Fredrik Wikstrom <fredrik@a500.org>
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Screen Selections
 * If a running pty-client does not support mouse-tracking extensions, a
 * terminal can manually mark selected areas if it does mouse-tracking itself.
 * This tracking is slightly different than the integrated client-tracking:
 *
 * Initial state is no-selection. At any time selection_reset() can be called to
 * clear the selection and go back to initial state.
 * If the user presses a mouse-button, the terminal can calculate the selected
 * cell and call selection_start() to notify the terminal that the user started
 * the selection. While the mouse-button is held down, the terminal should call
 * selection_target() whenever a mouse-event occurs. This will tell the screen
 * layer to draw the selection from the initial start up to the last given
 * target.
 * Please note that the selection-start cannot be modified by the terminal
 * during a selection. Instead, the screen-layer automatically moves it along
 * with any scroll-operations or inserts/deletes. This also means, the terminal
 * must _not_ cache the start-position itself as it may change under the hood.
 * This selection takes also care of scrollback-buffer selections and correctly
 * moves selection state along.
 *
 * Please note that this is not the kind of selection that some PTY applications
 * support. If the client supports the mouse-protocol, then it can also control
 * a separate screen-selection which is always inside of the actual screen. This
 * is a totally different selection.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "libtsm.h"
#include "libtsm-int.h"
#include "shl-llog.h"

#define LLOG_SUBSYSTEM "tsm-selection"

static void selection_age(struct tsm_screen *con,
                          const struct selection_pos *start,
                          const struct selection_pos *end)
{
	unsigned int i, j, k;
	struct line *iter, *line = NULL;
	bool in_sel = false, sel_start = false, sel_end = false;

	iter = con->sb_pos;
	k = 0;

	if (start->line ? (!iter || start->line->sb_id < iter->sb_id) : (start->y == SELECTION_TOP))
	{
		in_sel = !in_sel;
	}
	if (end->line ? (!iter || end->line->sb_id < iter->sb_id) : (end->y == SELECTION_TOP))
	{
		in_sel = !in_sel;
		if (!in_sel)
			return;
	}

	for (i = 0; i < con->size_y; ++i) {
		if (iter) {
			line = iter;
			iter = iter->next;
		} else {
			line = con->lines[k];
			k++;
		}

		if (start->line == line || (!start->line && start->y == k - 1))
			sel_start = true;
		else
			sel_start = false;

		if (end->line == line || (!end->line && end->y == k - 1))
			sel_end = true;
		else
			sel_end = false;

		if (sel_start && sel_end) {
			if (start->x <= end->x) {
				for (j = start->x; j <= end->x && j < line->size; ++j) {
					line->cells[j].age = con->age_cnt;
				}
			} else {
				for (j = end->x; j <= start->x && j < line->size; ++j) {
					line->cells[j].age = con->age_cnt;
				}
			}
		} else if (sel_start) {
			if (in_sel) {
				for (j = 0; j <= start->x && j < line->size; ++j) {
					line->cells[j].age = con->age_cnt;
				}
			} else {
				for (j = start->x; j < con->size_x && j < line->size; ++j) {
					line->cells[j].age = con->age_cnt;
				}
			}
			in_sel = !in_sel;
		} else if (sel_end) {
			if (in_sel) {
				for (j = 0; j <= end->x && j < line->size; ++j) {
					line->cells[j].age = con->age_cnt;
				}
			} else {
				for (j = end->x; j < con->size_x && j < line->size; ++j) {
					line->cells[j].age = con->age_cnt;
				}
			}
			in_sel = !in_sel;
		} else if (in_sel) {
			line->age = con->age_cnt;
		}
	}
}

static void selection_set(struct tsm_screen *con, struct selection_pos *sel,
			  unsigned int x, unsigned int y)
{
	struct line *pos;

	pos = con->sb_pos;

	while (y && pos) {
		--y;
		pos = pos->next;
	}

		sel->line = pos;
	sel->x = x;
	sel->y = y;
}

SHL_EXPORT
void tsm_screen_selection_reset(struct tsm_screen *con)
{
	if (!con || !con->sel_active)
		return;

	screen_inc_age(con);

	selection_age(con, &con->sel_start, &con->sel_end);

	con->sel_active = false;
}

SHL_EXPORT
void tsm_screen_selection_start(struct tsm_screen *con,
				unsigned int posx,
				unsigned int posy)
{
	if (!con)
		return;

	screen_inc_age(con);

	if (con->sel_active)
		selection_age(con, &con->sel_start, &con->sel_end);

	con->sel_active = true;
	selection_set(con, &con->sel_start, posx, posy);
	memcpy(&con->sel_end, &con->sel_start, sizeof(con->sel_end));

	selection_age(con, &con->sel_start, &con->sel_end);
}

SHL_EXPORT
void tsm_screen_selection_target(struct tsm_screen *con,
				 unsigned int posx,
				 unsigned int posy)
{
	struct selection_pos old_end;

	if (!con || !con->sel_active)
		return;

	screen_inc_age(con);

	memcpy(&old_end, &con->sel_end, sizeof(con->sel_end));
	selection_set(con, &con->sel_end, posx, posy);

	selection_age(con, &old_end, &con->sel_end);
}

static struct line *line_get(struct tsm_screen *con,
                             unsigned int y)
{
	struct line *pos;

	pos = con->sb_pos;

	while (y && pos) {
		--y;
		pos = pos->next;
	}

	if (pos)
		return pos;

	return con->lines[y];
}

void tsm_screen_selection_word(struct tsm_screen *con,
                               unsigned int posx,
                               unsigned int posy)
{
	struct line *line;

	if (!con)
		return;

	if (con->sel_active)
		selection_age(con, &con->sel_start, &con->sel_end);

	line = line_get(con, posy);

	if (posx < line->size && iswalnum(line->cells[posx].ch))
	{
		int i;
		unsigned int startx, endx;

		screen_inc_age(con);

		for (i = posx - 1; i >= 0 && iswalnum(line->cells[i].ch); i--);
		startx = i + 1;

		for (i = posx + 1; i < line->size && iswalnum(line->cells[i].ch); i++);
		endx = i - 1;

		con->sel_active = true;
		selection_set(con, &con->sel_start, startx, posy);
		memcpy(&con->sel_end, &con->sel_start, sizeof(con->sel_end));
		con->sel_end.x = endx;

		selection_age(con, &con->sel_start, &con->sel_end);
	}
}

void tsm_screen_selection_line(struct tsm_screen *con,
                               unsigned int posy)
{
	if (!con)
		return;

	screen_inc_age(con);

	if (con->sel_active)
		selection_age(con, &con->sel_start, &con->sel_end);

	con->sel_active = true;
	selection_set(con, &con->sel_start, 0, posy);
	memcpy(&con->sel_end, &con->sel_start, sizeof(con->sel_end));
	con->sel_end.x = con->size_x - 1;

	selection_age(con, &con->sel_start, &con->sel_end);
}

/* calculates the line length from the beginning to the last non zero character */
static unsigned int calc_line_len(struct line *line)
{
	unsigned int line_len = 0;
	int i;

	for (i = 0; i < line->size; i++) {
		if (line->cells[i].ch != 0) {
			line_len = i + 1;
		}
	}

	return line_len;
}

/* TODO: tsm_ucs4_to_utf8 expects UCS4 characters, but a cell contains a
 * tsm-symbol (which can contain multiple UCS4 chars). Fix this when introducing
 * support for combining characters. */
static unsigned int copy_line(struct line *line, char *buf,
			      unsigned int start, unsigned int len)
{
	unsigned int i, end;
	char *pos = buf;
	int line_len;

	line_len = calc_line_len(line);
	if (start > line_len) {
		return 0;
	}

	end = start + len;

	if (end > line_len) {
		end = line_len;
	}

	for (i = start; i < line->size && i < end; ++i) {
		if (line->cells[i].ch != '\0')
			pos += tsm_ucs4_to_utf8(line->cells[i].ch, pos);
	}

	pos += tsm_ucs4_to_utf8('\n', pos);

	return pos - buf;
}

static void swap_selections(struct selection_pos **a, struct selection_pos **b)
{
	struct selection_pos *c;

	c  = *a;
	*a = *b;
	*b = c;
}

/*
 * Normalize a selection
 *
 * Start must always point to the top left and end to the bottom right cell
 */
static void norm_selection(struct tsm_screen *con, struct selection_pos **start, struct selection_pos **end)
{
	struct line *iter;

	if ((*end)->line == NULL && (*end)->y == SELECTION_TOP) {
		swap_selections(start, end);

		return;
	}

	if ((*start)->line && (*end)->line) {
		/* single line selection */
		if ((*start)->line == (*end)->line) {
			if ((*start)->x > (*end)->x) {
				swap_selections(start, end);
			}

			return;
		}

		/*
		 * multi line selection
		 *
		 * search from end->line to con->sb_last
		 * if we find start->line on the way we
		 * need to change start and end
		*/
		iter = (*end)->line;
		while (iter && iter != con->sb_last) {
			if (iter == (*start)->line) {
				swap_selections(start, end);
			}

			iter = iter->next;
		}

		return;
	}

	/* end is in scroll back buffer and start on screen */
	if (!(*start)->line && (*end)->line) {
		swap_selections(start, end);
		return;
	}

	/* reorder one-line selection if selection was created right to left */
	if ((*start)->y == (*end)->y) {
		if ((*start)->x > (*end)->x) {
			swap_selections(start, end);
		}

		return;
	}

	/* reorder multi-line selection if selection was created bottom to top */
	if ((*start)->y > (*end)->y) {
		swap_selections(start, end);
	}
}

/*
 * Counts the lines a normalized selection selects on the scroll back buffer
 *
 * Does not count the lines selected on the screen
 */
static int selection_count_lines_sb(struct tsm_screen *con, struct selection_pos *start, struct selection_pos *end)
{
	struct line *iter;
	int count = 0;

	/* Single line selection */
	if (start->line && (start->line == end->line)) {
		return 1;
	}

	iter = start->line;
	while (iter) {
		count++;

		if (iter == con->sb_last) {
			break;
		}

		iter = iter->next;
	}

	return count;
}

/*
 * Counts the lines a normalized selection selects on the screen
 *
 * Does not count the lines selected in the scroll back buffer
 */
static int selection_count_lines(struct selection_pos *start, struct selection_pos *end)
{
	/* Selection only spans lines of the scroll back buffer */
	if (start->line && end->line) {
		return 0;
	}

	return end->y - start->y + 1;
}

/*
 * Calculate the number of selected cells in a line
 */
static int calc_selection_line_len_sb(struct tsm_screen *con, struct selection_pos *start, struct selection_pos *end, struct line *line)
{
	/* one-line selection */
	if (start->line == end->line) {
		return end->x - start->x + 1;
	}

	/* first line of a multi-line selection */
	if (line == start->line) {
		return con->size_x - start->x;
	}

	/* last line of a multi-line selection */
	if (line == end->line) {
		return end->x + 1;
	}

	/* every other selection */
	return con->size_x;
}

/*
 * Calculate the number of selected cells in a line
 */
static int calc_selection_line_len(struct tsm_screen *con, struct selection_pos *start, struct selection_pos *end, int line_num)
{
	if (!start->line) {
		/* one-line selection */
		if (start->y == end->y) {
			return end->x - start->x + 1;
		}

		/* first line of a multi-line selection */
		if (line_num == start->y) {
			return con->size_x - start->x;
		}
	}

	/* last line of a multi-line selection */
	if (line_num == end->y) {
		return end->x + 1;
	}

	/* every other selection */
	return con->size_x;
}

/*
 * Calculate the maximum needed space for the number of lines given
 */
static unsigned int calc_line_copy_buffer(struct tsm_screen *con, unsigned int num_lines)
{
	// 4 is the max size of a Unicode character
	return con->size_x * num_lines * 4 + 1;
}

/*
 * Copy all selected lines from the scroll back buffer
 */
static int copy_lines_sb(struct tsm_screen *con, struct selection_pos *start, struct selection_pos *end, char *buf, int pos)
{
	struct line *iter;
	int line_x, line_len;

	if (!start->line) {
		return pos;
	}

	iter = start->line;
	while (iter) {
		line_x = 0;
		if (iter == start->line) {
			line_x = start->x;
		}

		line_len = calc_selection_line_len_sb(con, start, end, iter);
		pos += copy_line(iter, &(buf[pos]), line_x, line_len);

		if (iter == con->sb_last || iter == end->line) {
			break;
		}

		iter = iter->next;
	}

	return pos;
}

/*
 * Copy all selected lines from the regular screen
 */
static int copy_lines(struct tsm_screen *con, struct selection_pos *start, struct selection_pos *end, char *buf, int pos)
{
	int line_len, line_x, i;

	/* selection is scroll back buffer only */
	if (end->line) {
		return pos;
	}

	for (i = start->y; i <= end->y; i++) {
		line_len = calc_selection_line_len(con, start, end, i);

		line_x = 0;
		if (!start->line && i == start->y) {
			line_x = start->x;
		}

		pos += copy_line(con->lines[i], &(buf[pos]), line_x, line_len);
	}

	return pos;
}

SHL_EXPORT
int tsm_screen_selection_copy(struct tsm_screen *con, char **out)
{
	struct selection_pos *start, *end;
	struct selection_pos start_copy, end_copy;
	int buf_size = 0;
	int pos = 0;
	int total_lines;

	if (!con || !out) {
		return -EINVAL;
	}

	if (!con->sel_active) {
		return -ENOENT;
	}

	/*
	 * copy the selection start and end so we can modify it without affecting
	 * the screen in any way
	 */
	memcpy(&start_copy, &con->sel_start, sizeof(con->sel_start));
	memcpy(&end_copy, &con->sel_end, sizeof(con->sel_end));
	start = &start_copy;
	end   = &end_copy;

	/* invalid selection */
	if (start->y == SELECTION_TOP && start->line == NULL &&
		end->y == SELECTION_TOP && end->line == NULL) {
		*out = strdup("");
		return 0;
	}

	norm_selection(con, &start, &end);

	if (start->line == NULL && start->y == SELECTION_TOP) {
		if (con->sb_first != NULL) {
			start->line = con->sb_first;
			start->x = 0;
		} else {
			start->y = 0;
			start->x = 0;
		}
	}

	total_lines =  selection_count_lines_sb(con, start, end);
	total_lines += selection_count_lines(start, end);
	buf_size = calc_line_copy_buffer(con, total_lines);

	*out = calloc(buf_size, 1);
	if (!*out) {
		return -ENOMEM;
	}

	pos = copy_lines_sb(con, start, end, *out, pos);
	pos = copy_lines(con, start, end, *out, pos);

	/* remove last line break */
	if (pos > 0) {
		(*out)[--pos] = '\0';
	}

	return pos;
}

SHL_EXPORT
int tsm_screen_copy_all(struct tsm_screen *con, char **out)
{
	unsigned int len, i;
	struct line *iter;
	char *str, *pos;

	if (!con || !out)
		return -EINVAL;

	/* calculate size of buffer */
	len = 0;
	iter = con->sb_first;

	while (iter) {
		len += iter->size;

		++len;
		iter = iter->next;
	}

	for (i = 0; i < con->size_y; ++i) {
		len += con->size_x;

		++len;
	}

	/* allocate buffer */
	len *= 4;
	++len;
	str = malloc(len);
	if (!str)
		return -ENOMEM;
	pos = str;

	/* copy data into buffer */
	iter = con->sb_first;

	while (iter) {
		pos += copy_line(iter, pos, 0, iter->size);

		iter = iter->next;
	}

	for (i = 0; i < con->size_y; ++i) {
		iter = con->lines[i];

		pos += copy_line(iter, pos, 0, con->size_x);
	}

	/* return buffer */
	*pos = 0;
	*out = str;
	return pos - str;
}
