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

#include	"sndfile.h"
#include	"common.h"

#if (ENABLE_EXPERIMENTAL_CODE && HAVE_LAME)

#include <lame/lame.h>

typedef struct
{	lame_t lamef ;
	unsigned char *block ;
	size_t len ;
	int max_samp ;
} MPEG_PRIVATE ;

typedef int (* mpeg_write_func) (MPEG_PRIVATE *, void const **, const int) ;

static int	mpeg_close (SF_PRIVATE *psf) ;
static int	mpeg_init (SF_PRIVATE *psf) ;
static int	mpeg_encoder_construct (SF_PRIVATE *psf) ;
static sf_count_t	mpeg_write (SF_PRIVATE *psf, const void *ptr, sf_count_t len, mpeg_write_func func) ;

static int	mpeg_write_short_mono (MPEG_PRIVATE *, void const **, const int) ;
static int	mpeg_write_short_stereo (MPEG_PRIVATE *, void const **, const int) ;
static int	mpeg_write_int_mono (MPEG_PRIVATE *, void const **, const int) ;
static int	mpeg_write_int_stereo (MPEG_PRIVATE *, void const **, const int) ;
static int	mpeg_write_float_mono (MPEG_PRIVATE *, void const **, const int) ;
static int	mpeg_write_float_stereo (MPEG_PRIVATE *, void const **, const int) ;
static int	mpeg_write_double_mono (MPEG_PRIVATE *, void const **, const int) ;
static int	mpeg_write_double_stereo (MPEG_PRIVATE *, void const **, const int) ;

static sf_count_t	mpeg_write_s (SF_PRIVATE *psf, const short *ptr, sf_count_t len) ;
static sf_count_t	mpeg_write_i (SF_PRIVATE *psf, const int *ptr, sf_count_t len) ;
static sf_count_t	mpeg_write_f (SF_PRIVATE *psf, const float *ptr, sf_count_t len) ;
static sf_count_t	mpeg_write_d (SF_PRIVATE *psf, const double *ptr, sf_count_t len) ;


/*------------------------------------------------------------------------------
 * Public fuctions
 */

int
mpeg_open (SF_PRIVATE *psf)
{	MPEG_PRIVATE * pmpeg ;
	int error ;

	if (psf->file.mode == SFM_RDWR)
		return SFE_BAD_MODE_RW ;

	if (psf->file.mode == SFM_READ)
	{	/* TODO: read/decode support */
		return SFE_UNIMPLEMENTED ;
		} ;

	if (psf->file.mode == SFM_WRITE)
	{	if ((error = mpeg_init (psf)))
			return error ;

		/* TODO ID3 support */

		pmpeg = (MPEG_PRIVATE *) psf->codec_data ;
		lame_set_VBR (pmpeg->lamef, 1) ;
		} ;

	return 0 ;
} /* mpeg_open */


/*------------------------------------------------------------------------------
 * Private static functions
 */

static int
mpeg_close (SF_PRIVATE *psf)
{	MPEG_PRIVATE* pmpeg = (MPEG_PRIVATE *) psf->codec_data ;
	int ret, len ;
	sf_count_t pos ;
	unsigned char *buffer ;

	if (psf->file.mode == SFM_WRITE)
	{	/* Magic number 7200 comes from a comment in lame.h */
		len = 7200 ;
		if (! (buffer = malloc (len)))
			return SFE_MALLOC_FAILED ;
		ret = lame_encode_flush (pmpeg->lamef, buffer, len) ;
		if (ret > 0)
			psf_fwrite (buffer, 1, ret, psf) ;

		/*
		** If possible, seek back and write the LAME/XING/Info header. This
		** contains information about the whole file and a seek table, and can
		** only be written after encodeing.
		** If enabled, Lame wrote an empty header at the begining of the data
		** that we now fill in.
		*/
		ret = lame_get_lametag_frame (pmpeg->lamef, 0, 0) ;
		if (ret > 0)
		{	if (ret > len)
			{	len = ret ;
				free (buffer) ;
				if (! (buffer = malloc (len)))
					return SFE_MALLOC_FAILED ;
				} ;
			psf_log_printf (psf, "  Writing LAME info header at offset %d, %d bytes.\n",
				psf->dataoffset, len) ;
			lame_get_lametag_frame (pmpeg->lamef, buffer, len) ;
			pos = psf_ftell (psf) ;
			if (psf_fseek (psf, psf->dataoffset, SEEK_SET) == psf->dataoffset)
			{	psf_fwrite (buffer, 1, ret, psf) ;
				psf_fseek (psf, pos, SEEK_SET) ;
				} ;
			} ;
		free (buffer) ;
		free (pmpeg->block) ;
		pmpeg->block = NULL ;
		if (pmpeg->lamef)
		{	lame_close (pmpeg->lamef) ;
			pmpeg->lamef = NULL ;
			} ;
		} ;

	return 0 ;
} /* mpeg_close */


