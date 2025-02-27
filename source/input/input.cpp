/*****************************************************************************
 * Copyright (C) 2013-2020 MulticoreWare, Inc
 *
 * Authors: Steve Borho <steve@borho.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at license @ x265.com.
 *****************************************************************************/

#include "input.h"
#include "yuv.h"
#include "y4m.h"
#ifdef ENABLE_AVISYNTH
    #include "avs.h"
#endif
#ifdef ENABLE_LAVF
    #include "lavf.h"
#endif
#ifdef ENABLE_VPYSYNTH
    #include "vpy.h"
#endif

using namespace X265_NS;

InputFile* InputFile::open(InputFileInfo& info, bool bForceY4m, bool alpha, int format)
{
    const char * s = strrchr(info.filename, '.');

    if (bForceY4m || (s && !strcmp(s, ".y4m")))
        return new Y4MInput(info, alpha, format);

#ifdef ENABLE_AVISYNTH
    if (s && !strcmp(s, ".avs"))
        return new AVSInput(info);
#endif

#ifdef ENABLE_VPYSYNTH
    if (s && !strcmp(s, ".vpy"))
        return new VPYInput(info);
#endif

#ifdef ENABLE_LAVF
    if (s &&
        ( !strcmp(s, ".mp4")
        ||!strcmp(s, ".mkv")
        ||!strcmp(s, ".mpg")
        ||!strcmp(s, ".m1v")
        ||!strcmp(s, ".m2v")
        ||!strcmp(s, ".mpeg")
        ||!strcmp(s, ".m4v")
        ||!strcmp(s, ".m2ts")
        ||!strcmp(s, ".ts")
        ||!strcmp(s, ".avs")
        ||!strcmp(s, ".avi")
        ||!strcmp(s, ".ogv")
        ||!strcmp(s, ".wmv")
        ))
        return new LavfInput(info);
#endif
    return new YUVInput(info, alpha, format);
}
