/*
** Copyright (C) 2019 Erik de Castro Lopo <erikd@mega-nerd.com>
** Copyright (C) 2019 Arthur Taylor <art@ified.ca>
**
** This program is free software ; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation ; either version 2.1 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY ; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this program ; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include	"sfconfig.h"

#include	<math.h>

#include	"sndfile.h"
#include	"common.h"

#include	"mpeg.h"

#if (ENABLE_EXPERIMENTAL_CODE && HAVE_MPEG)

#include <mpg123.h>

/* TODO
 * ID3v2 support.
 */

typedef struct
{	mpg123_handle *pmh ;
	/* TODO: Other members? Remove this struct? */
} MPEG_DEC_PRIVATE ;

static int mpeg_dec_close (SF_PRIVATE *psf) ;
static sf_count_t mpeg_dec_seek (SF_PRIVATE *psf, int whence, sf_count_t count) ;

static ssize_t mpeg_dec_io_read (void *priv, void *buffer, size_t nbytes) ;
static off_t mpeg_dec_io_lseek (void *priv, off_t offset, int whence) ;

static ssize_t
mpeg_dec_io_read (void *priv, void *buffer, size_t nbytes)
{	SF_PRIVATE *psf = (SF_PRIVATE *) priv ;

	return psf_fread (buffer, 1, nbytes, psf) ;
}

static off_t
mpeg_dec_io_lseek (void *priv, off_t offset, int whence)
{	SF_PRIVATE *psf = (SF_PRIVATE *) priv ;

	return psf_fseek (psf, offset, whence) ;
}


static int
mpeg_dec_close (SF_PRIVATE *psf)
{	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;

	if (pmp3d)
	{	if (pmp3d->pmh)
		{	mpg123_close (pmp3d->pmh) ;
			mpg123_delete (pmp3d->pmh) ;
			pmp3d->pmh = NULL ;
			}
		free (psf->codec_data) ;
		psf->codec_data = NULL ;
		} ;

	return 0 ;
}

static inline void
f2s_array (const float *src, int count, short *dest)
{	while (--count >= 0)
	{	dest [count] = lrintf (src [count] * 0x7FFF) ;
		} ;
} /* f2s_array */

static sf_count_t
mpeg_dec_read_s (SF_PRIVATE *psf, short *ptr, sf_count_t len)
{	BUF_UNION ubuf ;
	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;
	int bufferlen ;
	sf_count_t total = 0 ;
	size_t done ;
	int error ;

	bufferlen = ARRAY_LEN (ubuf.fbuf) ;

	while (len > 0)
	{	if (len < bufferlen)
			bufferlen = (int) len ;

		error = mpg123_read (pmp3d->pmh, ubuf.ucbuf, bufferlen * sizeof (float), &done) ;

		if (error != MPG123_OK)
		{	/* TODO: handle decoding error */
			break ;
			}

		done /= sizeof (float) ;

		f2s_array (ubuf.fbuf, done, ptr + total) ;

		total += done ;
		len -= done ;
		}

	return total ;
}

static inline void
f2i_array (const float *src, int count, int *dest)
{	while (--count >= 0)
	{	dest [count] = lrintf (src [count] * 0x7FFFFFFF) ;
		} ;
} /* f2i_array */

static sf_count_t
mpeg_dec_read_i (SF_PRIVATE *psf, int *ptr, sf_count_t len)
{	BUF_UNION ubuf ;
	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;
	int bufferlen ;
	sf_count_t total = 0 ;
	size_t done ;
	int error ;

	bufferlen = ARRAY_LEN (ubuf.fbuf) ;

	while (len > 0)
	{	if (len < bufferlen)
			bufferlen = (int) len ;

		error = mpg123_read (pmp3d->pmh, ubuf.ucbuf, bufferlen * sizeof (float), &done) ;

		if (error != MPG123_OK)
			break ;

		done /= sizeof (float) ;

		f2i_array (ubuf.fbuf, done, ptr + total) ;

		total += done ;
		len -= done ;
		}

	return total ;
}

static sf_count_t
mpeg_dec_read_f (SF_PRIVATE *psf, float *ptr, sf_count_t len)
{	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;
	size_t done ;
	int error ;
	int count ;

	error = mpg123_read (pmp3d->pmh, (unsigned char *) ptr, len * sizeof (float), &done) ;

	if (error != MPG123_OK)
		return 0 ;

	done /= sizeof (float) ;

	if (psf->norm_float == SF_FALSE)
	{	count = done ;
		while (--count >= 0)
		{	ptr [count] *= (double) 0x8000 ;
			} ;
		} ;

	return done ;
}

