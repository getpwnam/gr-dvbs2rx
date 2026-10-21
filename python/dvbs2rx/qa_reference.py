#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
import tempfile
from pathlib import Path

import numpy as np
from gnuradio import blocks, dtv, gr, gr_unittest

try:
    from gnuradio import dvbs2rx
    from gnuradio.dvbs2rx.reference import (ReferenceExport, _fec, _geometry,
                                            replay)
except ImportError:
    from python import dvbs2rx
    from python.dvbs2rx.reference import (ReferenceExport, _fec, _geometry,
                                          replay)


class qa_reference(gr_unittest.TestCase):

    def _symbols(self, config, count):
        lengths = _geometry(config)
        _, size, rate, constellation = dvbs2rx.params.translate(
            "DVB-S2", config["frame_size"], config["code_rate"],
            config["constellation"])
        # The encoder consumes one unpacked bit per item.
        rng = np.random.RandomState(1260)
        bits = rng.randint(0, 2, lengths["bch"] * 8 * count).tolist()
        top = gr.top_block()
        source = blocks.vector_source_b(bits, False)
        bch = dtv.dvb_bch_bb(dtv.STANDARD_DVBS2, int(size), int(rate))
        ldpc = dtv.dvb_ldpc_bb(dtv.STANDARD_DVBS2, int(size), int(rate),
                               dtv.MOD_OTHER)
        interleaver = dtv.dvbs2_interleaver_bb(int(size), int(rate),
                                               int(constellation))
        modulator = dtv.dvbs2_modulator_bc(int(size), int(rate),
                                           int(constellation),
                                           dtv.INTERPOLATION_OFF)
        sink = blocks.vector_sink_c()
        top.connect(source, bch, ldpc, interleaver, modulator, sink)
        top.run()
        return sink.data(), np.packbits(bits).tobytes()

    def _decode(self, symbols, config, directory=None):
        _, size, rate, constellation = dvbs2rx.params.translate(
            "DVB-S2", config["frame_size"], config["code_rate"],
            config["constellation"])
        top = gr.top_block()
        source = blocks.vector_source_c(symbols, False)
        demapper = dvbs2rx.xfecframe_demapper_cb(size, rate, constellation)
        ldpc, bch = _fec(config)
        sink = blocks.vector_sink_b()
        top.connect(source, demapper, ldpc, bch, sink)
        export = None
        try:
            if directory is not None:
                export = ReferenceExport(top, directory, config, source,
                                         demapper, ldpc, bch,
                                         {"decoder_revision": "qa"})
            top.run()
            manifest = export.finish() if export is not None else None
            if export is None:
                self.assertFalse(
                    any(
                        str(tag.key) == "ldpc_reference"
                        for tag in sink.tags()))
            return bytes(sink.data()), manifest
        finally:
            if export is not None:
                export.close()

    def test_qpsk_and_apsk_export_replay(self):
        for constellation, code, frame_size, count in (("QPSK", "3/4", "short",
                                                        3), ("16APSK", "3/4",
                                                             "normal", 35),
                                                       ("32APSK", "3/4",
                                                        "short", 3)):
            with self.subTest(constellation=constellation):
                config = {
                    "constellation": constellation,
                    "code_rate": code,
                    "frame_size": frame_size,
                    "pilots": False,
                    "max_trials": 25
                }
                symbols, expected = self._symbols(config, count)
                with tempfile.TemporaryDirectory() as temp:
                    directory = Path(temp) / "reference"
                    disabled, _ = self._decode(symbols, config)
                    enabled, manifest = self._decode(symbols, config,
                                                     directory)
                    self.assertEqual(disabled, expected)
                    self.assertEqual(enabled, disabled)
                    self.assertEqual(len(manifest["frames"]), count)
                    self.assertTrue(
                        all(frame["bch_valid"]
                            for frame in manifest["frames"]))
                    self.assertEqual(replay(directory)["frames"], count)
                    with self.assertRaises(FileExistsError):
                        self._decode(symbols, config, directory)
                    path = directory / "ldpc-input.i8"
                    data = bytearray(path.read_bytes())
                    data[0] ^= 1
                    path.write_bytes(data)
                    with self.assertRaisesRegex(ValueError, "integrity"):
                        replay(directory)

    def test_failed_frames_and_incomplete_export(self):
        config = {
            "constellation": "QPSK",
            "code_rate": "3/4",
            "frame_size": "short",
            "pilots": False,
            "max_trials": 1
        }
        rng = np.random.RandomState(1260)
        count = 3
        length = _geometry(config)["symbols"] * count
        symbols = tuple((rng.randn(length) + 1j * rng.randn(length)) / 2)
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp) / "reference"
            _, manifest = self._decode(symbols, config, directory)
            self.assertEqual([frame["frame"] for frame in manifest["frames"]],
                             list(range(count)))
            self.assertTrue(
                any(not frame["bch_valid"] for frame in manifest["frames"]))
            self.assertTrue(
                any(not batch["batch_converged"]
                    for batch in manifest["batches"]))
            self.assertEqual(replay(directory)["frames"], count)
            path = directory / "manifest.json"
            manifest["batches"][0]["first_frame"] = 1
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "boundaries"):
                replay(directory)
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp) / "empty"
            with self.assertRaisesRegex(ValueError, "no decoded frames"):
                self._decode((), config, directory)
            self.assertFalse((directory / "manifest.json").exists())


if __name__ == "__main__":
    gr_unittest.run(qa_reference)
