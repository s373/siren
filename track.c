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

/* Let glibc expose strcasestr(). */
#define _GNU_SOURCE

#ifdef __OpenBSD__
#include <sys/tree.h>
#else
#include "compat/tree.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "siren.h"

struct track_entry {
	struct track		track;
	int			delete;
	RB_ENTRY(track_entry)	entries;
};

RB_HEAD(track_tree, track_entry);

static int		 track_cmp_entry(struct track_entry *,
			    struct track_entry *);
static int		 track_cmp_number(const char *, const char *);
static int		 track_cmp_string(const char *, const char *);
static void		 track_free_entry(struct track_entry *);
static void		 track_free_metadata(struct track_entry *);
static void		 track_init_metadata(struct track_entry *);
static void		 track_read_cache(void);

RB_PROTOTYPE(track_tree, track_entry, entries, track_cmp_entry)

static pthread_mutex_t	 track_metadata_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct track_tree track_tree = RB_INITIALIZER(track_tree);
static size_t		 track_nentries;
static int		 track_tree_modified;

RB_GENERATE(track_tree, track_entry, entries, track_cmp_entry)

static int
track_add_entry(struct track_entry *te)
{
	if (track_nentries == SIZE_MAX)
		return -1;

	if (RB_INSERT(track_tree, &track_tree, te) != NULL) {
		/* This should not happen. */
		LOG_ERRX("%s: track already in tree", te->track.path);
		return -1;
	}

	track_nentries++;
	return 0;
}

static struct track *
track_add_new_entry(char *path, const struct ip *ip)
{
	struct track_entry *te;

	te = xmalloc(sizeof *te);
	te->delete = 0;
	te->track.path = xstrdup(path);
	te->track.ip = (ip != NULL) ? ip : plugin_find_ip(path);
	te->track.ipdata = NULL;
	track_init_metadata(te);

	if (te->track.ip != NULL)
		te->track.ip->get_metadata(&te->track);

	if (track_add_entry(te) == -1) {
		track_free_entry(te);
		return NULL;
	}

	track_tree_modified = 1;
	return &te->track;
}

int
track_cmp(const struct track *t1, const struct track *t2)
{
	int ret;

	if ((ret = track_cmp_string(t1->artist, t2->artist)))
		return ret;
	if ((ret = track_cmp_number(t1->date, t2->date)))
		return ret;
	if ((ret = track_cmp_string(t1->album, t2->album)))
		return ret;
	if ((ret = track_cmp_number(t1->discnumber, t2->discnumber)))
		return ret;
	if ((ret = track_cmp_number(t1->tracknumber, t2->tracknumber)))
		return ret;
	if ((ret = track_cmp_string(t1->title, t2->title)))
		return ret;
	return strcmp(t1->path, t2->path);
}

static int
track_cmp_entry(struct track_entry *t1, struct track_entry *t2)
{
	return strcmp(t1->track.path, t2->track.path);
}

