/*****************************************************************************
 * a52.c : raw A/52 stream input module for vlc
 *****************************************************************************
 * Copyright (C) 2001 VideoLAN
 * $Id: a52.c,v 1.2 2004/02/13 22:10:00 gbazin Exp $
 *
 * Authors: Gildas Bazin <gbazin@netcourrier.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <vlc/vlc.h>
#include <vlc/input.h>
#include <vlc_codec.h>

#ifdef HAVE_UNISTD_H
#   include <unistd.h>
#endif

#define A52_PACKET_SIZE 16384
#define A52_MAX_HEADER_SIZE 10

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  Open  ( vlc_object_t * );
static void Close ( vlc_object_t * );
static int  Demux ( input_thread_t * );

static int Control( input_thread_t *, int, va_list );

struct demux_sys_t
{
    vlc_bool_t  b_start;
    es_out_id_t *p_es;

    /* Packetizer */
    decoder_t *p_packetizer;

    int i_mux_rate;
    vlc_bool_t b_big_endian;
};

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin();
    set_description( _("Raw A/52 demuxer") );
    set_capability( "demux", 155 );
    set_callbacks( Open, Close );
    add_shortcut( "a52" );
vlc_module_end();

/*****************************************************************************
 * CheckSync: Check if buffer starts with an A52 sync code
 *****************************************************************************/
int CheckSync( uint8_t *p_peek, vlc_bool_t *p_big_endian )
{
    /* Little endian version of the bitstream */
    if( p_peek[0] == 0x77 && p_peek[1] == 0x0b &&
        p_peek[4] < 0x60 /* bsid < 12 */ )
    {
        return VLC_SUCCESS;
    }
    /* Big endian version of the bitstream */
    else if( p_peek[0] == 0x0b && p_peek[1] == 0x77 &&
             p_peek[5] < 0x60 /* bsid < 12 */ )
    {
        return VLC_SUCCESS;
    }

    return VLC_EGENERIC;
}

/*****************************************************************************
 * Open: initializes ES structures
 *****************************************************************************/
