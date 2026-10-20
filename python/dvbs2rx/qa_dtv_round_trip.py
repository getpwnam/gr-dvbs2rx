#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2023 Igor Freire.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

from math import pi

import numpy as np
import pmt
from gnuradio import blocks, dtv, gr, gr_unittest

try:
    from gnuradio.dvbs2rx import (C3_4, C4_5, C5_6, FECFRAME_NORMAL,
                                  FECFRAME_SHORT, INFO_OFF, MOD_16APSK,
                                  MOD_32APSK, OM_MESSAGE, STANDARD_DVBS2,
                                  bch_decoder_bb, ldpc_decoder_bb, plsync_cc,
                                  rotator_cc, xfecframe_demapper_cb)
except ImportError:
    from python.dvbs2rx import (C3_4, C4_5, C5_6, FECFRAME_NORMAL,
                                FECFRAME_SHORT, INFO_OFF, MOD_16APSK,
                                MOD_32APSK, OM_MESSAGE, STANDARD_DVBS2,
                                bch_decoder_bb, ldpc_decoder_bb, plsync_cc,
                                rotator_cc, xfecframe_demapper_cb)


class qa_dtv_round_trip(gr_unittest.TestCase):

    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def test_pilotless_carrier_rotator_transitions(self):
        for order, constellation, rate, modcod in (
            (4, dtv.MOD_QPSK, dtv.C1_2, 4),
            (8, dtv.MOD_8PSK, dtv.C3_5, 12),
        ):
            for offset in (-0.18, 0.18):
                with self.subTest(order=order, offset=offset):
                    self.tb = gr.top_block()
                    length = 16200 // int(np.log2(order))
                    rng = np.random.RandomState(42)
                    payload = np.exp(2j * pi *
                                     rng.randint(order, size=length) / order)
                    source = blocks.vector_source_c(tuple(payload) * 24)
                    framer = dtv.dvbs2_physical_cc(dtv.FECFRAME_SHORT, rate,
                                                   constellation,
                                                   dtv.PILOTS_OFF, 0)
                    symbols = blocks.keep_m_in_n(gr.sizeof_gr_complex, 1, 2, 0)
                    channel = rotator_cc(2 * pi * offset)
                    correction = rotator_cc(0, True)
                    correction.set_max_output_buffer(1024)
                    pls = (modcod << 2) | 2
                    sync = plsync_cc(0, 4, 1.0, 0, False, False, 1 << pls, 0)
                    sink = blocks.vector_sink_c()
                    applied = blocks.vector_sink_c()
                    feedback = blocks.message_debug()
                    self.tb.connect(source, framer, symbols, channel,
                                    correction, sync, sink)
                    self.tb.connect(correction, applied)
                    self.tb.msg_connect((sync, 'rotator_phase_inc'),
                                        (correction, 'cmd'))
                    self.tb.msg_connect((sync, 'rotator_phase_inc'),
                                        (feedback, 'store'))
                    self.tb.run()

                    self.assertGreater(sync.get_frame_count(), 12)
                    self.assertGreater(feedback.num_messages(), 2)
                    applied_tags = [
                        tag for tag in applied.tags()
                        if pmt.eq(tag.key, pmt.intern("rot_phase_inc"))
                    ]
                    self.assertGreater(len(applied_tags), 0)
                    for index in range(feedback.num_messages()):
                        message = feedback.get_message(index)
                        increment = pmt.to_double(
                            pmt.dict_ref(message, pmt.intern("inc"),
                                         pmt.PMT_NIL))
                        self.assertAlmostEqual(-increment / (2 * pi),
                                               offset,
                                               delta=1e-4)
                    self.assertAlmostEqual(sync.get_freq_offset(),
                                           offset,
                                           delta=1e-4)
                    corrected = np.asarray(sink.data()[-length:])
                    residual = corrected / payload
                    phase_steps = np.angle(residual[1:] * residual[:-1].conj())
                    self.assertLess(np.max(np.abs(phase_steps)), 1e-3)

    def test_16apsk_normal_5_6_reference_round_trip(self):
        bits = [((index * 73 + index // 7) & 1) for index in range(64800)]
        source = blocks.vector_source_b(bits, False)
        interleaver = dtv.dvbs2_interleaver_bb(dtv.FECFRAME_NORMAL, dtv.C5_6,
                                               dtv.MOD_16APSK)
        modulator = dtv.dvbs2_modulator_bc(dtv.FECFRAME_NORMAL, dtv.C5_6,
                                           dtv.MOD_16APSK,
                                           dtv.INTERPOLATION_OFF)
        demapper = xfecframe_demapper_cb(FECFRAME_NORMAL, C5_6, MOD_16APSK)
        sink = blocks.vector_sink_b()

        self.tb.connect(source, interleaver, modulator, demapper, sink)
        self.tb.run()

        llrs = sink.data()
        self.assertEqual(len(llrs), len(bits))
        self.assertEqual(
            [(llr >= 128) for llr in llrs],
            [bool(bit) for bit in bits],
        )

    def test_16apsk_normal_4_5_reference_round_trip(self):
        bits = [((index * 73 + index // 7) & 1) for index in range(64800)]
        source = blocks.vector_source_b(bits, False)
        interleaver = dtv.dvbs2_interleaver_bb(dtv.FECFRAME_NORMAL, dtv.C4_5,
                                               dtv.MOD_16APSK)
        modulator = dtv.dvbs2_modulator_bc(dtv.FECFRAME_NORMAL, dtv.C4_5,
                                           dtv.MOD_16APSK,
                                           dtv.INTERPOLATION_OFF)
        demapper = xfecframe_demapper_cb(FECFRAME_NORMAL, C4_5, MOD_16APSK)
        sink = blocks.vector_sink_b()

        self.tb.connect(source, interleaver, modulator, demapper, sink)
        self.tb.run()

        llrs = sink.data()
        self.assertEqual(len(llrs), len(bits))
        self.assertEqual(
            [(llr >= 128) for llr in llrs],
            [bool(bit) for bit in bits],
        )

    def test_16apsk_normal_5_6_fec_round_trip(self):
        frame = [((index * 73 + index // 7) & 1) for index in range(53840)]
        bits = frame * 32

        tx_source = blocks.vector_source_b(bits, False)
        bch_encoder = dtv.dvb_bch_bb(dtv.STANDARD_DVBS2, dtv.FECFRAME_NORMAL,
                                     dtv.C5_6)
        ldpc_encoder = dtv.dvb_ldpc_bb(dtv.STANDARD_DVBS2, dtv.FECFRAME_NORMAL,
                                       dtv.C5_6, dtv.MOD_OTHER)
        encoded_sink = blocks.vector_sink_b()
        self.tb.connect(tx_source, bch_encoder, ldpc_encoder, encoded_sink)
        self.tb.run()

        self.tb = gr.top_block()
        encoded_source = blocks.vector_source_b(encoded_sink.data(), False)
        interleaver = dtv.dvbs2_interleaver_bb(dtv.FECFRAME_NORMAL, dtv.C5_6,
                                               dtv.MOD_16APSK)
        modulator = dtv.dvbs2_modulator_bc(dtv.FECFRAME_NORMAL, dtv.C5_6,
                                           dtv.MOD_16APSK,
                                           dtv.INTERPOLATION_OFF)
        demapper = xfecframe_demapper_cb(FECFRAME_NORMAL, C5_6, MOD_16APSK)
        llr_sink = blocks.vector_sink_b()
        self.tb.connect(encoded_source, interleaver, modulator, demapper,
                        llr_sink)
        self.tb.run()

        self.tb = gr.top_block()
        llr_source = blocks.vector_source_b(llr_sink.data(), False)
        ldpc_decoder = ldpc_decoder_bb(STANDARD_DVBS2, FECFRAME_NORMAL, C5_6,
                                       MOD_16APSK, OM_MESSAGE, INFO_OFF, 25, 0)
        bch_decoder = bch_decoder_bb(STANDARD_DVBS2, FECFRAME_NORMAL, C5_6,
                                     OM_MESSAGE, 0)
        decoded_sink = blocks.vector_sink_b()
        self.tb.connect(llr_source, ldpc_decoder, bch_decoder, decoded_sink)
        self.tb.run()

        expected = [
            sum(bits[offset + bit] << (7 - bit) for bit in range(8))
            for offset in range(0, len(bits), 8)
        ]
        self.assertEqual(bch_decoder.get_frame_count(), 32)
        self.assertEqual(bch_decoder.get_error_count(), 0)
        self.assertEqual(list(decoded_sink.data()), expected)

    def test_16apsk_normal_4_5_fec_round_trip(self):
        frame = [((index * 73 + index // 7) & 1) for index in range(51648)]
        bits = frame * 32

        tx_source = blocks.vector_source_b(bits, False)
        bch_encoder = dtv.dvb_bch_bb(dtv.STANDARD_DVBS2, dtv.FECFRAME_NORMAL,
                                     dtv.C4_5)
        ldpc_encoder = dtv.dvb_ldpc_bb(dtv.STANDARD_DVBS2, dtv.FECFRAME_NORMAL,
                                       dtv.C4_5, dtv.MOD_OTHER)
        encoded_sink = blocks.vector_sink_b()
        self.tb.connect(tx_source, bch_encoder, ldpc_encoder, encoded_sink)
        self.tb.run()

        self.tb = gr.top_block()
        encoded_source = blocks.vector_source_b(encoded_sink.data(), False)
        interleaver = dtv.dvbs2_interleaver_bb(dtv.FECFRAME_NORMAL, dtv.C4_5,
                                               dtv.MOD_16APSK)
        modulator = dtv.dvbs2_modulator_bc(dtv.FECFRAME_NORMAL, dtv.C4_5,
                                           dtv.MOD_16APSK,
                                           dtv.INTERPOLATION_OFF)
        demapper = xfecframe_demapper_cb(FECFRAME_NORMAL, C4_5, MOD_16APSK)
        llr_sink = blocks.vector_sink_b()
        self.tb.connect(encoded_source, interleaver, modulator, demapper,
                        llr_sink)
        self.tb.run()

        self.tb = gr.top_block()
        llr_source = blocks.vector_source_b(llr_sink.data(), False)
        ldpc_decoder = ldpc_decoder_bb(STANDARD_DVBS2, FECFRAME_NORMAL, C4_5,
                                       MOD_16APSK, OM_MESSAGE, INFO_OFF, 25, 0)
        bch_decoder = bch_decoder_bb(STANDARD_DVBS2, FECFRAME_NORMAL, C4_5,
                                     OM_MESSAGE, 0)
        decoded_sink = blocks.vector_sink_b()
        self.tb.connect(llr_source, ldpc_decoder, bch_decoder, decoded_sink)
        self.tb.run()

        expected = [
            sum(bits[offset + bit] << (7 - bit) for bit in range(8))
            for offset in range(0, len(bits), 8)
        ]
        self.assertEqual(bch_decoder.get_frame_count(), 32)
        self.assertEqual(bch_decoder.get_error_count(), 0)
        self.assertEqual(list(decoded_sink.data()), expected)

    def test_32apsk_short_3_4_reference_round_trip(self):
        bits = [((index * 73 + index // 7) & 1) for index in range(16200)]
        source = blocks.vector_source_b(bits, False)
        interleaver = dtv.dvbs2_interleaver_bb(dtv.FECFRAME_SHORT, dtv.C3_4,
                                               dtv.MOD_32APSK)
        modulator = dtv.dvbs2_modulator_bc(dtv.FECFRAME_SHORT, dtv.C3_4,
                                           dtv.MOD_32APSK,
                                           dtv.INTERPOLATION_OFF)
        demapper = xfecframe_demapper_cb(FECFRAME_SHORT, C3_4, MOD_32APSK)
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
        frame = [((index * 73 + index // 7) & 1) for index in range(11712)]
        bits = frame * 32

        tx_source = blocks.vector_source_b(bits, False)
        bch_encoder = dtv.dvb_bch_bb(dtv.STANDARD_DVBS2, dtv.FECFRAME_SHORT,
                                     dtv.C3_4)
        ldpc_encoder = dtv.dvb_ldpc_bb(dtv.STANDARD_DVBS2, dtv.FECFRAME_SHORT,
                                       dtv.C3_4, dtv.MOD_OTHER)
        encoded_sink = blocks.vector_sink_b()
        self.tb.connect(tx_source, bch_encoder, ldpc_encoder, encoded_sink)
        self.tb.run()

        self.tb = gr.top_block()
        encoded_source = blocks.vector_source_b(encoded_sink.data(), False)
        interleaver = dtv.dvbs2_interleaver_bb(dtv.FECFRAME_SHORT, dtv.C3_4,
                                               dtv.MOD_32APSK)
        modulator = dtv.dvbs2_modulator_bc(dtv.FECFRAME_SHORT, dtv.C3_4,
                                           dtv.MOD_32APSK,
                                           dtv.INTERPOLATION_OFF)
        demapper = xfecframe_demapper_cb(FECFRAME_SHORT, C3_4, MOD_32APSK)
        llr_sink = blocks.vector_sink_b()
        self.tb.connect(encoded_source, interleaver, modulator, demapper,
                        llr_sink)
        self.tb.run()

        self.tb = gr.top_block()
        llr_source = blocks.vector_source_b(llr_sink.data(), False)
        ldpc_decoder = ldpc_decoder_bb(STANDARD_DVBS2, FECFRAME_SHORT, C3_4,
                                       MOD_32APSK, OM_MESSAGE, INFO_OFF, 25, 0)
        bch_decoder = bch_decoder_bb(STANDARD_DVBS2, FECFRAME_SHORT, C3_4,
                                     OM_MESSAGE, 0)
        decoded_sink = blocks.vector_sink_b()
        self.tb.connect(llr_source, ldpc_decoder, bch_decoder, decoded_sink)
        self.tb.run()

        expected = [
            sum(bits[offset + bit] << (7 - bit) for bit in range(8))
            for offset in range(0, len(bits), 8)
        ]
        self.assertEqual(bch_decoder.get_frame_count(), 32)
        self.assertEqual(bch_decoder.get_error_count(), 0)
        self.assertEqual(list(decoded_sink.data()), expected)


if __name__ == '__main__':
    gr_unittest.run(qa_dtv_round_trip)