static int
track_cmp_number(const char *s1, const char *s2)
{
	int		 i1, i2;
	const char	*errstr;

	if (s1 == NULL)
		return (s2 == NULL) ? 0 : -1;
	if (s2 == NULL)
		return 1;

	i1 = strtonum(s1, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		return strcasecmp(s1, s2);

	i2 = strtonum(s2, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		return strcasecmp(s1, s2);

	return (i1 < i2) ? -1 : (i1 > i2);
}

static int
track_cmp_string(const char *s1, const char *s2)
{
	if (s1 == NULL)
		return (s2 == NULL) ? 0: -1;
	if (s2 == NULL)
		return 1;
	return strcasecmp(s1, s2);
}

void
track_end(void)
{
	struct track_entry *te;

	if (track_tree_modified)
		track_write_cache();

	while ((te = RB_ROOT(&track_tree)) != NULL) {
		RB_REMOVE(track_tree, &track_tree, te);
		track_free_entry(te);
	}
}

static struct track_entry *
track_find_entry(char *path, const struct ip *ip)
{
	struct track_entry search, *te;

	search.track.path = path;
	te = RB_FIND(track_tree, &track_tree, &search);
	if (te != NULL && te->track.ip == NULL)
		te->track.ip = (ip != NULL) ? ip : plugin_find_ip(path);
	return te;
}

static void
track_free_entry(struct track_entry *te)
{
	track_free_metadata(te);
	free(te->track.path);
	free(te);
}

static void
track_free_metadata(struct track_entry *te)
{
	free(te->track.album);
	free(te->track.artist);
	free(te->track.date);
	free(te->track.genre);
	free(te->track.title);
	free(te->track.tracknumber);
}

struct track *
track_get(char *path, const struct ip *ip)
{
	struct track_entry *te;

	te = track_find_entry(path, ip);
	if (te != NULL) {
		if (te->track.ip != NULL)
			return &te->track;
		else {
			msg_errx("%s: Unsupported file format", path);
			return NULL;
		}
	}

	if (ip == NULL) {
		ip = plugin_find_ip(path);
		if (ip == NULL) {
			msg_errx("%s: Unsupported file format", path);
			return NULL;
		}
	}

	return track_add_new_entry(path, ip);
}

void
track_init(void)
{
	track_read_cache();
}

static void
track_init_metadata(struct track_entry *te)
{
	te->track.album = NULL;
	te->track.artist = NULL;
	te->track.date = NULL;
	te->track.discnumber = NULL;
	te->track.genre = NULL;
	te->track.title = NULL;
	te->track.tracknumber = NULL;
	te->track.duration = 0;
}

void
track_lock_metadata(void)
{
	XPTHREAD_MUTEX_LOCK(&track_metadata_mtx);
}

static void
track_read_cache(void)
{
	struct track_entry *te;

	if (cache_open(CACHE_MODE_READ) == -1)
		return;

	for (;;) {
		te = xmalloc(sizeof *te);
		te->delete = 0;
		if (cache_read_entry(&te->track) == -1) {
			track_free_entry(te);
			break;
		}
		if (track_add_entry(te) == -1)
			track_free_entry(te);
	}

	cache_close();
}

struct track *
track_require(char *path)
{
	struct track_entry *te;

	te = track_find_entry(path, NULL);
	return (te != NULL) ? &te->track : track_add_new_entry(path, NULL);
}

int
track_search(const struct track *t, const char *search)
{
	if (t->album != NULL && strcasestr(t->album, search))
		return 0;
	if (t->artist != NULL && strcasestr(t->artist, search))
		return 0;
	if (t->date != NULL && strcasestr(t->date, search))
		return 0;
	if (t->genre != NULL && strcasestr(t->genre, search))
		return 0;
	if (t->title != NULL && strcasestr(t->title, search))
		return 0;
	if (t->tracknumber != NULL && strcasestr(t->tracknumber, search))
		return 0;
	if (strcasestr(t->path, search))
		return 0;
	return -1;
}

void
track_unlock_metadata(void)
{
	XPTHREAD_MUTEX_UNLOCK(&track_metadata_mtx);
}

void
track_update_metadata(int delete)
{
	struct track_entry	*te;
	size_t			 i;

	i = 1;
	RB_FOREACH(te, track_tree, &track_tree) {
		msg_info("Updating track %zu of %zu (%zu%%)", i,
		    track_nentries, 100 * i / track_nentries);
		i++;

		if (access(te->track.path, F_OK) == -1) {
			if (delete)
				te->delete = 1;
			continue;
		}

		if (te->track.ip == NULL) {
			te->track.ip = plugin_find_ip(te->track.path);
			if (te->track.ip == NULL) {
				LOG_ERRX("%s: no ip found", te->track.path);
				continue;
			}
		}

		track_lock_metadata();
		track_free_metadata(te);
		track_init_metadata(te);
		te->track.ip->get_metadata(&te->track);
		track_unlock_metadata();
	}

	track_tree_modified = 1;
}

int
track_write_cache(void)
{
	struct track_entry *te;

	if (cache_open(CACHE_MODE_WRITE) == -1)
		return -1;

	RB_FOREACH(te, track_tree, &track_tree)
		if (!te->delete)
			cache_write_entry(&te->track);

	cache_close();
	track_tree_modified = 0;
	return 0;
}