static inline void
f2d_array (const float *src, int count, double *dest, double normfact)
{	while (--count >= 0)
	{	dest [count] = src [count] * normfact ;
		}
} /* f2d_array */

static sf_count_t
mpeg_dec_read_d (SF_PRIVATE *psf, double *ptr, sf_count_t len)
{	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;
	size_t done ;
	int error ;
	double normfact ;

	normfact = (psf->norm_double == SF_TRUE) ? 1.0 : (double) 0x8000 ;

	error = mpg123_read (pmp3d->pmh, (unsigned char *) ptr, len * sizeof (float), &done) ;
	if (error != MPG123_OK)
		return 0 ;

	done /= sizeof (float) ;

	f2d_array ((float *) ptr, done, ptr, normfact) ;

	return done ;
} /* mpeg_dec_read_d */

static sf_count_t
mpeg_dec_seek (SF_PRIVATE *psf, int mode, sf_count_t count)
{	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;
	off_t ret ;

	if (mode != SFM_READ || psf->file.mode != SFM_READ)
	{	psf->error = SFE_BAD_SEEK ;
		return PSF_SEEK_ERROR ;
		} ;

	ret = mpg123_seek (pmp3d->pmh, count, SEEK_SET) ;

	if (ret < 0)
		return PSF_SEEK_ERROR ;

	return (sf_count_t) ret ;
} /* mpeg_dec_seek */

static int
mpeg_dec_fill_sfinfo (mpg123_handle *mh, SF_INFO *info)
{	int error ;
	int channels ;
	int encoding ;
	long rate ;
	off_t length ;

	error = mpg123_getformat (mh, &rate, &channels, &encoding) ;
	if (error != MPG123_OK)
		return error ;

	info->samplerate = rate ;
	info->channels = channels ;

	length = mpg123_length (mh) ;
	if (length >= 0)
	{	info->frames = length ;
		info->seekable = SF_TRUE ;
		}
	else
	{	info->frames = SF_COUNT_MAX ;
		info->seekable = SF_FALSE ;
		}

	/* Force 32-bit float samples. */
	if (encoding != MPG123_ENC_FLOAT_32)
	{	error = mpg123_format (mh, rate, channels, MPG123_ENC_FLOAT_32) ;
		} ;

	return error ;
} /* mpeg_dec_fill_sfinfo */

static void
mpeg_dec_print_frameinfo (SF_PRIVATE *psf, const struct mpg123_frameinfo *fi)
{	psf_log_printf (psf, "  version: %s\n",
		fi->version == MPG123_1_0 ? "MPEG 1.0" :
		fi->version == MPG123_2_0 ? "MPEG 2.0" :
		fi->version == MPG123_2_5 ? "MPEG 2.5" : "?") ;
	psf_log_printf (psf, "  layer: %d\n", fi->layer) ;
	psf_log_printf (psf, "  rate: %d\n", fi->rate) ;
	psf_log_printf (psf, "  mode: %s\n",
		fi->mode == MPG123_M_STEREO ? "stereo" :
		fi->mode == MPG123_M_JOINT ? "joint stereo" :
		fi->mode == MPG123_M_DUAL ? "dual channel" :
		fi->mode == MPG123_M_MONO ? "mono" : "?") ;
	psf_log_printf (psf, "  mode ext: %d\n", fi->mode_ext) ;
	psf_log_printf (psf, "  framesize: %d\n", fi->framesize) ;
	psf_log_printf (psf, "  crc: %c\n", fi->flags & MPG123_CRC ? '1' : '0') ;
	psf_log_printf (psf, "  copyright flag: %c\n", fi->flags & MPG123_COPYRIGHT ? '1' : '0') ;
	psf_log_printf (psf, "  private flag: %c\n", fi->flags & MPG123_PRIVATE ? '1' : '0') ;
	psf_log_printf (psf, "  original flag: %c\n", fi->flags & MPG123_ORIGINAL ? '1' : '0') ;
	psf_log_printf (psf, "  emphasis: %d\n", fi->emphasis) ;
	psf_log_printf (psf, "  bitrate mode: ") ;
	switch (fi->vbr)
	{	case MPG123_CBR :
			psf_log_printf (psf, "constant\n") ;
			break ;
		case MPG123_VBR :
			psf_log_printf (psf, "variable\n") ;
			break ;

		case MPG123_ABR :
			psf_log_printf (psf, "average\n") ;
			psf_log_printf (psf, "  ABR target: %d\n", fi->abr_rate) ;
			break ;
		} ;
	psf_log_printf (psf, "  bitrate: %d kbps\n", fi->bitrate) ;
} /* mpeg_dec_print_frameinfo */

