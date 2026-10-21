"""Experiment-only CPU decoder taps and exact SIMD-batch LLR replay.

Run with ``python3 -m gnuradio.dvbs2rx.reference --help``. This is not a
WaveScope storage or API contract.
"""

import argparse
import hashlib
import json
from contextlib import ExitStack
from pathlib import Path

import numpy as np
import pmt
from gnuradio import analog, blocks, gr

from . import params

try:
    from gnuradio import dvbs2rx
except ImportError:
    from python import dvbs2rx

SCHEMA = "gr-dvbs2rx.reference/1"
FILES = {
    "symbols": ("symbols.cf32", "<c8"),
    "llrs": ("ldpc-input.i8", "i1"),
    "codewords": ("ldpc-codewords.u8", "u1"),
    "ldpc": ("ldpc-message.u8", "u1"),
    "bch": ("bch-message.u8", "u1"),
}


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _write_json(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def _fec(config, outputmode=None):
    standard, size, rate, constellation = params.translate(
        "DVB-S2", config["frame_size"], config["code_rate"],
        config["constellation"])
    ldpc = dvbs2rx.ldpc_decoder_bb(
        standard, size, rate, constellation,
        dvbs2rx.OM_MESSAGE if outputmode is None else outputmode,
        dvbs2rx.INFO_OFF, config["max_trials"], 0)
    bch = dvbs2rx.bch_decoder_bb(standard, size, rate, dvbs2rx.OM_MESSAGE, 0)
    return ldpc, bch


def _geometry(config):
    if (type(config["max_trials"]) is not int or config["max_trials"] <= 0
            or type(config["pilots"]) is not bool):
        raise ValueError(
            "max_trials must be positive and pilots must be Boolean")
    modcod = params.dvbs2_pls(config["constellation"], config["code_rate"],
                              config["frame_size"], config["pilots"]) >> 2
    if config.get("modcod", modcod) != modcod:
        raise ValueError("MODCOD does not match constellation and code rate")
    info = params.pl_info(config["constellation"], config["code_rate"],
                          config["frame_size"], config["pilots"])
    n = 16200 if config["frame_size"] == "short" else 64800
    ldpc, bch = _fec(config)
    k_bytes = round(ldpc.relative_rate() * n)
    kbch_bytes = round(bch.relative_rate() * k_bytes)
    return {
        "symbols": info["xfecframe_len"],
        "llrs": n,
        "codewords": n // 8,
        "ldpc": k_bytes,
        "bch": kbch_bytes,
    }


class _StreamTap(gr.sync_block):

    def __init__(self, name, dtype, stream, tag_key=None):
        gr.sync_block.__init__(self,
                               name=name,
                               in_sig=[np.dtype(dtype).type],
                               out_sig=[])
        self.stream = stream
        self.dtype = dtype
        self.tag_key = tag_key
        self.tags = []

    def work(self, input_items, output_items):
        data = input_items[0]
        self.stream.write(data.astype(self.dtype, copy=False).tobytes())
        if self.tag_key is not None:
            for tag in self.get_tags_in_window(0, 0, len(data),
                                               pmt.intern(self.tag_key)):
                self.tags.append((int(tag.offset), pmt.to_python(tag.value)))
        return len(data)


class ReferenceExport:
    """Attach only when explicitly requested; finish after top_block.wait().

    Fixed-MODCOD graphs only. All frame ordinals count frames actually emitted
    by PL sync, not transmitted frames. Source ranges retain acquisition gaps.
    """

    def __init__(self, top, directory, config, symbols, demapper, ldpc, bch,
                 provenance):
        self.directory = Path(directory)
        self.config = dict(config)
        self.config["modcod"] = params.dvbs2_pls(
            config["constellation"], config["code_rate"], config["frame_size"],
            config["pilots"]) >> 2
        self.provenance = dict(provenance)
        self.lengths = _geometry(config)
        self.directory.mkdir(parents=True, exist_ok=False)
        self.resources = ExitStack()
        self.taps = {}
        try:
            streams = {}
            for key, (filename, dtype) in FILES.items():
                streams[key] = self.resources.enter_context(
                    (self.directory / filename).open("xb"))
                if key == "codewords":
                    continue
                tag = {
                    "symbols": "PLFRAME_SOURCE_RANGE",
                    "ldpc": "ldpc_reference",
                    "bch": "bch_frame_status"
                }.get(key)
                self.taps[key] = _StreamTap("reference " + key, dtype,
                                            streams[key], tag)
            self.codewords = streams["codewords"]
            ldpc.set_reference_export(True)
            for key, source in (("symbols", symbols), ("llrs", demapper),
                                ("ldpc", ldpc), ("bch", bch)):
                top.connect(source, self.taps[key])
        except BaseException:
            self.resources.close()
            raise

    def close(self):
        self.resources.close()

    def finish(self):
        """Validate every stage and write the completion manifest last."""
        if self.codewords.closed:
            raise ValueError("reference export is already closed")
        batches = []
        count = 0
        for offset, (meta, llrs) in self.taps["ldpc"].tags:
            batch = {
                "first_frame": int(meta["frame_cnt"]),
                "frames": int(meta["simd_size"]),
                "batch_converged": bool(meta["batch_converged"]),
                "batch_trials": int(meta["batch_trials"]),
                "implementation": meta["implementation"],
            }
            if (batch["first_frame"] != count or batch["frames"] <= 0
                    or offset != count * self.lengths["ldpc"]
                    or len(llrs) != batch["frames"] * self.lengths["llrs"]):
                raise ValueError("missing or unordered LDPC batch")
            self.codewords.write(
                np.packbits(np.asarray(llrs) >= 128).tobytes())
            batches.append(batch)
            count += batch["frames"]
        self.close()
        if count == 0:
            raise ValueError(
                "no decoded frames; reference export is incomplete")
        files = {}
        for key, (name, dtype) in FILES.items():
            path = self.directory / name
            size = path.stat().st_size
            if size != count * self.lengths[key] * np.dtype(dtype).itemsize:
                raise ValueError("incomplete reference stream: " + name)
            files[key] = {"bytes": size, "sha256": _sha256(path)}
        statuses = self.taps["bch"].tags
        if len(statuses) != count:
            raise ValueError("missing BCH frame status")
        ranges = dict(self.taps["symbols"].tags)
        frames = []
        for index, (offset, status) in enumerate(statuses):
            if (offset != index * self.lengths["bch"]
                    or status["frame_index"] != index):
                raise ValueError("unordered BCH frame status")
            frames.append({
                "frame":
                index,
                "source_range":
                ranges.get(index * self.lengths["symbols"]),
                "bch_valid":
                status["valid"],
                "bch_corrections":
                status["corrections"],
            })
        manifest = {
            "schema": SCHEMA,
            "config": self.config,
            "provenance": self.provenance,
            "lengths": self.lengths,
            "files": files,
            "batches": batches,
            "frames": frames,
        }
        _write_json(self.directory / "manifest.json", manifest)
        return manifest


def replay(directory):
    """Replay SIMD batches, checking full LDPC and BCH bytes and status."""
    directory = Path(directory)
    manifest = json.loads((directory / "manifest.json").read_text())
    if manifest["schema"] != SCHEMA:
        raise ValueError("unsupported reference schema")
    config = manifest["config"]
    lengths = _geometry(config)
    if manifest["lengths"] != lengths:
        raise ValueError("invalid reference frame geometry")
    count = len(manifest["frames"])
    for key, (name, dtype) in FILES.items():
        path = directory / name
        expected = manifest["files"][key]
        size = count * lengths[key] * np.dtype(dtype).itemsize
        if (expected["bytes"] != size or path.stat().st_size != size
                or _sha256(path) != expected["sha256"]):
            raise ValueError("reference integrity check failed: " + name)
    cursor = 0
    with ExitStack() as resources:
        streams = {
            key: resources.enter_context((directory / name).open("rb"))
            for key, (name, _) in FILES.items()
        }
        for batch in manifest["batches"]:
            size = batch["frames"]
            if (batch["first_frame"] != cursor or not 1 <= size <= 32
                    or cursor + size > count):
                raise ValueError("invalid reference batch boundaries")
            top = gr.top_block()
            ldpc, _ = _fec(config, dvbs2rx.OM_CODEWORD)
            source = blocks.vector_source_b(
                tuple(streams["llrs"].read(size * lengths["llrs"])), False)
            sink = blocks.vector_sink_b()
            ldpc.set_reference_export(True)
            top.connect(source, ldpc, sink)
            top.run()
            decoded = bytes(sink.data())
            if decoded != streams["codewords"].read(size *
                                                    lengths["codewords"]):
                raise ValueError("LDPC codeword mismatch at frame " +
                                 str(cursor))
            tags = [
                tag for tag in sink.tags()
                if pmt.eq(tag.key, pmt.intern("ldpc_reference"))
            ]
            if len(tags) != 1:
                raise ValueError("CPU replay did not preserve SIMD batch")
            meta = pmt.to_python(pmt.car(tags[0].value))
            for key in ("batch_converged", "batch_trials", "implementation"):
                if meta[key] != batch[key]:
                    raise ValueError(
                        "LDPC replay status/implementation mismatch: " + key)
            words = np.frombuffer(decoded, dtype=np.uint8).reshape(size, -1)
            ldpc_bytes = words[:, :lengths["ldpc"]].tobytes()
            if ldpc_bytes != streams["ldpc"].read(size * lengths["ldpc"]):
                raise ValueError("LDPC message mismatch at frame " +
                                 str(cursor))
            top = gr.top_block()
            _, bch = _fec(config)
            source = blocks.vector_source_b(tuple(ldpc_bytes), False)
            sink = blocks.vector_sink_b()
            top.connect(source, bch, sink)
            top.run()
            if bytes(sink.data()) != streams["bch"].read(size *
                                                         lengths["bch"]):
                raise ValueError("BCH output mismatch at frame " + str(cursor))
            statuses = [
                pmt.to_python(tag.value) for tag in sink.tags()
                if pmt.eq(tag.key, pmt.intern("bch_frame_status"))
            ]
            expected = manifest["frames"][cursor:cursor + size]
            if len(statuses) != size:
                raise ValueError("missing replay BCH status")
            for frame, status in zip(expected, statuses):
                if (frame["frame"] != cursor + status["frame_index"]
                        or frame["bch_valid"] != status["valid"]
                        or frame["bch_corrections"] != status["corrections"]):
                    raise ValueError("BCH status mismatch at frame " +
                                     str(frame["frame"]))
            cursor += size
    if cursor != count or count == 0:
        raise ValueError("incomplete reference batch coverage")
    return {
        "schema": SCHEMA,
        "frames": count,
        "batches": len(manifest["batches"]),
        "ldpc_byte_identical": True,
        "bch_byte_identical": True
    }


def capture(args):
    config = {
        "constellation": args.constellation,
        "code_rate": args.code_rate,
        "frame_size": args.frame_size,
        "pilots": args.pilots,
        "max_trials": args.max_trials
    }
    if args.max_trials <= 0 or args.sps < 1 or not np.isfinite(args.sps):
        raise ValueError("max-trials must be positive and sps finite and >= 1")
    if (args.input.resolve() == args.output.resolve()
            or (args.output.exists() and args.input.samefile(args.output))):
        raise ValueError("input and output must be different files")
    if args.input.stat().st_size % np.dtype("<c8").itemsize:
        raise ValueError("input must contain whole cf32 IQ samples")
    standard, size, rate, constellation = params.translate(
        "DVB-S2", args.frame_size, args.code_rate, args.constellation)
    pls = params.dvbs2_pls(args.constellation, args.code_rate, args.frame_size,
                           args.pilots)
    config["modcod"] = pls >> 2
    top = gr.top_block()
    source = blocks.file_source(gr.sizeof_gr_complex, str(args.input), False)
    agc = analog.agc2_cc(1e-2, 1e-3, 1.0, 1.0)
    agc.set_max_gain(65536)
    rotator = dvbs2rx.rotator_cc(0, True)
    rotator.set_max_output_buffer(1024)
    timing = dvbs2rx.symbol_sync_cc(args.sps, 1e-3, 1.0, 0.35, 5, 128, 0)
    lo, hi = params.pls_filter(pls)
    sync = dvbs2rx.plsync_cc(0, 20, args.sps, 0, False, True, lo, hi)
    demapper = dvbs2rx.xfecframe_demapper_cb(size, rate, constellation)
    ldpc, bch = _fec(config)
    sink = blocks.file_sink(gr.sizeof_char, str(args.output), False)
    top.connect(source, agc, rotator, timing, sync, demapper, ldpc, bch, sink)
    top.connect((timing, 1), (sync, 1))
    top.msg_connect((sync, "rotator_phase_inc"), (rotator, "cmd"))
    top.msg_connect((ldpc, "llr_pdu"), (demapper, "llr_pdu"))
    export = None
    try:
        if args.export_dir is not None:
            provenance = {
                "input": str(args.input),
                "input_sha256": _sha256(args.input),
                "input_bytes": args.input.stat().st_size,
                "decoder_revision": args.decoder_revision,
                "sps": args.sps
            }
            export = ReferenceExport(top, args.export_dir, config, sync,
                                     demapper, ldpc, bch, provenance)
        top.run()
        sink.close()
        if export is not None:
            export.finish()
    finally:
        if export is not None:
            export.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    cap = commands.add_parser("capture",
                              help="decode cf32 IQ; export only on request")
    cap.add_argument("--input", type=Path, required=True)
    cap.add_argument("--output",
                     type=Path,
                     required=True,
                     help="packed BCH message bytes, before BB descrambling")
    cap.add_argument("--constellation",
                     choices=("QPSK", "8PSK", "16APSK", "32APSK"),
                     required=True)
    cap.add_argument("--code-rate", required=True)
    cap.add_argument("--frame-size",
                     choices=("normal", "short"),
                     default="normal")
    cap.add_argument("--pilots", action="store_true")
    cap.add_argument("--sps", type=float, default=2.0)
    cap.add_argument("--max-trials", type=int, default=25)
    cap.add_argument("--export-dir",
                     type=Path,
                     help="new exclusive directory; disabled by default")
    cap.add_argument("--decoder-revision",
                     help="exact fork revision, required when exporting")
    rep = commands.add_parser("replay",
                              help="verify and replay exported LLR batches")
    rep.add_argument("directory", type=Path)
    args = parser.parse_args()
    if args.command == "capture":
        if args.export_dir is not None and not args.decoder_revision:
            parser.error("--decoder-revision is required with --export-dir")
        capture(args)
    else:
        print(json.dumps(replay(args.directory), sort_keys=True))


if __name__ == "__main__":
    main()
