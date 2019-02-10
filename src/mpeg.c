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
} MP3_PRIVATE ;

static int	mpeg_close (SF_PRIVATE *psf) ;

int
mpeg_open (SF_PRIVATE *psf)
{	MP3_PRIVATE* pmp3 = NULL ;

	if (psf->file.mode == SFM_RDWR)
		return SFE_BAD_MODE_RW ;

	if (psf->file.mode == SFM_READ)
	{	/* TODO: read/decode support */
		return SFE_UNIMPLEMENTED ;
		} ;

	psf->codec_data = pmp3 = calloc (1, sizeof (MP3_PRIVATE)) ;
	if (!pmp3)
		return SFE_MALLOC_FAILED ;

	if (psf->file.mode == SFM_WRITE)
	{	pmp3->lamef = lame_init () ;
		if (!pmp3->lamef)
			return SFE_MALLOC_FAILED ;
		} ;

	psf->codec_close = mpeg_close ;

	return SFE_UNIMPLEMENTED ;
} /* mpeg_open */


static int
mpeg_close (SF_PRIVATE *psf)
{	MP3_PRIVATE* pmp3 = (MP3_PRIVATE *) psf->codec_data ;

	if (pmp3->lamef)
	{	lame_close (pmp3->lamef) ;
		pmp3->lamef = NULL ;
		}

	free (pmp3) ;

	return 0 ;
} /* mpeg_close */

#else /* ENABLE_EXPERIMENTAL_CODE && HAVE_LAME */

int mpeg_open (PSF_PRIVATE *psf)
{
	psf_log_printf (psf, "This version of libsndfile was compiled without MP3 support.\n") ;
	return SFE_UNIMPLEMENTED ;
} /* mpeg_open */

#endif
