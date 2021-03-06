/*
 * Copyright (c) 2011 Tim van der Molen <tim@kariliq.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "attribute.h"
#include "compat.h"

/* File paths. */
#define CONF_DIR		".siren"
#define CACHE_FILE		"metadata"
#define CONF_FILE		"config"
#define LIBRARY_FILE		"library"
#define PLUGIN_IP_DIR		PLUGIN_DIR "/ip"
#define PLUGIN_OP_DIR		PLUGIN_DIR "/op"

/* Priority of input plug-ins. */
#define IP_PRIORITY_FLAC	0
#define IP_PRIORITY_MAD		0
#define IP_PRIORITY_OPUS	0
#define IP_PRIORITY_SNDFILE	0
#define IP_PRIORITY_VORBIS	0
#define IP_PRIORITY_WAVPACK	0
#define IP_PRIORITY_MPG123	1
#define IP_PRIORITY_FFMPEG	2
#define IP_PRIORITY_AAC		3

/* Priority of output plug-ins. */
#define OP_PRIORITY_SNDIO	0
#define OP_PRIORITY_PULSE	1
#define OP_PRIORITY_SUN		2
#define OP_PRIORITY_ALSA	3
#define OP_PRIORITY_OSS		4
#define OP_PRIORITY_AO		5
#define OP_PRIORITY_PORTAUDIO	6

/* Size of the buffer to be passed to strerror_r(). The value is arbitrary. */
#define STRERROR_BUFSIZE	256

/* Character attributes. */
#define ATTRIB_NORMAL		0x0
#define ATTRIB_BLINK		0x1
#define ATTRIB_BOLD		0x2
#define ATTRIB_DIM		0x4
#define ATTRIB_REVERSE		0x8
#define ATTRIB_STANDOUT		0x10
#define ATTRIB_UNDERLINE	0x20

/* Keys. */
#define K_NONE			0x100
#define K_BACKSPACE		0x101
#define K_BACKTAB		0x102
#define K_DELETE		0x103
#define K_DOWN			0x104
#define K_END			0x105
#define K_ENTER			0x106
#define K_ESCAPE		0x107
#define K_HOME			0x108
#define K_INSERT		0x109
#define K_LEFT			0x110
#define K_PAGEDOWN		0x111
#define K_PAGEUP		0x112
#define K_RIGHT			0x113
#define K_TAB			0x114
#define K_UP			0x115
#define K_F1			0x116
#define K_F2			0x117
#define K_F3			0x118
#define K_F4			0x119
#define K_F5			0x120
#define K_F6			0x121
#define K_F7			0x122
#define K_F8			0x123
#define K_F9			0x124
#define K_F10			0x125
#define K_F11			0x126
#define K_F12			0x127
#define K_F13			0x128
#define K_F14			0x129
#define K_F15			0x130
#define K_F16			0x131
#define K_F17			0x132
#define K_F18			0x133
#define K_F19			0x134
#define K_F20			0x135

/* Whether a character is a control character. */
#define K_IS_CTRL(c)		(((c) & ~0x1F) == 0 || (c) == 0x7F)

/*
 * Convert a control character to its matching printable character and vice
 * versa. For example, convert the ^A control character to "A". Conversion in
 * both directions is done by negating the 7th bit.
 */
#define K_CTRL(c)		((~(c) & 0x40) | ((c) & 0xBF))
#define K_UNCTRL(c)		K_CTRL(c)

/* Time conversion macros. */
#define HOURS(s)		((s) / 3600)
#define MINS(s)			((s) / 60)
#define MSECS(s)		((s) % 60)
#define HMINS(s)		(MINS(s) % 60)

/* Traverse each entry of a menu. */
#define MENU_FOR_EACH_ENTRY(menu, entry)				\
	for ((entry) = menu_get_first_entry(menu);			\
	    (entry) != NULL;						\
	    (entry) = menu_get_next_entry(entry))

#define MENU_FOR_EACH_ENTRY_REVERSE(menu, entry)			\
	for ((entry) = menu_get_last_entry(menu);			\
	    (entry) != NULL;						\
	    (entry) = menu_get_prev_entry(entry))

/* Number of items in an array. */
#ifndef nitems
#define nitems(a)		(sizeof (a) / sizeof (a)[0])
#endif

/*
 * Wrappers for log functions.
 */