static int
mpeg_init (SF_PRIVATE *psf)
{	MPEG_PRIVATE* pmpeg = NULL ;

	if (psf->file.mode == SFM_RDWR)
		return SFE_BAD_MODE_RW ;

	if (psf->file.mode == SFM_READ)
		return SFE_UNIMPLEMENTED ;

	psf->codec_data = pmpeg = calloc (1, sizeof (MPEG_PRIVATE)) ;
	if (!pmpeg)
		return SFE_MALLOC_FAILED ;

	if (psf->file.mode == SFM_WRITE)
	{	if (psf->sf.channels < 1 || psf->sf.channels > 2)
			return SFE_BAD_OPEN_FORMAT ;

		if (! (pmpeg->lamef = lame_init ()))
			return SFE_MALLOC_FAILED ;

		lame_set_in_samplerate (pmpeg->lamef, psf->sf.samplerate) ;
		lame_set_num_channels (pmpeg->lamef, psf->sf.channels) ;
		if (lame_set_out_samplerate (pmpeg->lamef, psf->sf.samplerate) < 0)
			/* TODO */ return SFE_BAD_OPEN_FORMAT ;

		lame_set_quality (pmpeg->lamef, 2) ;
		lame_set_write_id3tag_automatic (pmpeg->lamef, 0) ;

		if (psf->is_pipe)
		{	/* Can't seek, so force disable Xing/Lame header. */
			lame_set_bWriteVbrTag (pmpeg->lamef, 0) ;
			} ;

		psf->write_short	= mpeg_write_s ;
		psf->write_int		= mpeg_write_i ;
		psf->write_float	= mpeg_write_f ;
		psf->write_double	= mpeg_write_d ;

		psf->datalength = 0 ;
		psf->dataoffset = 0 ;
		} ;

	psf->sf.seekable = 0 ;
	psf->codec_close	= mpeg_close ;

	return 0 ;
} /* mpeg_init */


static int
mpeg_encoder_construct (SF_PRIVATE *psf)
{	MPEG_PRIVATE *pmpeg = (MPEG_PRIVATE *) psf->codec_data ;

	if (lame_init_params (pmpeg->lamef) < 0)
	{	psf_log_printf (psf, "Failed to initialize lame encoder!\n") ;
		return SFE_INTERNAL ;
		} ;

	pmpeg->len = lame_get_framesize (pmpeg->lamef) * 4 ;
	if (! (pmpeg->block = malloc (pmpeg->len)))
		return SFE_MALLOC_FAILED ;

	pmpeg->max_samp = lame_get_maximum_number_of_samples (
			pmpeg->lamef, pmpeg->len) * psf->sf.channels ;

	return 0 ;
} /* mpeg_encoder_construct */



static sf_count_t
mpeg_write (SF_PRIVATE *psf, const void *ptr, sf_count_t len, mpeg_write_func func)
{	MPEG_PRIVATE *pmpeg = (MPEG_PRIVATE*) psf->codec_data ;
	sf_count_t total, k ;
	int ret, nsamp ;

	if (!pmpeg->len && (psf->error = mpeg_encoder_construct (psf)))
		return 0 ;

	for (total = 0 ; total < len ; total += nsamp)
	{	nsamp = SF_MIN ((int) (len - total), pmpeg->max_samp) ;
		ret = func (pmpeg, &ptr, nsamp) ;
		if (ret < 0)
		{	psf_log_printf (psf, "lame_encode_buffer returned %d\n", ret) ;
			break ;
			} ;

		if (ret)
		{	if ((k = psf_fwrite (pmpeg->block, 1, ret, psf)) != ret)
			{	psf_log_printf (psf, "*** Warning : short write (%d != %d).\n", k, ret) ;
				} ;
			} ;
	} ;

	return total ;
} /* mpeg_write */

static int
mpeg_write_short_mono (MPEG_PRIVATE *pmpeg, void const ** buffer, const int nsamp)
{	int ret ;
	ret = lame_encode_buffer (
		pmpeg->lamef, (const short *) *buffer, NULL, nsamp, pmpeg->block, pmpeg->len) ;
	if (ret >= 0)
		*((short const **) buffer) += nsamp ;
	return ret ;
}

