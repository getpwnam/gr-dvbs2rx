# Utility Tools and Scripts

## IQ Recordings CLI

The IQ Recordings CLI (`iq-rec-cli`) is a command line tool that can help manage and test the IQ recordings generated with the `dvbs2-rec` application.

First, you can use it to list the available recordings:

```bash
./iq-rec-cli ls
```

By default, the CLI assumes the recordings are in the `data/iq/` directory relative to the root of the repository. If that is not the case, you can switch the directory using option `--data-dir`.

Next, you can select one of the recordings and test the receiver on it. The CLI will run `dvbs2-rx` reading IQ samples from the file and will measure the number of decoded bytes, the processing duration, and the corresponding throughput. You can run the test as follows:

```bash
./iq-rec-cli rx REC_NAME
```

Note `REC_NAME` is the name of the recording as displayed in the first column returned by the list (`ls`) command.

The above command can also measure the CPU utilization of each thread spawned by the receiver application. To do so, run it with the `--cpu` option. Also, if you would like to plot the per-thread CPU utilization over time, use the `--plot-cpu` switch, as follows:

```bash
./iq-rec-cli rx REC_NAME --cpu --plot-cpu
```

Finally, the CLI can benchmark implementation performances using a selected recording. In this case, it compares the performance of `dvbs2-rx` to the one achieved with the `leandvb` application, which must be installed separately. The benchmark can be launched as follows:

```bash
./iq-rec-cli bench REC_NAME
```

Like the `rx` command, the `bench` command supports the `--cpu` and `--plot-cpu` options to measure and plot the CPU utilization of the receiver threads.

## Scan-Driven Autotuning (Separate Utility)

The `dvbs2-autotune` utility is a standalone script that automates receiver parameter search from scan results, without changing the main scan or receiver code paths.

The script takes scan candidates from a JSON or CSV file and runs short headless `dvbs2-rx` trials. It polls the monitoring server (`--mon-server`) and ranks each trial using a score that prioritizes lock and decode quality.

Example:

```bash
./dvbs2-autotune \
  --scan-file /path/to/scan_results.json \\
  --source soapy \\
  --soapy-device "driver=remote,remote=172.17.128.253:55132,biastee=0" \\
  --samp-rate 12e6 \\
  --default-sym-rate 1.5e6 \\
  --max-trials 24 \\
  --trial-seconds 8 \\
  --result-json autotune-results.json
```

Accepted scan formats:

- JSON list of candidates, e.g. `[{"freq": 741.5e6, "sym_rate": 1.5e6, "score": 12.3}]`
- JSON object with `candidates` list
- CSV with headers including `freq` (or `frequency`) and optional `sym_rate` and `score`

The script prints the best `dvbs2-rx` command at the end so you can launch a GUI run using the selected parameters.

You can also skip the scan file entirely and pass candidates directly on the command line:

```bash
./dvbs2-autotune \
  --freq-candidate 741.50e6 \
  --freq-candidate 741.55e6 \
  --sym-rate-candidate 1.5e6 \
  --source soapy \
  --soapy-device "driver=remote,remote=172.17.128.253:55132,biastee=0" \
  --samp-rate 12e6 \
  --max-trials 16
```

If only one `--sym-rate-candidate` is provided, it is applied to all frequencies.