#define LOG_ERR(...)		log_err(__func__, __VA_ARGS__)
#define LOG_ERRX(...)		log_errx(__func__, __VA_ARGS__)
#define LOG_FATAL(...)		log_fatal(__func__, __VA_ARGS__)
#define LOG_FATALX(...)		log_fatalx(__func__, __VA_ARGS__)
#define LOG_INFO(...)		log_info(__func__, __VA_ARGS__)
#define LOG_VERRX(...)		log_verrx(__func__, __VA_ARGS__)

#ifndef DEBUG
#define LOG_DEBUG(...)
#else
#define LOG_DEBUG(...)		log_info(__func__, __VA_ARGS__)
#endif

/*
 * Wrappers for pthreads functions.
 */

#define XPTHREAD_WRAPPER(func, ...)					\
	do								\
		if ((errno = pthread_ ## func(__VA_ARGS__)) != 0)	\
			LOG_FATAL("pthread_" #func);			\
	while (0)

#define XPTHREAD_COND_BROADCAST(cond)	XPTHREAD_WRAPPER(cond_broadcast, cond)
#define XPTHREAD_COND_DESTROY(cond)	XPTHREAD_WRAPPER(cond_destroy, cond)
#define XPTHREAD_COND_INIT(cond, attr)	XPTHREAD_WRAPPER(cond_init, cond, attr)
#define XPTHREAD_COND_WAIT(cond, mtx)	XPTHREAD_WRAPPER(cond_wait, cond, mtx)
#define XPTHREAD_CREATE(thd, attr, func, arg) \
	XPTHREAD_WRAPPER(create, thd, attr, func, arg)
#define XPTHREAD_JOIN(thd, ret)		XPTHREAD_WRAPPER(join, thd, ret)
#define XPTHREAD_MUTEX_LOCK(mtx)	XPTHREAD_WRAPPER(mutex_lock, mtx)
#define XPTHREAD_MUTEX_UNLOCK(mtx)	XPTHREAD_WRAPPER(mutex_unlock, mtx)

/* Scopes for key bindings. */
enum bind_scope {
	BIND_SCOPE_COMMON,
	BIND_SCOPE_BROWSER,
	BIND_SCOPE_LIBRARY,
	BIND_SCOPE_MENU,
	BIND_SCOPE_PLAYLIST,
	BIND_SCOPE_PROMPT,
	BIND_SCOPE_QUEUE
};

enum byte_order {
	BYTE_ORDER_BIG,
	BYTE_ORDER_LITTLE
};

enum cache_mode {
	CACHE_MODE_READ,
	CACHE_MODE_WRITE
};

enum colour {
	COLOUR_BLACK	= -1,
	COLOUR_BLUE	= -2,
	COLOUR_CYAN	= -3,
	COLOUR_DEFAULT	= -4,
	COLOUR_GREEN	= -5,
	COLOUR_MAGENTA	= -6,
	COLOUR_RED	= -7,
	COLOUR_WHITE	= -8,
	COLOUR_YELLOW	= -9
};

enum file_type {
	FILE_TYPE_DIRECTORY,
	FILE_TYPE_REGULAR,
	FILE_TYPE_OTHER
};

enum input_mode {
	INPUT_MODE_PROMPT,
	INPUT_MODE_VIEW
};

enum menu_scroll {
	MENU_SCROLL_HALF_PAGE,
	MENU_SCROLL_LINE,
	MENU_SCROLL_PAGE
};

enum option_type {
	OPTION_TYPE_ATTRIB,
	OPTION_TYPE_BOOLEAN,
	OPTION_TYPE_COLOUR,
	OPTION_TYPE_FORMAT,
	OPTION_TYPE_NUMBER,
	OPTION_TYPE_STRING
};

enum player_source {
	PLAYER_SOURCE_BROWSER,
	PLAYER_SOURCE_LIBRARY,
	PLAYER_SOURCE_PLAYLIST
};

enum view_id {
	VIEW_ID_BROWSER,
	VIEW_ID_LIBRARY,
	VIEW_ID_PLAYLIST,
	VIEW_ID_QUEUE
};

struct command;

struct history;

struct menu;

struct menu_entry;

struct dir;

struct dir_entry {
	char		*name;
	char		*path;
	size_t		 pathsize;
	enum file_type	 type;
};

struct format;

struct format_variable {
	const char		*lname;
	char			 sname;
	enum {
		FORMAT_VARIABLE_NUMBER,
		FORMAT_VARIABLE_STRING,
		FORMAT_VARIABLE_TIME
	} type;
	union {
		int			 number;
		unsigned int		 time;
		const char		*string;
	} value;
};

struct sample_buffer {
	void		*data;
	int8_t		*data1;
	int16_t		*data2;
	int32_t		*data4;

	size_t		 size_b;
	size_t		 size_s;
	size_t		 len_b;
	size_t		 len_s;

	unsigned int	 nbytes;
	int		 swap;
};

struct sample_format {
	enum byte_order	 byte_order;
	unsigned int	 nbits;
	unsigned int	 nchannels;
	unsigned int	 rate;
};

struct track {
	char		*path;

	const struct ip	*ip;
	void		*ipdata;

	char		*album;
	char		*albumartist;
	char		*artist;
	char		*comment;
	char		*date;
	char		*discnumber;
	char		*disctotal;
	char		*filename;
	char		*genre;
	char		*title;
	char		*tracknumber;
	char		*tracktotal;
	unsigned int	 duration;

	struct sample_format format;
};

/* Input plug-in. */
struct ip {
	const char	 *name;
	const int	  priority;
	const char	**extensions;
	void		  (*close)(struct track *) NONNULL();
	void		  (*get_metadata)(struct track *) NONNULL();
	int		  (*get_position)(struct track *, unsigned int *)
			    NONNULL();
	int		  (*init)(void);
	int		  (*open)(struct track *) NONNULL();
	int		  (*read)(struct track *, struct sample_buffer *)
			    NONNULL();
	void		  (*seek)(struct track *, unsigned int) NONNULL();
};

/* Output plug-in. */
struct op {
	const char	*name;
	const int	 priority;
	const char	*promises;
	void		 (*close)(void);
	size_t		 (*get_buffer_size)(void);
	int		 (*get_volume)(void);
	int		 (*get_volume_support)(void);
	int		 (*init)(void);
	int		 (*open)(void);
	void		 (*set_volume)(unsigned int);
	int		 (*start)(struct sample_format *) NONNULL();
	int		 (*stop)(void);
	int		 (*write)(struct sample_buffer *) NONNULL();
};

const char	*argv_error(int);
void		 argv_free(int, char **);
int		 argv_parse(const char *, int *, char ***);

void		 bind_end(void);
int		 bind_execute(enum bind_scope, int);
const char	*bind_get_command(enum bind_scope, int key);
void		 bind_init(void);
void		 bind_set(enum bind_scope, int, struct command *, void *,
		    const char *) NONNULL();
int		 bind_string_to_scope(const char *, enum bind_scope *)
		    NONNULL();
int		 bind_string_to_key(const char *) NONNULL();
int		 bind_unset(enum bind_scope, int);

void		 browser_activate_entry(void);
void		 browser_change_dir(const char *);
void		 browser_copy_entry(enum view_id);
void		 browser_end(void);
const char	*browser_get_dir(void);
struct track	*browser_get_next_track(void);
struct track	*browser_get_prev_track(void);
void		 browser_init(void);
void		 browser_print(void);
void		 browser_reactivate_entry(void);
void		 browser_refresh_dir(void);
void		 browser_scroll_down(enum menu_scroll);
void		 browser_scroll_up(enum menu_scroll);
void		 browser_search_next(const char *);
void		 browser_search_prev(const char *);
void		 browser_select_active_entry(void);
void		 browser_select_first_entry(void);
void		 browser_select_last_entry(void);
void		 browser_select_next_entry(void);
void		 browser_select_prev_entry(void);

void		 cache_close(void);
int		 cache_open(enum cache_mode);
int		 cache_read_entry(struct track *) NONNULL();
void		 cache_update(void);
void		 cache_write_entry(const struct track *) NONNULL();

void		 command_execute(struct command *, void *) NONNULL(1);
void		 command_free_data(struct command *, void *) NONNULL(1);
int		 command_parse_string(const char *, struct command **, void **,
		    char **) NONNULL();
int		 command_process(const char *, char **) NONNULL();

void		 conf_end(void);
void		 conf_init(const char *);
char		*conf_get_path(const char *) NONNULL();
void		 conf_read_file(void);
void		 conf_source_file(const char *) NONNULL();

void		 dir_close(struct dir *) NONNULL();
struct dir_entry *dir_get_entry(struct dir *) NONNULL();
struct dir	*dir_open(const char *) NONNULL();

void		 format_free(struct format *);
struct format	*format_parse(const char *) NONNULL();
void		 format_snprintf(char *, size_t, const struct format *,
		    const struct format_variable *, size_t) NONNULL();
const char	*format_to_string(const struct format *) NONNULL();
void		 format_track_snprintf(char *, size_t, const struct format *,
		    const struct format *, const struct track *) NONNULL();

void		 history_add(struct history *, const char *) NONNULL();
void		 history_free(struct history *) NONNULL();
const char	*history_get_next(struct history *) NONNULL();
const char	*history_get_prev(struct history *) NONNULL();
struct history	*history_init(void);
void		 history_rewind(struct history *) NONNULL();

void		 input_end(void);
enum input_mode	 input_get_mode(void);
void		 input_handle_key(void);
void		 input_init(void);
void		 input_set_mode(enum input_mode);

void		 library_activate_entry(void);
void		 library_add_dir(const char *) NONNULL();
void		 library_add_track(struct track *) NONNULL();
void		 library_copy_entry(enum view_id);
void		 library_delete_all_entries(void);
void		 library_delete_entry(void);
void		 library_end(void);
struct track	*library_get_next_track(void);
struct track	*library_get_prev_track(void);
void		 library_init(void);
void		 library_print(void);
void		 library_reactivate_entry(void);
void		 library_read_file(void);
void		 library_scroll_down(enum menu_scroll);
void		 library_scroll_up(enum menu_scroll);
void		 library_search_next(const char *);
void		 library_search_prev(const char *);
void		 library_select_active_entry(void);
void		 library_select_first_entry(void);
void		 library_select_last_entry(void);
void		 library_select_next_entry(void);
void		 library_select_prev_entry(void);
void		 library_update(void);
int		 library_write_file(void);

void		 log_end(void);
void		 log_err(const char *, const char *, ...) PRINTFLIKE2;
void		 log_errx(const char *, const char *, ...) PRINTFLIKE2;
void		 log_fatal(const char *, const char *, ...) NORETURN
		    PRINTFLIKE2;
void		 log_fatalx(const char *, const char *, ...) NORETURN
		    PRINTFLIKE2;
void		 log_info(const char *, const char *, ...) PRINTFLIKE2;
void		 log_init(int);
void		 log_verrx(const char *, const char *, va_list) VPRINTFLIKE2;

void		 menu_activate_entry(struct menu *, struct menu_entry *)
		    NONNULL();
void		 menu_free(struct menu *) NONNULL();
struct menu_entry *menu_get_active_entry(const struct menu *) NONNULL();
void		*menu_get_entry_data(const struct menu_entry *) NONNULL();
struct menu_entry *menu_get_first_entry(const struct menu *) NONNULL();
struct menu_entry *menu_get_last_entry(const struct menu *) NONNULL();
unsigned int	 menu_get_nentries(const struct menu *) NONNULL();
struct menu_entry *menu_get_next_entry(const struct menu_entry *) NONNULL();
struct menu_entry *menu_get_prev_entry(const struct menu_entry *) NONNULL();
struct menu_entry *menu_get_selected_entry(const struct menu *) NONNULL();
void		*menu_get_selected_entry_data(const struct menu *) NONNULL();
struct menu	*menu_init(void (*)(void *),
		    void (*)(const void *, char *, size_t),
		    int (*)(const void *, const char *));
void		 menu_insert_after(struct menu *, struct menu_entry *, void *)
		    NONNULL();
void		 menu_insert_before(struct menu *, struct menu_entry *,
		    void *) NONNULL();
void		 menu_insert_head(struct menu *, void *) NONNULL();
void		 menu_insert_tail(struct menu *, void *) NONNULL();
void		 menu_move_entry_before(struct menu *, struct menu_entry *,
		    struct menu_entry *) NONNULL();
void		 menu_move_entry_down(struct menu *, struct menu_entry *)
		    NONNULL();
void		 menu_move_entry_up(struct menu *, struct menu_entry *)
		    NONNULL();
void		 menu_print(struct menu *) NONNULL();
void		 menu_remove_all_entries(struct menu *) NONNULL();
void		 menu_remove_entry(struct menu *, struct menu_entry *)
		    NONNULL();
void		 menu_remove_selected_entry(struct menu *) NONNULL();
void		 menu_scroll_down(struct menu *, enum menu_scroll) NONNULL();
void		 menu_scroll_up(struct menu *, enum menu_scroll) NONNULL();
void		 menu_search_next(struct menu *, const char *) NONNULL();
void		 menu_search_prev(struct menu *, const char *) NONNULL();
void		 menu_select_active_entry(struct menu *) NONNULL();
void		 menu_select_entry(struct menu *, struct menu_entry *)
		    NONNULL();
void		 menu_select_first_entry(struct menu *) NONNULL();
void		 menu_select_last_entry(struct menu *) NONNULL();
void		 menu_select_next_entry(struct menu *) NONNULL();
void		 menu_select_prev_entry(struct menu *) NONNULL();

void		 msg_clear(void);
void		 msg_err(const char *, ...) PRINTFLIKE1;
void		 msg_errx(const char *, ...) PRINTFLIKE1;
void		 msg_info(const char *, ...) PRINTFLIKE1;

void		 option_add_number(const char *, int, int, int, void (*)(void))
		    NONNULL(1);
void		 option_add_string(const char *, const char *,
		    void (*)(void)) NONNULL(1, 2);
char		*option_attrib_to_string(int);
const char	*option_boolean_to_string(int);
char		*option_colour_to_string(int);
void		 option_end(void);
const char	*option_format_to_string(const struct format *) NONNULL();
int		 option_get_attrib(const char *) NONNULL();
int		 option_get_boolean(const char *) NONNULL();
int		 option_get_colour(const char *) NONNULL();
struct format	*option_get_format(const char *) NONNULL();
int		 option_get_number(const char *) NONNULL();
void		 option_get_number_range(const char *, int *, int *) NONNULL();
char		*option_get_string(const char *) NONNULL();
int		 option_get_type(const char *, enum option_type *) NONNULL();
void		 option_init(void);
void		 option_lock(void);
void		 option_set_attrib(const char *, int) NONNULL();
void		 option_set_boolean(const char *, int) NONNULL();
void		 option_set_colour(const char *, int) NONNULL();
void		 option_set_format(const char *, struct format *) NONNULL();
void		 option_set_number(const char *, int) NONNULL();
void		 option_set_string(const char *, const char *) NONNULL();
int		 option_string_to_attrib(const char *) NONNULL();
int		 option_string_to_boolean(const char *) NONNULL();
int		 option_string_to_colour(const char *, int *)
		    NONNULL();
void		 option_toggle_boolean(const char *) NONNULL();
void		 option_unlock(void);

char		*path_get_cwd(void);
char		*path_get_dirname(const char *);
char		*path_get_home_dir(const char *);
char		*path_normalise(const char *) NONNULL();

void		 player_change_op(void);
void		 player_end(void);
void		 player_forcibly_close_op(void);
enum byte_order	 player_get_byte_order(void);
void		 player_init(void);
void		 player_pause(void);
void		 player_play(void);
void		 player_play_next(void);
void		 player_play_prev(void);
void		 player_play_track(struct track *) NONNULL();
void		 player_print(void);
void		 player_reopen_op(void);
void		 player_seek(int, int);
void		 player_set_source(enum player_source);
void		 player_set_volume(int, int);
void		 player_stop(void);

void		 playlist_activate_entry(void);
void		 playlist_copy_entry(enum view_id);
void		 playlist_end(void);
struct track	*playlist_get_next_track(void);
struct track	*playlist_get_prev_track(void);
void		 playlist_init(void);
void		 playlist_load(const char *) NONNULL();
void		 playlist_print(void);
void		 playlist_reactivate_entry(void);
void		 playlist_scroll_down(enum menu_scroll);
void		 playlist_scroll_up(enum menu_scroll);
void		 playlist_search_next(const char *);
void		 playlist_search_prev(const char *);
void		 playlist_select_active_entry(void);
void		 playlist_select_first_entry(void);
void		 playlist_select_last_entry(void);
void		 playlist_select_next_entry(void);
void		 playlist_select_prev_entry(void);
void		 playlist_update(void);

void		 plugin_append_promises(char **) NONNULL();
void		 plugin_end(void);
void		 plugin_init(void);
const struct ip	*plugin_find_ip(const char *) NONNULL();
const struct op	*plugin_find_op(const char *) NONNULL();

void		 prompt_end(void);
void		 prompt_get_answer(const char *, void (*)(char *, void *),
		    void *) NONNULL(1, 2);
void		 prompt_get_command(const char *, void (*)(char *, void *),
		    void *) NONNULL(1, 2);
void		 prompt_get_search_query(const char *,
		    void (*)(char *, void *), void *) NONNULL(1, 2);
void		 prompt_handle_key(int);
void		 prompt_init(void);
void		 prompt_print(void);

void		 queue_activate_entry(void);
void		 queue_add_dir(const char *) NONNULL();
void		 queue_add_track(struct track *) NONNULL();
void		 queue_copy_entry(enum view_id);
void		 queue_delete_all_entries(void);
void		 queue_delete_entry(void);
void		 queue_end(void);
struct track	*queue_get_next_track(void);
void		 queue_init(void);
void		 queue_move_entry_down(void);
void		 queue_move_entry_up(void);
void		 queue_print(void);
void		 queue_scroll_down(enum menu_scroll);
void		 queue_scroll_up(enum menu_scroll);
void		 queue_search_next(const char *);
void		 queue_search_prev(const char *);
void		 queue_select_first_entry(void);
void		 queue_select_last_entry(void);
void		 queue_select_next_entry(void);
void		 queue_select_prev_entry(void);
void		 queue_update(void);

void		 screen_configure_cursor(void);
void		 screen_configure_objects(void);
void		 screen_end(void);
int		 screen_get_key(void);
int		 screen_get_ncolours(void);
unsigned int	 screen_get_ncols(void);
void		 screen_init(void);
void		 screen_msg_error_printf(const char *, ...) NONNULL()
		    PRINTFLIKE1;
void		 screen_msg_error_vprintf(const char *, va_list) NONNULL()
		    VPRINTFLIKE1;
void		 screen_msg_info_vprintf(const char *, va_list) NONNULL()
		    VPRINTFLIKE1;
void		 screen_player_status_printf(const struct format *,
		    const struct format_variable *, size_t) NONNULL();
void		 screen_player_track_printf(const struct format *,
		    const struct format *, const struct track *) NONNULL(1, 2);
void		 screen_print(void);
void		 screen_prompt_begin(void);
void		 screen_prompt_end(void);
void		 screen_prompt_printf(size_t, const char *, ...) NONNULL()
		    PRINTFLIKE2;
void		 screen_refresh(void);
void		 screen_status_clear(void);
unsigned int	 screen_view_get_nrows(void);
void		 screen_view_print(const char *) NONNULL();
void		 screen_view_print_active(const char *) NONNULL();
void		 screen_view_print_begin(void);
void		 screen_view_print_end(void);
void		 screen_view_print_selected(const char *) NONNULL();
void		 screen_view_title_printf(const char *, ...) PRINTFLIKE1;
void		 screen_view_title_printf_right(const char *, ...) PRINTFLIKE1;

int		 track_cmp(const struct track *, const struct track *)
		    NONNULL();
void		 track_copy_vorbis_comment(struct track *, const char *);
void		 track_end(void);
struct track	*track_get(char *, const struct ip *) NONNULL(1);
void		 track_init(void);
void		 track_lock_metadata(void);
struct track	*track_require(char *);
int		 track_search(const struct track *, const char *);
void		 track_split_tag(const char *, char **, char **);
void		 track_unlock_metadata(void);
void		 track_update_metadata(int);
int		 track_write_cache(void);

void		 view_activate_entry(void);
void		 view_add_dir(enum view_id, const char *) NONNULL();
void		 view_add_track(enum view_id, struct track *) NONNULL();
void		 view_copy_entry(enum view_id);
void		 view_delete_all_entries(void);
void		 view_delete_entry(void);
enum view_id	 view_get_id(void);
void		 view_handle_key(int);
void		 view_move_entry_down(void);
void		 view_move_entry_up(void);
void		 view_print(void);
void		 view_reactivate_entry(void);
void		 view_scroll_down(enum menu_scroll);
void		 view_scroll_up(enum menu_scroll);
void		 view_search_next(const char *);
void		 view_search_prev(const char *);
void		 view_select_active_entry(void);
void		 view_select_first_entry(void);
void		 view_select_last_entry(void);
void		 view_select_next_entry(void);
void		 view_select_prev_entry(void);
void		 view_select_view(enum view_id);

int		 xasprintf(char **, const char *, ...) NONNULL() PRINTFLIKE2;
void		*xmalloc(size_t);
void		*xrealloc(void *, size_t);
void		*xreallocarray(void *, size_t, size_t);
int		 xsnprintf(char *, size_t, const char *, ...) PRINTFLIKE3;
char		*xstrdup(const char *) NONNULL();
char		*xstrndup(const char *, size_t) NONNULL();
int		 xvasprintf(char **, const char *, va_list) NONNULL()
		    VPRINTFLIKE2;
int		 xvsnprintf(char *, size_t, const char *, va_list) NONNULL()
		    VPRINTFLIKE3;
