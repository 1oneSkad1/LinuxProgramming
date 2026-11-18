# Reusable Parallel Traffic Analyzer

A C11/POSIX program for analyzing files that use the course's custom `pkt` binary format. Input discovery, buffered reading, parsing, aggregation, parallel execution, and reporting are implemented as separate modules.

## Quick start

From this directory:

```bash
make
./traffic_analyzer --help

./traffic_analyzer --input ../data/2026.08.02/pkt \
  --workers 4 --output results/day1_parallel

./traffic_analyzer --input ../data/2026.08.02/pkt \
  --mode sequential --output results/day1_sequential
```

A single packet file can also be supplied through `--input`. Use a new output directory for each run. Files are read after collection or downloading has finished.

Directory input selects immediate regular files matching `pkt_YYYY.MM.DD_HH.MM`, including the supported underscore separators. Subdirectories, symbolic links, text dumps, and numbered download duplicates are excluded. An explicitly supplied regular file has no filename restriction.

## Command-line options

| Option | Default | Meaning |
|---|---|---|
| `--input PATH` | Required | Packet directory or a single regular file |
| `--output NEW_DIR` | Required | New directory for this run |
| `--mode sequential\|threaded` | `threaded` | Main-thread analysis or pthread worker pool |
| `--workers N` | 4 | 1 to 128 workers; sequential mode uses one analysis worker |
| `--interval SECONDS` | 60 | Time-bin width from 1 to 86,400 seconds |
| `--top K` | 10 | Number of highest-traffic hosts to report |
| `--buffer-size BYTES` | 1,048,576 | Per-worker read buffer, from 16 to 67,108,864 bytes |
| `--timezone-minutes OFFSET` | 540 | Fixed display offset from UTC, from -720 to 840 minutes |
| `--quiet` | Off | Suppress progress and phase messages |

Time bins use `floor(epoch_seconds / interval) * interval`. The fixed timezone offset affects displayed timestamps. The default offset is UTC+09:00. Full timestamps keep different dates in separate bins. Empty bins are omitted.

## Progress display

Input scanning is announced immediately. During analysis, progress is refreshed approximately once per second on standard error. It includes completed files, file-based percentage, bytes read, failed files, and elapsed time.

Interactive terminals update a single line. Redirected output records a new line for each update. Read-byte counts include data prefetched into read buffers. A single-file run shows changing byte counts and elapsed time while its completed-file percentage remains at zero until file processing finishes.

Failed files count as finished files. Statistics merging and CSV writing continue after file processing reaches 100%. The final summary and exit code identify complete and partial results. `--quiet` disables the progress-monitor thread and display messages. Sequential mode performs analysis on the main thread, with a separate progress monitor when enabled.

## Binary input format

Each record contains a 12-byte storage header followed by `stored_len` packet bytes:

| Offset | Size | Field |
|---|---:|---|
| 0 | 4 bytes | Unix timestamp in seconds, little-endian |
| 4 | 4 bytes | Microseconds, little-endian |
| 8 | 2 bytes | Original packet length, `pkt_len`, little-endian |
| 10 | 2 bytes | Stored packet length, `stored_len`, little-endian |

The stored data starts with an IPv4 header. IPv4 address and length fields use network byte order. The parser explicitly decodes bytes and avoids dependence on C structure padding or host byte order.

## Statistics

- `records` counts fully read storage records. A truncated header or payload is excluded.
- `packets` counts records accepted for IPv4 aggregation.
- `bytes` sums the original `pkt_len` of accepted records.
- `skipped_records` counts zero-length stored records and non-IPv4 records.
- `invalid_records` counts fully read records that fail timestamp, length, or IPv4-header validation.
- Each packet contributes once to its source's transmit counters and once to its destination's receive counters, including when both addresses match.
- The sum of all hosts' transmitted bytes equals the overall byte count. The same holds independently for received bytes.
- Top-K hosts are sorted by combined transmit and receive bytes, descending. Ties use numeric IPv4 order.
- IP fragments contribute to host statistics. Current aggregation uses IPv4 addresses and timestamps.

## Output and error handling

| Output | Contents |
|---|---|
| `summary.csv` | Overall status, file outcomes, record/packet/byte totals, unique hosts |
| `hosts.csv` | All host statistics in numeric IP order |
| `timeseries.csv` | Time-bin epoch, displayed timestamp, UTC offset, packets, and bytes |
| `top_hosts.csv` | Highest-traffic hosts |
| `errors.log` | Failed file paths and their first error offsets/reasons |
| `run_metrics.csv` | Execution settings, input sizes, timings, throughput, and memory |

