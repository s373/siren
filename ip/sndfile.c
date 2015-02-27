/*
 * Copyright (c) 2011 Tim van der Molen <tbvdm@xs4all.nl>
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

/* Silence gcc. */
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 0

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <sndfile.h>

#include "../siren.h"

struct ip_sndfile_ipdata {
	SNDFILE		*sffp;
	sf_count_t	 position;	/* Current position, in samples. */
};

static void		 ip_sndfile_close(struct track *);
static int		 ip_sndfile_get_metadata(struct track *);
static int		 ip_sndfile_get_position(struct track *,
			    unsigned int *);
static int		 ip_sndfile_open(struct track *);
static int		 ip_sndfile_read(struct track *, int16_t *, size_t);
static void		 ip_sndfile_seek(struct track *, unsigned int);

/*
 * Based on <http://www.mega-nerd.com/libsndfile/> and src/command.c in the
 * libsndfile distribution.
 */
static const char	*ip_sndfile_extensions[] = {
	"aif", "aifc", "aiff",	/* AIFF; AIFF-C (compressed) */
	"au", "snd",		/* NeXT/Sun audio */
	"avr",			/* Audio Visual Research */
	"caf",			/* Apple Core Audio Format */
	"htk",			/* Hidden Markov Model Toolkit */
	"iff", "svx",		/* Commodore Amiga IFF/8SVX */
	"mat",			/* MATLAB or GNU Octave */
	"mpc",			/* Akai Music Production Center */
	"nist", "sph",		/* NIST/Sphere WAVE */
	"paf",			/* Ensoniq PARIS audio file */
	"pvf",			/* Portable Voice Format */
	"rf64",			/* EBU MBWF/RF64 */
	"sd2",			/* Sound Designer II */
	"sds",			/* MIDI Sample Dump Standard */
	"sf",			/* IRCAM SF */
	"voc",			/* Creative Sound Blaster voice */
	"w64",			/* Sony Sound Forge Wave64 */
	"wav", "wave",		/* Microsoft WAVE */
	"wve",			/* Psion Series 3 WVE */
	"xi",			/* FastTracker 2 XI */
	NULL
};

const struct ip		 ip = {
	"sndfile",
	ip_sndfile_extensions,
	ip_sndfile_close,
	ip_sndfile_get_metadata,
	ip_sndfile_get_position,
	ip_sndfile_open,
	ip_sndfile_read,
	ip_sndfile_seek
};

static void
ip_sndfile_close(struct track *t)
{
	struct ip_sndfile_ipdata *ipd;

	ipd = t->ipdata;

	sf_close(ipd->sffp);
	free(ipd);
}

static int
ip_sndfile_get_metadata(struct track *t)
{
	SNDFILE		*sffp;
	SF_INFO		 sfinfo;
	int		 fd;
	const char	*value;

	if ((fd = open(t->path, O_RDONLY)) == -1) {
		LOG_ERR("open: %s", t->path);
		msg_err("%s: Cannot open track", t->path);
		return -1;
	}

	sfinfo.format = 0;
	if ((sffp = sf_open_fd(fd, SFM_READ, &sfinfo, SF_TRUE)) == NULL) {
		LOG_ERRX("sf_open_fd: %s: %s", t->path, sf_strerror(sffp));
		msg_errx("%s: Cannot open track: %s", t->path,
		    sf_strerror(sffp));
		close(fd);
		return -1;
	}

	if ((value = sf_get_string(sffp, SF_STR_ALBUM)) != NULL)
		t->album = xstrdup(value);
	if ((value = sf_get_string(sffp, SF_STR_ARTIST)) != NULL)
		t->artist = xstrdup(value);
	if ((value = sf_get_string(sffp, SF_STR_DATE)) != NULL)
		t->date = xstrdup(value);
#ifdef HAVE_SF_STR_GENRE
	if ((value = sf_get_string(sffp, SF_STR_GENRE)) != NULL)
		t->genre = xstrdup(value);
#endif
	if ((value = sf_get_string(sffp, SF_STR_TITLE)) != NULL)
		t->title = xstrdup(value);
#ifdef HAVE_SF_STR_TRACKNUMBER
	if ((value = sf_get_string(sffp, SF_STR_TRACKNUMBER)) != NULL)
		t->tracknumber = xstrdup(value);
#endif

	if (sfinfo.frames < 0 || sfinfo.samplerate <= 0)
		t->duration = 0;
	else
		t->duration = sfinfo.frames / sfinfo.samplerate;

	sf_close(sffp);

	return 0;
}

static int
ip_sndfile_get_position(struct track *t, unsigned int *pos)
{
	struct ip_sndfile_ipdata *ipd;

	ipd = t->ipdata;

	if (t->format.nchannels <= 0 || t->format.rate <= 0)
		*pos = 0;
	else
		*pos = ipd->position / t->format.nchannels / t->format.rate;

	return 0;
}

static int
ip_sndfile_open(struct track *t)
{
	struct ip_sndfile_ipdata *ipd;
	SF_INFO	sfinfo;
	int	fd;

	if ((fd = open(t->path, O_RDONLY)) == -1) {
		LOG_ERR("open: %s", t->path);
		msg_err("%s: Cannot open track", t->path);
		return -1;
	}

	ipd = xmalloc(sizeof *ipd);
	ipd->position = 0;

	sfinfo.format = 0;
	if ((ipd->sffp = sf_open_fd(fd, SFM_READ, &sfinfo, SF_TRUE)) == NULL) {
		LOG_ERRX("sf_open_fd: %s: %s", t->path,
		    sf_strerror(ipd->sffp));
		msg_errx("%s: Cannot open track: %s", t->path,
		    sf_strerror(ipd->sffp));
		free(ipd);
		close(fd);
		return -1;
	}

	t->format.nbits = 16;
	t->format.nchannels = sfinfo.channels;
	t->format.rate = sfinfo.samplerate;

	t->ipdata = ipd;
	return 0;
}

static int
ip_sndfile_read(struct track *t, int16_t *samples, size_t maxsamples)
{
	struct ip_sndfile_ipdata *ipd;
	size_t n;

	ipd = t->ipdata;

	/* Assume, like libsndfile, that short ints always are 2 bytes long. */
	n = sf_read_short(ipd->sffp, (short int *)samples, maxsamples);

	if (sf_error(ipd->sffp) != SF_ERR_NO_ERROR) {
		LOG_ERRX("sf_read_short: %s: %s", t->path,
		    sf_strerror(ipd->sffp));
		msg_errx("Cannot read from track: %s", sf_strerror(ipd->sffp));
		return -1;
	}

	ipd->position += n;
	return n;
}

static void
ip_sndfile_seek(struct track *t, unsigned int pos)
{
	struct ip_sndfile_ipdata *ipd;
	sf_count_t frame, seekframe;

	ipd = t->ipdata;

	seekframe = pos * t->format.rate;
	if ((frame = sf_seek(ipd->sffp, seekframe, SEEK_SET)) >= 0)
		ipd->position = frame * t->format.nchannels;
	else {
		LOG_ERRX("sf_seek: %s: %s", t->path, sf_strerror(ipd->sffp));
		msg_errx("Cannot seek: %s", sf_strerror(ipd->sffp));
	}
}
