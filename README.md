# RedShow

RedShow is the redundancy-analysis engine of [RedSan](https://github.com/FlagZhao/redsan) (this is the `redundant` branch of the [GVProf](https://github.com/Jokeren/GVProf) trace-analysis library). It consumes the memory-access trace produced by [`gputrigger`](../gputrigger)/[`gpu-patch`](../gpu-patch) and computes redundancy statistics, optionally attributed to calling context (CCT).

This branch builds two analyses, in `src/analysis/`:

- `memory_access` — raw per-kernel memory access statistics
- `redundant_write` — redundant memory write detection

## Compile

Edit `Makefile.config` (or pass the same variables on the `make` command line). `GPU_PATCH_DIR` and `BOOST_DIR` are mandatory:

```bash
make GPU_PATCH_DIR=<install_path>/gpu-patch BOOST_DIR=<path/to/boost> -j
```

Other optional variables:

| Variable | Effect |
|---|---|
| `DEBUG` | Build with `-g -DDEBUG` instead of `-g -O3` |
| `STATIC_CPP` | Link `libstdc++` statically (`-static-libstdc++`) |
| `OPENMP` | Enable OpenMP (`-DOPENMP -fopenmp`) |
| `AVX` | Override `-march=native` with a specific `-m<AVX>` flag |
| `PREFIX` | Also install headers/lib/bin under `$(PREFIX)` |

`make` (using `Makefile`) builds `lib/libredshow.so`. `make -f Makefile.static` builds a static `lib/libredshow.a` instead — this is the variant `gputrigger` links against, and what the top-level `bin/install_release.sh` uses:

```bash
make PREFIX=<install_path>/redshow BOOST_DIR=<path/to/boost> GPU_PATCH_DIR=<install_path>/gpu-patch STATIC_CPP=1 install -f Makefile.static
```
