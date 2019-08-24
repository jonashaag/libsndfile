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

#if (ENABLE_EXPERIMENTAL_CODE)

#include "mpeg.h"

static int	mpeg_write_header (SF_PRIVATE *psf, int calc_length) ;
static int	mpeg_command (SF_PRIVATE *psf, int command, void *data, int datasize) ;

/*------------------------------------------------------------------------------
 * Public fuctions
 */

int
mpeg_open (SF_PRIVATE *psf)
{	int error ;

	if (psf->file.mode == SFM_RDWR)
		return SFE_BAD_MODE_RW ;

	if (psf->file.mode == SFM_WRITE)
	{	if ((error = mpeg_encoder_init (psf, SF_TRUE)))
			return error ;

		/* ID3 support */
		psf->strings.flags = SF_STR_ALLOW_START ;
		psf->write_header = mpeg_write_header ;
		psf->datalength = 0 ;
		psf->dataoffset = 0 ;

//		/* Enable VBR by default */
//		lame_set_VBR (pmpeg->lamef, 1) ;
		} ;

	if (psf->file.mode == SFM_READ)
	{	if ((error = mpeg_decoder_init (psf)))
			return error ;
		} ;

	psf->command = mpeg_command ;

	return 0 ;
} /* mpeg_open */

static int
mpeg_write_header (SF_PRIVATE *psf, int UNUSED (calc_length))
{
	if (psf->have_written)
		return 0 ;

	return mpeg_encoder_write_id3tag (psf) ;
} ;

static int
mpeg_command (SF_PRIVATE *psf, int command, void *data, int datasize)
{
	switch (command)
	{	case SFC_SET_COMPRESSION_LEVEL :
			if (data == NULL || datasize != sizeof (double))
				return SF_FALSE ;
			if (psf->file.mode != SFM_WRITE || psf->have_written)
				return SF_FALSE ;

			return mpeg_encoder_set_quality (psf, *(double *) data) ;

		default :
			return SF_FALSE ;
		} ;

	return SF_FALSE ;
} /* mpeg_command */

#else /* ENABLE_EXPERIMENTAL_CODE */

int
mpeg_open (PSF_PRIVATE *psf)
{
	psf_log_printf (psf, "This version of libsndfile was compiled without MP3 support.\n") ;
	return SFE_UNIMPLEMENTED ;
} /* mpeg_open */

#endif
