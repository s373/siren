/*
 * Copyright (c) 2015 Tim van der Molen <tim@kariliq.nl>
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

#include "../config.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <mp4v2/mp4v2.h>
#include <neaacdec.h>

#include "../siren.h"

#if MP4V2_PROJECT_version_hex < 0x00020000
#define IP_AAC_OLD_MP4V2_API
#endif

#ifdef IP_AAC_OLD_MP4V2_API
#define MP4Close(hdl, flags)	MP4Close(hdl)
#define MP4Read(path)		MP4Read(path, MP4_DETAILS_ERROR)
#define MP4SetLogCallback(func)	MP4SetLibFunc(func)
#define MP4TagsFetch(tag, hdl)	(MP4TagsFetch(tag, hdl), 1)
#endif

struct ip_aac_ipdata {
	MP4FileHandle	 hdl;
	MP4TrackId	 track;
	MP4SampleId	 nsamples;
	MP4SampleId	 sample;
	MP4Duration	 pos;
	NeAACDecHandle	 dec;
	uint32_t	 aacbufsize;
	uint8_t		*aacbuf;
	unsigned long	 pcmbuflen;
	char		*pcmbuf;
};

static void		 ip_aac_close(struct track *);
static void		 ip_aac_get_metadata(struct track *);
static int		 ip_aac_get_position(struct track *, unsigned int *);
static int		 ip_aac_init(void);
static int		 ip_aac_open(struct track *);
static int		 ip_aac_read(struct track *, struct sample_buffer *);
static void		 ip_aac_seek(struct track *, unsigned int);

static const char	*ip_aac_extensions[] = { "aac", "m4a", "m4b", "mp4",
    NULL };

const struct ip		 ip = {
	"aac",
	IP_PRIORITY_AAC,
	ip_aac_extensions,
	ip_aac_close,
	ip_aac_get_metadata,
	ip_aac_get_position,
	ip_aac_init,
	ip_aac_open,
	ip_aac_read,
	ip_aac_seek
};

#ifdef IP_AAC_OLD_MP4V2_API
PRINTFLIKE3 static void
ip_aac_log(UNUSED int loglevel, UNUSED const char *lib, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	LOG_VERRX(fmt, ap);
	va_end(ap);
}
#else
VPRINTFLIKE2 static void
ip_aac_log(UNUSED MP4LogLevel loglevel, const char *fmt, va_list ap)
{
	LOG_VERRX(fmt, ap);
}
#endif

static MP4TrackId
ip_aac_get_aac_track(MP4FileHandle hdl)
{
	uint32_t	 i, ntracks;
	MP4TrackId	 trk;
	uint8_t		 obj;

	/* Look for an audio track with AAC audio. */
	ntracks = MP4GetNumberOfTracks(hdl, MP4_AUDIO_TRACK_TYPE, 0);
	for (i = 0; i < ntracks; i++) {
		trk = MP4FindTrackId(hdl, i, MP4_AUDIO_TRACK_TYPE, 0);
		obj = MP4GetTrackEsdsObjectTypeId(hdl, trk);
		if (MP4_IS_AAC_AUDIO_TYPE(obj))
			return trk;
	}

	return MP4_INVALID_TRACK_ID;
}

static int
ip_aac_open_file(const char *path, MP4FileHandle *hdl, MP4TrackId *trk)
{
	*hdl = MP4Read(path);
	if (*hdl == MP4_INVALID_FILE_HANDLE) {
		LOG_ERRX("%s: MP4Read() failed", path);
		msg_errx("%s: Cannot open file", path);
		return -1;
	}

	*trk = ip_aac_get_aac_track(*hdl);
	if (*trk == MP4_INVALID_TRACK_ID) {
		LOG_ERRX("%s: cannot find AAC track", path);
		msg_errx("%s: Cannot find AAC track", path);
		MP4Close(*hdl, 0);
		return -1;
	}

	return 0;
}