/*
 * Like strlcpy, except the size argument is the maximum size of the input,
 * always null terminates the output string. Thus, up to size + 1 bytes may be
 * written.
 *
 * Returns the length of the copied string.
 */
static int
strcpy_inbounded (char *dest, size_t size, const char *src)
{	char *c = memccpy (dest, src, '\0', size) ;
	if (!c)
		c = dest + size ;
	*c = '\0' ;
	return c - dest ;
}

static void
mpeg_decoder_read_strings (SF_PRIVATE *psf)
{	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;
	mpg123_id3v1 *v1_tags ;
	mpg123_id3v2 *v2_tags ;
	const char *genre ;
	char buf [31] ;

	if (mpg123_id3 (pmp3d->pmh, &v1_tags, &v2_tags) != MPG123_OK)
		return ;

	if (v1_tags != NULL)
	{	psf_log_printf (psf, "ID3v1 Tags\n") ;

		if (strcpy_inbounded (buf, ARRAY_LEN (v1_tags->title), v1_tags->title))
		{	psf_log_printf (psf, "  Title       : %s\n", buf) ;
			psf_store_string (psf, SF_STR_TITLE, buf) ;
			} ;

		if (strcpy_inbounded (buf, ARRAY_LEN (v1_tags->artist), v1_tags->artist))
		{	psf_log_printf (psf, "  Artist      : %s\n", buf) ;
			psf_store_string (psf, SF_STR_ARTIST, buf) ;
			} ;

		if (strcpy_inbounded (buf, ARRAY_LEN (v1_tags->album), v1_tags->album))
		{	psf_log_printf (psf, "  Album       : %s\n", buf) ;
			psf_store_string (psf, SF_STR_ALBUM, buf) ;
			} ;

		if (strcpy_inbounded (buf, ARRAY_LEN (v1_tags->year), v1_tags->year))
		{	psf_log_printf (psf, "  Year        : %s\n", buf) ;
			psf_store_string (psf, SF_STR_DATE, buf) ;
			} ;

		if (strcpy_inbounded (buf, ARRAY_LEN (v1_tags->comment), v1_tags->comment))
		{	psf_log_printf (psf, "  Comment     : %s\n", buf) ;
			psf_store_string (psf, SF_STR_COMMENT, buf) ;
			} ;

		/* ID3v1.1 Tracknumber */
		if (v1_tags->comment [28] == '\0' && v1_tags->comment [29] != '\0')
		{	snprintf (buf, ARRAY_LEN (buf), "%hhu", (unsigned char) v1_tags->comment [29]) ;
			psf_log_printf (psf, "  Tracknumber : %s\n", buf) ;
			psf_store_string (psf, SF_STR_TRACKNUMBER, buf) ;
			} ;

		if ((genre = id3_lookup_v1_genre (v1_tags->genre)) != NULL)
		{	psf_log_printf (psf, "  Genre       : %s\n", genre) ;
			psf_store_string (psf, SF_STR_GENRE, genre) ;
			} ;
		} ;
}

static int
mpeg_dec_byterate (SF_PRIVATE *psf)
{	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;
	struct mpg123_frameinfo fi ;

	if (mpg123_info (pmp3d->pmh, &fi) == MPG123_OK)
		return (fi.bitrate + 7) / 8 ;

	return -1 ;

} /* mpeg_dec_byterate */

