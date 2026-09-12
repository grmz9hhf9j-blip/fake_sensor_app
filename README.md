# Fake accelerometer application

A Zephyr application that reads a simulated accelerometer at 100 Hz,
stores timestamped samples in a ring buffer, and outputs them as NDJSON.

The sensor driver lives in a separate repository fetched through west.

The application and tests have been done on Linux using Zephyr
4.4.2 and `native_sim`.

From a new workspace directory:

```sh
git clone https://github.com/grmz9hhf9j-blip/fake_sensor_app project

python3.12 -m venv .venv
source .venv/bin/activate

python -m pip install west

west init -l project
west update

export ZEPHYR_BASE="$PWD/deps/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=host

python -m pip install -r "$ZEPHYR_BASE/scripts/requirements.txt"

cd project
```



## Build and run

From the application repository root:

```sh
west build -b native_sim -d build/app-direct app -- \
  -DZephyr_DIR="$ZEPHYR_BASE/share/zephyr-package/cmake"

west build -d build/app-direct -t run
```

The simulator prints a pseudoterminal path, such as `/dev/pts/5`.
Connect from another terminal, using the path printed by the current run:

```sh
screen /dev/pts/5 115200
```

## Shell commands

```text
samples stream off
samples dump
samples stream on
```

Streaming is enabled by default. Turning it off stops automatic output
but leaves sampling and buffer draining running.

`samples dump` asks the existing consumer to drain and print up to
20 queued samples immediately. The count can be zero if the periodic
consumer has just emptied the buffer.

Example:

```json
{"type":"dump","count":1}
{"t_ms":1000,"ax_ms2":0.000000,"ay_ms2":1.000000,"az_ms2":9.806650}
```

The interactive shell also emits prompts and status messages; the complete
terminal session is not a pure NDJSON file.


## Sampling

The application has:

- A producer work item scheduled at 100 Hz.
- A single consumer scheduled at 20 Hz, draining up to 20 samples.
- A ring buffer with capacity for 256 samples.
- A counter for samples rejected because the buffer is full.

The buffer uses atomic read/write. It allocates storage once
during creation and performs no further allocation during push/pop.
One extra internal slot distinguishes full from empty.

Timestamps are captured by the application immediately after fetch,
using `k_uptime_get()`. They represent application capture time, not
the precise time the driver generated the reading.

Devicetree takes precedence over the Kconfig rate. Because the binding
defaults `odr-hz` to 100, omitting the property still selects 100 Hz.


## Tests

Run both suites from the repository root:

```sh
west twister -T tests -p native_sim --inline-logs \
  --outdir build/twister \
  --extra-args "Zephyr_DIR=$ZEPHYR_BASE/share/zephyr-package/cmake"
```


## CI

GitHub Actions fetches and caches dependencies, builds the application,
and runs both suites through Twister.

The workflow uploads the application build log, a frozen active
manifest, and Twister results as the `build-and-test-results` artifact.
Build or test failures fail the job.