static int
ip_aac_fill_buffer(struct track *t, struct ip_aac_ipdata *ipd)
{
	NeAACDecFrameInfo	 frame;
	uint32_t		 buflen;
	char			*errmsg;

	for (;;) {
		if (ipd->sample > ipd->nsamples)
			return 0; /* EOF reached */

		buflen = ipd->aacbufsize;
		if (!MP4ReadSample(ipd->hdl, ipd->track, ipd->sample,
		    &ipd->aacbuf, &buflen, NULL, NULL, NULL, NULL)) {
			LOG_ERRX("%s: MP4ReadSample() failed", t->path);
			msg_errx("Cannot read from file");
			return -1;
		}

		ipd->pos += MP4GetSampleDuration(ipd->hdl, ipd->track,
		    ipd->sample);
		ipd->sample++;

		ipd->pcmbuf = NeAACDecDecode(ipd->dec, &frame, ipd->aacbuf,
		    buflen);
		if (frame.error) {
			errmsg = NeAACDecGetErrorMessage(frame.error);
			LOG_ERRX("NeAACDecDecode: %s: %s", t->path, errmsg);
			msg_errx("Cannot read from file: %s", errmsg);
			return -1;
		}
		if (frame.samples > 0) {
			/* 16-bit samples */
			ipd->pcmbuflen = frame.samples * 2;
			return 1;
		}
	}
}

static void
ip_aac_close(struct track *t)
{
	struct ip_aac_ipdata *ipd;

	ipd = t->ipdata;
	NeAACDecClose(ipd->dec);
	MP4Close(ipd->hdl, 0);
	free(ipd->aacbuf);
	free(ipd);
}

static void
ip_aac_get_metadata(struct track *t)
{
	MP4FileHandle		 hdl;
	MP4TrackId		 trk;
	const MP4Tags		*tag;

	if (ip_aac_open_file(t->path, &hdl, &trk) == -1)
		return;

	tag = MP4TagsAlloc();
	if (tag == NULL) {
		LOG_ERRX("%s: MP4TagsAlloc() failed", t->path);
		msg_errx("%s: Cannot get metadata", t->path);
		MP4Close(hdl, 0);
		return;
	}

	if (!MP4TagsFetch(tag, hdl)) {
		LOG_ERRX("%s: MP4TagsFetch failed", t->path);
		msg_errx("%s: Cannot get metadata", t->path);
		MP4TagsFree(tag);
		MP4Close(hdl, 0);
		return;
	}

	if (tag->album != NULL)
		t->album = xstrdup(tag->album);
	if (tag->albumArtist != NULL)
		t->albumartist = xstrdup(tag->albumArtist);
	if (tag->artist != NULL)
		t->artist = xstrdup(tag->artist);
	if (tag->comments != NULL)
		t->comment = xstrdup(tag->comments);
	if (tag->releaseDate != NULL)
		t->date = xstrdup(tag->releaseDate);
	if (tag->genre != NULL)
		t->genre = xstrdup(tag->genre);
	if (tag->name != NULL)
		t->title = xstrdup(tag->name);
	if (tag->disk != NULL) {
		xasprintf(&t->discnumber, "%u", tag->disk->index);
		xasprintf(&t->disctotal, "%u", tag->disk->total);
	}
	if (tag->track != NULL) {
		xasprintf(&t->tracknumber, "%u", tag->track->index);
		xasprintf(&t->tracktotal, "%u", tag->track->total);
	}

	t->duration = MP4ConvertFromTrackDuration(hdl, trk,
	    MP4GetTrackDuration(hdl, trk), MP4_SECS_TIME_SCALE);

	MP4TagsFree(tag);
	MP4Close(hdl, 0);
}

static int
ip_aac_get_position(struct track *t, unsigned int *pos)
{
	struct ip_aac_ipdata *ipd;

	ipd = t->ipdata;
	*pos = MP4ConvertFromTrackDuration(ipd->hdl, ipd->track, ipd->pos,
	    MP4_SECS_TIME_SCALE);
	return 0;
}

static int
ip_aac_init(void)
{
	MP4SetLogCallback(ip_aac_log);
	return 0;
}

