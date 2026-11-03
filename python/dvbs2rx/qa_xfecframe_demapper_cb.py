#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2023 Igor Freire.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

from gnuradio import gr, gr_unittest

try:
    from python.dvbs2rx import (C3_4, C3_5, C25_36, FECFRAME_NORMAL,
                                FECFRAME_SHORT, MOD_16APSK, MOD_32APSK,
                                MOD_QPSK, xfecframe_demapper_cb)
except ImportError:
    from gnuradio.dvbs2rx import (C3_4, C3_5, C25_36, FECFRAME_NORMAL,
                                  FECFRAME_SHORT, MOD_16APSK, MOD_32APSK,
                                  MOD_QPSK, xfecframe_demapper_cb)


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

    def test_s2x_16apsk_25_36_instance(self):
        instance = xfecframe_demapper_cb(FECFRAME_NORMAL, C25_36, MOD_16APSK)
        assert (instance is not None)

    def test_s2x_16apsk_25_36_rejects_short_frames(self):
        with self.assertRaises(RuntimeError):
            xfecframe_demapper_cb(FECFRAME_SHORT, C25_36, MOD_16APSK)


if __name__ == '__main__':
    gr_unittest.run(qa_xfecframe_demapper_cb)