A truncated file stops at the incomplete record while other files continue. Fully read malformed records are reported, and processing continues using the stored record length. Valid records already aggregated from a failed file are included in the partial result.

Global failures such as memory exhaustion or worker-creation failure return an error. Output failures can leave a partially written result directory. Check the process exit code:

| Code | Meaning |
|---|---|
| `0` | All selected files were processed successfully |
| `1` | File errors produced partial results; inspect `errors.log` |
| `2` | Invalid options, input-discovery failure, or a system/output error |

Inputs must remain unchanged throughout analysis. File size and modification time are checked before and after reading.

## Timing and memory metrics

`elapsed_seconds` covers startup, input discovery, analysis, merging, and the main report files. Writing the final metrics CSV itself is excluded. `processing_seconds` covers worker execution, excluding the final merge and reports. Throughput is based on the overall elapsed time.

`max_rss_kib` uses Linux `/proc/self/status` `VmHWM`, falling back to `getrusage` when unavailable. `getrusage_max_rss_kib` preserves the latter value separately because a launcher process can influence its high-water mark. Memory values use KiB.

## Implementation modules

| Module | Responsibility |
|---|---|
| `src/main.c` | CLI parsing, execution mode, lifecycle, merging, and reporting |
| `src/file_scan.c` | Directory traversal, filename selection, and sorted file jobs |
| `src/reader.c` | Buffered `read()`, short reads, `EINTR`, and record boundaries |
| `src/parser.c` | Storage-header and IPv4 decoding and validation |
| `src/worker.c` | Mutex-protected job dispatch and per-file error handling |
| `src/stats.c` | Per-worker host/time hash tables and deterministic merging |
| `src/progress.c` | Periodic progress monitor and final processing update |
| `src/report.c` | CSV output, errors, and execution metrics |

Each analysis worker owns its input buffer and statistics. The queue mutex protects job assignment and completion counters. The progress monitor reads these counters and an atomic read-byte total. Statistics memory grows with unique hosts and worker count. File-level parallelism distributes whole files across workers.

## Tests and experiment reproduction

```bash
make test
make sanitize
```

The 14-test suite covers hand-calculated expectations, worker-count equivalence, buffer boundaries, malformed/truncated input, empty input, hash-table growth, same-address send/receive traffic, date separation, input selection, output protection, and progress/quiet behavior. The sanitizer target runs AddressSanitizer and UndefinedBehaviorSanitizer.

Compare full-input sequential and four-worker results, then validate representative files with an independent Python parser:

```bash
python3 scripts/validate_real.py --input ../data/2026.08.02/pkt \
  --output results/full_day_check
```

Run a repeat benchmark on 24 files sampled across the day:

```bash
python3 scripts/benchmark.py --input ../data/2026.08.02/pkt \
  --output results/benchmark_check --sample-files 24 --repeats 3
```

`--sample-files 0` benchmarks the full input. The benchmark performs an unmeasured warm-up, randomizes the timed order, runs sequential and threaded 1/2/4/8-worker modes, and checks analytical CSV hashes before publishing comparisons. It passes `--quiet` to the executable. `comparison.csv` records median/minimum/maximum time, speedup, throughput, and peak RSS. `environment.json` records the machine, compiler, and cache policy.

Caches are retained. WSL `/mnt/c`, storage performance, and file-size distribution affect timings. The recorded reference results are in `../docs/results/`.

The optional `scripts/plot_benchmark.mjs` uses the Codex-bundled `@oai/artifact-tool` renderer. The default build, tests, and CSV benchmarks use GCC, Make, and Python. The committed comparison chart can be viewed directly in the root README.

## Supplied-data issue

`pkt_2026.08.02_19.27` ends with a request for 28 stored bytes and only 25 remaining bytes. The server and local copies have the same size, 3,145,728 bytes, and the same SHA-256. Full-day analysis therefore returns a partial result and exit code `1` while retaining all valid preceding records. Evidence is included in `../docs/results/full_day/source_issue.json`.

`validate_real.py` accepts file-error exit code `1` for equality checks, including identical summaries and error logs. Benchmark timing comparisons require successful input processing.