static int
ip_aac_open(struct track *t)
{
	struct ip_aac_ipdata		*ipd;
	NeAACDecConfigurationPtr	 cfg;
	uint8_t				*esc;
	uint32_t			 escsize;
	unsigned long			 rate;
	unsigned char			 nchan;

	ipd = xmalloc(sizeof *ipd);

	if (ip_aac_open_file(t->path, &ipd->hdl, &ipd->track) == -1)
		goto error1;

	ipd->aacbufsize = MP4GetTrackMaxSampleSize(ipd->hdl, ipd->track);
	if (ipd->aacbufsize == 0) {
		/* Avoid zero-size allocation. */
		LOG_ERRX("%s: MP4GetTrackMaxSampleSize() returned 0", t->path);
		goto error2;
	}

	ipd->dec = NeAACDecOpen();
	if (ipd->dec == NULL) {
		LOG_ERRX("%s: NeAACDecOpen() failed", t->path);
		goto error2;
	}

	cfg = NeAACDecGetCurrentConfiguration(ipd->dec);
	cfg->outputFormat = FAAD_FMT_16BIT;
	cfg->downMatrix = 1; /* Down-matrix 5.1 channels to 2 */
	if (NeAACDecSetConfiguration(ipd->dec, cfg) != 1) {
		LOG_ERRX("%s: NeAACDecSetConfiguration() failed", t->path);
		goto error3;
	}

	if (!MP4GetTrackESConfiguration(ipd->hdl, ipd->track, &esc,
	    &escsize)) {
		LOG_ERRX("%s: MP4GetTrackESConfiguration() failed", t->path);
		goto error3;
	}

	if (NeAACDecInit2(ipd->dec, esc, escsize, &rate, &nchan) != 0) {
		LOG_ERRX("%s: NeAACDecInit2() failed", t->path);
		free(esc);
		goto error3;
	}
	free(esc);

	ipd->nsamples = MP4GetTrackNumberOfSamples(ipd->hdl, ipd->track);
	ipd->sample = 1;
	ipd->pos = 0;
	ipd->aacbuf = xmalloc(ipd->aacbufsize);
	ipd->pcmbuflen = 0;

	t->format.nbits = 16;
	t->format.nchannels = nchan;
	t->format.rate = rate;
	t->ipdata = ipd;

	return 0;

error3:
	NeAACDecClose(ipd->dec);
error2:
	MP4Close(ipd->hdl, 0);
error1:
	free(ipd);
	msg_errx("%s: Cannot open file", t->path);
	return -1;
}

static int
ip_aac_read(struct track *t, struct sample_buffer *sb)
{
	struct ip_aac_ipdata	*ipd;
	char			*buf;
	size_t			 len, bufsize;
	int			 ret;

	ipd = t->ipdata;
	buf = (char *)sb->data;
	bufsize = sb->size_b;

	while (bufsize > 0) {
		if (ipd->pcmbuflen == 0) {
			ret = ip_aac_fill_buffer(t, ipd);
			if (ret == 0)
				break;		/* EOF */
			if (ret == -1)
				return -1;	/* Error */
		}
		len = (bufsize < ipd->pcmbuflen) ? bufsize : ipd->pcmbuflen;
		memcpy(buf, ipd->pcmbuf, len);
		buf += len;
		bufsize -= len;
		ipd->pcmbuf += len;
		ipd->pcmbuflen -= len;
	}

	sb->len_b = sb->size_b - bufsize;
	sb->len_s = sb->len_b / sb->nbytes;
	return sb->len_s != 0;
}

static void
ip_aac_seek(struct track *t, unsigned int pos)
{
	struct ip_aac_ipdata	*ipd;
	MP4SampleId		 sample;
	MP4Timestamp		 tim;

	ipd = t->ipdata;
	tim = MP4ConvertToTrackTimestamp(ipd->hdl, ipd->track, pos,
	    MP4_SECS_TIME_SCALE);
	sample = MP4GetSampleIdFromTime(ipd->hdl, ipd->track, tim, true);
	if (sample != MP4_INVALID_SAMPLE_ID) {
		ipd->sample = sample;
		ipd->pos = MP4GetSampleTime(ipd->hdl, ipd->track, sample);
	}
}
