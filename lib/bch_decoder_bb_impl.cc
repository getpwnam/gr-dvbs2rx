/* -*- c++ -*- */
/*
 * Copyright 2018 Ahmet Inan, Ron Economos.
 *
 * This file is part of gr-dvbs2rx.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bch_decoder_bb_impl.h"
#include "debug_level.h"
#include "fec_params.h"
#include <gnuradio/io_signature.h>
#include <gnuradio/logger.h>
#include <functional>
// BCH Code
#define BCH_CODE_N8 0
#define BCH_CODE_N10 1
#define BCH_CODE_N12 2
#define BCH_CODE_S12 3
#define BCH_CODE_M12 4

namespace gr {
namespace dvbs2rx {

bch_decoder_bb::sptr bch_decoder_bb::make(dvb_standard_t standard,
                                          dvb_framesize_t framesize,
                                          dvb_code_rate_t rate,
                                          dvb_outputmode_t outputmode,
                                          int debug_level)
{
    return gnuradio::get_initial_sptr(
        new bch_decoder_bb_impl(standard, framesize, rate, outputmode, debug_level));
}

/*
 * The private constructor
 */
bch_decoder_bb_impl::bch_decoder_bb_impl(dvb_standard_t standard,
                                         dvb_framesize_t framesize,
                                         dvb_code_rate_t rate,
                                         dvb_outputmode_t outputmode,
                                         int debug_level)
    : gr::block("bch_decoder_bb",
                gr::io_signature::make(1, 1, sizeof(unsigned char)),
                gr::io_signature::make(1, 1, sizeof(unsigned char))),
      d_debug_level(debug_level),
      d_frame_cnt(0),
      d_frame_error_cnt(0)
{
    fec_info_t fec_info;
    get_fec_info(standard, framesize, rate, fec_info);
    uint32_t prim_poly;
    if (framesize == FECFRAME_NORMAL)
        prim_poly = 0b10000000000101101; // x^16 + x^5 + x^3 + x^2 + 1
    else if (framesize == FECFRAME_SHORT)
        prim_poly = 0b100000000101011; // x^14 + x^5 + x^3 + x + 1
    else
        prim_poly = 0b1000000000101101; // x^15 + x^5 + x^3 + x^2 + 1
    d_gf = std::make_unique<galois_field<uint32_t>>(prim_poly);
    d_codec = std::make_unique<bch_codec<uint32_t, bitset256_t>>(
        d_gf.get(), fec_info.bch.t, fec_info.bch.n);
    d_k_bytes = fec_info.bch.k / 8;
    d_n_bytes = fec_info.bch.n / 8;
    set_output_multiple(d_k_bytes);
    set_relative_rate((double)fec_info.bch.k / fec_info.bch.n);
    set_tag_propagation_policy(TPP_CUSTOM);
}

/*
 * Our virtual destructor.
 */
bch_decoder_bb_impl::~bch_decoder_bb_impl() {}

void bch_decoder_bb_impl::forecast(int noutput_items,
                                   gr_vector_int& ninput_items_required)
{
    ninput_items_required[0] = (noutput_items / d_k_bytes) * d_n_bytes;
}

int bch_decoder_bb_impl::general_work(int noutput_items,
                                      gr_vector_int& ninput_items,
                                      gr_vector_const_void_star& input_items,
                                      gr_vector_void_star& output_items)
{
    const unsigned char* in = (const unsigned char*)input_items[0];
    unsigned char* out = (unsigned char*)output_items[0];

    int consumed = 0;
    int n_codewords = noutput_items / d_k_bytes;
    const auto status_key = pmt::intern("bch_frame_status");
    for (int i = 0; i < n_codewords; i++) {
        const int corrections = d_codec->decode(in, out);
        if (corrections > 0) {
            GR_LOG_DEBUG_LEVEL(1,
                               "frame = {:d}, BCH decoder corrections = {:d}",
                               d_frame_cnt,
                               corrections);
        } else if (corrections == -1) {
            d_frame_error_cnt++;
            GR_LOG_DEBUG_LEVEL(
                1,
                "frame = {:d}, BCH decoder too many bit errors (FER = {:g})",
                d_frame_cnt,
                ((double)d_frame_error_cnt / (d_frame_cnt + 1)));
        }
        auto status = pmt::make_dict();
        status = pmt::dict_add(
            status, pmt::intern("frame_index"), pmt::from_uint64(d_frame_cnt));
        status =
            pmt::dict_add(status, pmt::intern("valid"), pmt::from_bool(corrections >= 0));
        status = pmt::dict_add(
            status, pmt::intern("corrections"), pmt::from_long(corrections));
        add_item_tag(0, nitems_written(0) + uint64_t(i) * d_k_bytes, status_key, status);
        d_frame_cnt++;
        in += d_n_bytes;
        out += d_k_bytes;
        consumed += d_n_bytes;
    }

    // Replace upstream status tags, preserving other tags and their rate mapping.
    std::vector<tag_t> tags;
    get_tags_in_range(tags, 0, nitems_read(0), nitems_read(0) + consumed);
    for (auto tag : tags) {
        if (pmt::eq(tag.key, status_key))
            continue;
        const auto frame = tag.offset / d_n_bytes;
        const auto within_frame = tag.offset % d_n_bytes;
        tag.offset =
            frame * d_k_bytes + (within_frame * d_k_bytes + d_n_bytes / 2) / d_n_bytes;
        add_item_tag(0, tag);
    }

    consume_each(consumed);
    return noutput_items;
}

} /* namespace dvbs2rx */
} /* namespace gr */