static int Open( vlc_object_t * p_this )
{
    input_thread_t *p_input = (input_thread_t *)p_this;
    demux_sys_t    *p_sys;
    byte_t *       p_peek;
    int            i_peek = 0;
    vlc_bool_t     b_big_endian;

    p_input->pf_demux = Demux;
    p_input->pf_demux_control = Control;
    p_input->pf_rewind = NULL;

    /* Check if we are dealing with a WAV file */
    if( input_Peek( p_input, &p_peek, 12 ) == 12 &&
        !strncmp( p_peek, "RIFF", 4 ) && !strncmp( &p_peek[8], "WAVE", 4 ) )
    {
        int i_size;

        /* Skip the wave header */
        i_peek = 12 + 8;
        while( input_Peek( p_input, &p_peek, i_peek ) == i_peek &&
               strncmp( p_peek + i_peek - 8, "data", 4 ) )
        {
            i_peek += GetDWLE( p_peek + i_peek - 4 ) + 8;
        }

        /* TODO: should check wave format and sample_rate */

        /* Some A52 wav files don't begin with a sync code so we do a more
         * extensive search */
        i_size = input_Peek( p_input, &p_peek, i_peek + A52_PACKET_SIZE * 2);
        i_size -= A52_MAX_HEADER_SIZE;

        while( i_peek < i_size )
        {
            if( CheckSync( p_peek + i_peek, &b_big_endian ) != VLC_SUCCESS )
                /* The data is stored in 16 bits words */
                i_peek += 2;
            else
                break;
        }
    }

    /* Have a peep at the show. */
    if( input_Peek( p_input, &p_peek, i_peek + A52_MAX_HEADER_SIZE * 2 ) <
        i_peek + A52_MAX_HEADER_SIZE * 2 )
    {
        /* Stream too short */
        msg_Warn( p_input, "cannot peek()" );
        return VLC_EGENERIC;
    }

    if( CheckSync( p_peek + i_peek, &b_big_endian ) != VLC_SUCCESS )
    {
        if( p_input->psz_demux && !strncmp( p_input->psz_demux, "a52", 3 ) )
        {
            /* User forced */
            msg_Err( p_input, "this doesn't look like a A52 audio stream, "
                     "continuing anyway" );
        }
        else
        {
            return VLC_EGENERIC;
        }
    }

    p_input->p_demux_data = p_sys = malloc( sizeof( demux_sys_t ) );
    p_sys->b_start = VLC_TRUE;
    p_sys->i_mux_rate = 0;
    p_sys->b_big_endian = b_big_endian;

    /*
     * Load the A52 packetizer
     */
    p_sys->p_packetizer = vlc_object_create( p_input, VLC_OBJECT_DECODER );
    p_sys->p_packetizer->pf_decode_audio = 0;
    p_sys->p_packetizer->pf_decode_video = 0;
    p_sys->p_packetizer->pf_decode_sub = 0;
    p_sys->p_packetizer->pf_packetize = 0;

    /* Initialization of decoder structure */
    es_format_Init( &p_sys->p_packetizer->fmt_in, AUDIO_ES,
                    VLC_FOURCC( 'a', '5', '2', ' ' ) );

    p_sys->p_packetizer->p_module =
        module_Need( p_sys->p_packetizer, "packetizer", NULL );
    if( !p_sys->p_packetizer->p_module )
    {
        msg_Err( p_input, "cannot find A52 packetizer" );
        return VLC_EGENERIC;
    }

    /* Create one program */
    vlc_mutex_lock( &p_input->stream.stream_lock );
    if( input_InitStream( p_input, 0 ) == -1 )
    {
        vlc_mutex_unlock( &p_input->stream.stream_lock );
        msg_Err( p_input, "cannot init stream" );
        return VLC_EGENERIC;
    }
    p_input->stream.i_mux_rate = 0;
    vlc_mutex_unlock( &p_input->stream.stream_lock );

    p_sys->p_es =
        es_out_Add( p_input->p_es_out, &p_sys->p_packetizer->fmt_in );

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: frees unused data
 *****************************************************************************/
static void Close( vlc_object_t * p_this )
{
    input_thread_t *p_input = (input_thread_t*)p_this;
    demux_sys_t    *p_sys = p_input->p_demux_data;

    /* Unneed module */
    module_Unneed( p_sys->p_packetizer, p_sys->p_packetizer->p_module );

    /* Delete the decoder */
    vlc_object_destroy( p_sys->p_packetizer );

    free( p_sys );
}

/*****************************************************************************
 * Demux: reads and demuxes data packets
 *****************************************************************************
 * Returns -1 in case of error, 0 in case of EOF, 1 otherwise
 *****************************************************************************/
static int Demux( input_thread_t * p_input )
{
    demux_sys_t  *p_sys = p_input->p_demux_data;
    block_t *p_block_in, *p_block_out;

     /* Align stream */
    int64_t i_pos = stream_Tell( p_input->s );
    if( i_pos % 2 ) stream_Read( p_input->s, NULL, 1 );

    if( !( p_block_in = stream_Block( p_input->s, A52_PACKET_SIZE ) ) )
    {
        return 0;
    }

    if( !p_sys->b_big_endian && p_block_in->i_buffer )
    {
        /* Convert to big endian */

#ifdef HAVE_SWAB
        swab(p_block_in->p_buffer, p_block_in->p_buffer, p_block_in->i_buffer);

#else
        int i;
        byte_t *p_tmp, tmp;
        p_tmp = p_block_in->p_buffer;
        for( i = p_block_in->i_buffer / 2 ; i-- ; )
        {
            tmp = p_tmp[0];
            p_tmp[0] = p_tmp[1];
            p_tmp[1] = tmp;
            p_tmp += 2;
        }
#endif
    }

    if( p_sys->b_start )
        p_block_in->i_pts = p_block_in->i_dts = 1;
    else
        p_block_in->i_pts = p_block_in->i_dts = 0;

    while( (p_block_out = p_sys->p_packetizer->pf_packetize(
                p_sys->p_packetizer, &p_block_in )) )
    {
        p_sys->b_start = VLC_FALSE;

        while( p_block_out )
        {
            block_t *p_next = p_block_out->p_next;

            /* We assume a constant bitrate */
            if( p_block_out->i_length )
            p_sys->i_mux_rate =
                p_block_out->i_buffer * I64C(1000000) / p_block_out->i_length;
            p_input->stream.i_mux_rate = p_sys->i_mux_rate / 50;

            input_ClockManageRef( p_input,
                                  p_input->stream.p_selected_program,
                                  p_block_out->i_pts * 9 / 100 );

            p_block_in->b_discontinuity = 0;
            p_block_out->i_dts = p_block_out->i_pts =
                input_ClockGetTS( p_input, p_input->stream.p_selected_program,
                                  p_block_out->i_pts * 9 / 100 );

            es_out_Send( p_input->p_es_out, p_sys->p_es, p_block_out );

            p_block_out = p_next;
        }
    }

    return 1;
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int Control( input_thread_t *p_input, int i_query, va_list args )
{
    demux_sys_t *p_sys  = (demux_sys_t *)p_input->p_demux_data;
    int64_t *pi64;

    switch( i_query )
    {
        case DEMUX_GET_TIME:
            pi64 = (int64_t*)va_arg( args, int64_t * );
            if( p_sys->i_mux_rate > 0 )
            {
                *pi64 = I64C(1000000) * stream_Tell( p_input->s ) /
                        p_sys->i_mux_rate;
                return VLC_SUCCESS;
            }
            *pi64 = 0;
            return VLC_EGENERIC;

        case DEMUX_GET_LENGTH:
            pi64 = (int64_t*)va_arg( args, int64_t * );
            if( p_sys->i_mux_rate > 0 )
            {
                *pi64 = I64C(1000000) * stream_Size( p_input->s ) /
                        p_sys->i_mux_rate;
                return VLC_SUCCESS;
            }
            *pi64 = 0;
            return VLC_EGENERIC;

        default:
            return demux_vaControlDefault( p_input, i_query, args );
    }
}
