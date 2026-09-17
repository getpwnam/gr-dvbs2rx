#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2023 Igor Freire.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

from gnuradio import blocks, dtv, gr, gr_unittest

try:
    from gnuradio.dvbs2rx import (C3_4, C3_5, FECFRAME_NORMAL, FECFRAME_SHORT,
                                  INFO_OFF, MOD_32APSK, MOD_QPSK, OM_MESSAGE,
                                  STANDARD_DVBS2, bch_decoder_bb,
                                  ldpc_decoder_bb, xfecframe_demapper_cb)
except ImportError:
    from python.dvbs2rx import (C3_4, C3_5, FECFRAME_NORMAL, FECFRAME_SHORT,
                                INFO_OFF, MOD_32APSK, MOD_QPSK, OM_MESSAGE,
                                STANDARD_DVBS2, bch_decoder_bb,
                                ldpc_decoder_bb, xfecframe_demapper_cb)


class qa_xfecframe_demapper_cb(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def test_instance(self):
        instance = xfecframe_demapper_cb(FECFRAME_NORMAL, C3_5, MOD_QPSK)
        assert (instance is not None)

    def test_32apsk_short_instance(self):
        instance = xfecframe_demapper_cb(FECFRAME_SHORT, C3_4, MOD_32APSK)
        assert (instance is not None)

    def test_32apsk_short_3_4_reference_round_trip(self):
        bits = [((index * 73 + index // 7) & 1) for index in range(16200)]
        source = blocks.vector_source_b(bits, False)
        interleaver = dtv.dvbs2_interleaver_bb(
            dtv.FECFRAME_SHORT, dtv.C3_4, dtv.MOD_32APSK)
        modulator = dtv.dvbs2_modulator_bc(
            dtv.FECFRAME_SHORT, dtv.C3_4, dtv.MOD_32APSK,
            dtv.INTERPOLATION_OFF)
        demapper = xfecframe_demapper_cb(
            FECFRAME_SHORT, C3_4, MOD_32APSK)
        sink = blocks.vector_sink_b()

        self.tb.connect(source, interleaver, modulator, demapper, sink)
        self.tb.run()

        llrs = sink.data()
        self.assertEqual(len(llrs), len(bits))
        self.assertEqual(
            [(llr >= 128) for llr in llrs],
            [bool(bit) for bit in bits],
        )

    def test_32apsk_short_3_4_fec_round_trip(self):
        frame = [
            ((index * 73 + index // 7) & 1) for index in range(11712)
        ]
        bits = frame * 32

        tx_source = blocks.vector_source_b(bits, False)
        bch_encoder = dtv.dvb_bch_bb(
            dtv.STANDARD_DVBS2, dtv.FECFRAME_SHORT, dtv.C3_4)
        ldpc_encoder = dtv.dvb_ldpc_bb(
            dtv.STANDARD_DVBS2, dtv.FECFRAME_SHORT, dtv.C3_4,
            dtv.MOD_OTHER)
        encoded_sink = blocks.vector_sink_b()
        self.tb.connect(
            tx_source, bch_encoder, ldpc_encoder, encoded_sink)
        self.tb.run()

        self.tb = gr.top_block()
        encoded_source = blocks.vector_source_b(encoded_sink.data(), False)
        interleaver = dtv.dvbs2_interleaver_bb(
            dtv.FECFRAME_SHORT, dtv.C3_4, dtv.MOD_32APSK)
        modulator = dtv.dvbs2_modulator_bc(
            dtv.FECFRAME_SHORT, dtv.C3_4, dtv.MOD_32APSK,
            dtv.INTERPOLATION_OFF)
        demapper = xfecframe_demapper_cb(
            FECFRAME_SHORT, C3_4, MOD_32APSK)
        llr_sink = blocks.vector_sink_b()
        self.tb.connect(
            encoded_source, interleaver, modulator, demapper, llr_sink)
        self.tb.run()

        self.tb = gr.top_block()
        llr_source = blocks.vector_source_b(llr_sink.data(), False)
        ldpc_decoder = ldpc_decoder_bb(
            STANDARD_DVBS2, FECFRAME_SHORT, C3_4, MOD_32APSK,
            OM_MESSAGE, INFO_OFF, 25, 0)
        bch_decoder = bch_decoder_bb(
            STANDARD_DVBS2, FECFRAME_SHORT, C3_4, OM_MESSAGE, 0)
        decoded_sink = blocks.vector_sink_b()
        self.tb.connect(
            llr_source, ldpc_decoder, bch_decoder, decoded_sink)
        self.tb.run()

        expected = [
            sum(bits[offset + bit] << (7 - bit) for bit in range(8))
            for offset in range(0, len(bits), 8)
        ]
        self.assertEqual(bch_decoder.get_frame_count(), 32)
        self.assertEqual(bch_decoder.get_error_count(), 0)
        self.assertEqual(list(decoded_sink.data()), expected)


if __name__ == '__main__':
    gr_unittest.run(qa_xfecframe_demapper_cb)