static int
mpeg_write_short_stereo (MPEG_PRIVATE *pmpeg, void const ** buffer, const int nsamp)
{	int ret ;
	ret = lame_encode_buffer_interleaved (
		pmpeg->lamef, (const short *) *buffer, nsamp / 2, pmpeg->block, pmpeg->len) ;
	if (ret >= 0)
		*((short const **) buffer) += nsamp * 2 ;
	return ret ;
}

static int
mpeg_write_int_mono (MPEG_PRIVATE *pmpeg, void const ** buffer, const int nsamp)
{	int ret ;
	ret = lame_encode_buffer_int (
		pmpeg->lamef, (const int *) *buffer, NULL, nsamp, pmpeg->block, pmpeg->len) ;
	if (ret >= 0)
		*((int const **) buffer) += nsamp ;
	return ret ;
}

static int
mpeg_write_int_stereo (MPEG_PRIVATE *pmpeg, void const ** buffer, const int nsamp)
{	int ret ;
	ret = lame_encode_buffer_interleaved_int (
		pmpeg->lamef, (const int *) *buffer, nsamp / 2, pmpeg->block, pmpeg->len) ;
	if (ret >= 0)
		*((int const **) buffer) += nsamp * 2 ;
	return ret ;
}

static int
mpeg_write_float_mono (MPEG_PRIVATE *pmpeg, void const ** buffer, const int nsamp)
{	int ret ;
	ret = lame_encode_buffer_ieee_float (
		pmpeg->lamef, (const float *) *buffer, NULL, nsamp, pmpeg->block, pmpeg->len) ;
	if (ret >= 0)
		*((float const **) buffer) += nsamp ;
	return ret ;
}

static int
mpeg_write_float_stereo (MPEG_PRIVATE *pmpeg, void const ** buffer, const int nsamp)
{	int ret ;
	ret = lame_encode_buffer_interleaved_ieee_float (
		pmpeg->lamef, (const float *) *buffer, nsamp / 2, pmpeg->block, pmpeg->len) ;
	if (ret >= 0)
		*((float const **) buffer) += nsamp * 2 ;
	return ret ;
}

static int
mpeg_write_double_mono (MPEG_PRIVATE *pmpeg, void const ** buffer, const int nsamp)
{	int ret ;
	ret = lame_encode_buffer_ieee_double (
		pmpeg->lamef, (const double *) *buffer, NULL, nsamp, pmpeg->block, pmpeg->len) ;
	if (ret >= 0)
		*((double const **) buffer) += nsamp ;
	return ret ;
}

static int
mpeg_write_double_stereo (MPEG_PRIVATE *pmpeg, void const ** buffer, const int nsamp)
{	int ret ;
	ret = lame_encode_buffer_interleaved_ieee_double (
		pmpeg->lamef, (const double *) *buffer, nsamp / 2, pmpeg->block, pmpeg->len) ;
	if (ret >= 0)
		*((double const **) buffer) += nsamp * 2 ;
	return ret ;
}

static sf_count_t
mpeg_write_s (SF_PRIVATE *psf, const short *ptr, sf_count_t len)
{
	if (psf->sf.channels == 1)
		return mpeg_write (psf, ptr, len, mpeg_write_short_mono) ;
	else
		return mpeg_write (psf, ptr, len, mpeg_write_short_stereo) ;
}

static sf_count_t
mpeg_write_i (SF_PRIVATE *psf, const int *ptr, sf_count_t len)
{
	if (psf->sf.channels == 1)
		return mpeg_write (psf, ptr, len, mpeg_write_int_mono) ;
	else
		return mpeg_write (psf, ptr, len, mpeg_write_int_stereo) ;
}

static sf_count_t
mpeg_write_f (SF_PRIVATE *psf, const float *ptr, sf_count_t len)
{
	if (psf->sf.channels == 1)
		return mpeg_write (psf, ptr, len, mpeg_write_float_mono) ;
	else
		return mpeg_write (psf, ptr, len, mpeg_write_float_stereo) ;
}

static sf_count_t
mpeg_write_d (SF_PRIVATE *psf, const double *ptr, sf_count_t len)
{
	if (psf->sf.channels == 1)
		return mpeg_write (psf, ptr, len, mpeg_write_double_mono) ;
	else
		return mpeg_write (psf, ptr, len, mpeg_write_double_stereo) ;
}



#else /* ENABLE_EXPERIMENTAL_CODE && HAVE_LAME */

int mpeg_open (PSF_PRIVATE *psf)
{
	psf_log_printf (psf, "This version of libsndfile was compiled without MP3 support.\n") ;
	return SFE_UNIMPLEMENTED ;
} /* mpeg_open */

#endif