int
mpeg_decoder_init (SF_PRIVATE *psf)
{	MPEG_DEC_PRIVATE *pmp3d ;
	struct mpg123_frameinfo fi ;
	int error ;

	if (! (psf->file.mode & SFM_READ))
		return SFE_INTERNAL ;

	/*
	** *** FIXME - Threading issues ***
	**
	** mpg123_init() is a global call that should only be called once, and
	** should be paried with mpg123_exit() when done. libsndfile does not
	** provide for these requirements.
	**
	** Currently this is a moot issue as mpg123_init() non-conditionally writes
	** static areas with calculated data, and mpg123_exit() is a NOP, but this
	** could change in a future version of it!
	**
	** From mpg123.h:
	** > This should be called once in a non-parallel context. It is not explicitly
	** > thread-safe, but repeated/concurrent calls still _should_ be safe as static
	** > tables are filled with the same values anyway.
	**
	** Note that calling mpg123_init() after it has already completed is a NOP.
	*/
	if (mpg123_init () != MPG123_OK)
		return SFE_INTERNAL ;

	psf->codec_data = pmp3d = calloc (1, sizeof (MPEG_DEC_PRIVATE)) ;
	if (!psf->codec_data)
		return SFE_MALLOC_FAILED ;

	pmp3d->pmh = mpg123_new (NULL, &error) ;
	if (!pmp3d->pmh)
	{ psf_log_printf (psf, "Could not obtain a mpg123 handle: %s\n", mpg123_plain_strerror (error)) ;
		return SFE_INTERNAL ;
		} ;

	psf->codec_close = mpeg_dec_close ;

	mpg123_replace_reader_handle (pmp3d->pmh,
		mpeg_dec_io_read, mpeg_dec_io_lseek, NULL) ;

	mpg123_param (pmp3d->pmh, MPG123_REMOVE_FLAGS, MPG123_AUTO_RESAMPLE, 1.0) ;
	mpg123_param (pmp3d->pmh, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT | MPG123_NO_FRANKENSTEIN, 1.0) ;
	//mpg123_param (pmp3d->pmh, MPG123_VERBOSE, 12, 1.0) ;

	psf->dataoffset = 0 ;
	if (psf_is_pipe (psf))
	{	mpg123_param (pmp3d->pmh, MPG123_ADD_FLAGS, MPG123_NO_PEEK_END, 1.0) ;
		}
	else
	{	if (psf->fileoffset > 0)
		{	/* TODO HACK: 'recover' the ID3v2 header. */
			//psf->dataoffset = psf->fileoffset ;
			psf->fileoffset = 0 ;
			psf_fseek (psf, 0, SEEK_SET) ;
			} ;
		} ;

	error = mpg123_open_handle (pmp3d->pmh, psf) ;
	if (error != MPG123_OK)
	{	psf_log_printf (psf, "mpg123 could not open the file: %s\n", mpg123_plain_strerror (error)) ;
		return SFE_BAD_FILE ;
		} ;

	if (mpeg_dec_fill_sfinfo (pmp3d->pmh, &psf->sf) != MPG123_OK)
	{	psf_log_printf (psf, "Cannot get MPEG decoder configuration: %s\n", mpg123_plain_strerror (error)) ;
		return SFE_INTERNAL ;
		} ;

	error = mpg123_info (pmp3d->pmh, &fi) ;
	if (error != MPG123_OK)
	{	psf_log_printf (psf, "Cannot get MPEG frame info: %s\n", mpg123_plain_strerror (error)) ;
		return SFE_INTERNAL ;
		}

	switch (fi.layer)
	{	case 1 : psf->sf.format |= SF_FORMAT_MPEG_LAYER_I ; break ;
		case 2 : psf->sf.format |= SF_FORMAT_MPEG_LAYER_II ; break ;
		case 3 : psf->sf.format |= SF_FORMAT_MPEG_LAYER_III ; break ;
		default : /* Nothing: A lack of subformat will cause an error later. */ break ;
		} ;
	mpeg_dec_print_frameinfo (psf, &fi) ;

	psf->read_short = mpeg_dec_read_s ;
	psf->read_int	= mpeg_dec_read_i ;
	psf->read_float	= mpeg_dec_read_f ;
	psf->read_double = mpeg_dec_read_d ;
	psf->seek		= mpeg_dec_seek ;
	psf->byterate	= mpeg_dec_byterate ;

	mpeg_decoder_read_strings (psf) ;

	if (psf->filelength != SF_COUNT_MAX)
		psf->datalength = psf->filelength - psf->dataoffset ;
	else
		psf->datalength = SF_COUNT_MAX ;

	return 0 ;
}

int
mpeg_decoder_get_bitrate_mode (SF_PRIVATE *psf)
{	MPEG_DEC_PRIVATE *pmp3d = (MPEG_DEC_PRIVATE *) psf->codec_data ;
	struct mpg123_frameinfo fi ;

	if (mpg123_info (pmp3d->pmh, &fi) == MPG123_OK)
	{
		switch (fi.vbr)
		{	case MPG123_CBR : return SF_BITRATE_MODE_CONSTANT ;
			case MPG123_ABR : return SF_BITRATE_MODE_AVERAGE ;
			case MPG123_VBR : return SF_BITRATE_MODE_VARIABLE ;
			default : break ;
			} ;
		} ;

	psf_log_printf (psf, "Cannot determine MPEG bitrate mode.\n") ;
	return -1 ;
} /* mpeg_decoder_get_bitrate_mode */

#else /* ENABLE_EXPERIMENTAL_CODE && HAVE_MPEG */

int mpeg_decoder_init (SF_PRIVATE *psf)
{	psf_log_printf (psf, "This version of libsndfile was compiled without MP3 decode support.\n") ;
	return SFE_UNIMPLEMENTED ;
} /* mpeg_decoder_init */

#endif
