/*****************************************************************************
 * SmoothSegment.cpp:
 *****************************************************************************
 * Copyright (C) 2015 - VideoLAN Authors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "SmoothSegment.hpp"

#include "../adaptative/playlist/BaseRepresentation.h"
#include "../adaptative/playlist/AbstractPlaylist.hpp"
#include "../mp4/IndexReader.hpp"

using namespace smooth::playlist;
using namespace smooth::mp4;

SmoothSegment::SmoothSegment(SegmentInformation *parent) :
    MediaSegmentTemplate( parent )
{

}

void SmoothSegment::onChunkDownload(block_t **pp_block, SegmentChunk *, BaseRepresentation *rep)
{
    if(!rep || ((*pp_block)->i_flags & BLOCK_FLAG_HEADER) == 0 ||
       !rep->getPlaylist()->isLive())
        return;

    IndexReader br(rep->getPlaylist()->getVLCObject());
    br.parseIndex(*pp_block, rep);
}
