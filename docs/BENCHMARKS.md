# Benchmarks

Three in-process microbenchmarks are built from `benchmarks/`:
`benchmark_parser`, `benchmark_keepalive` and `benchmark_concurrent`. None of
them opens a socket or starts the server, so they measure the CPU cost of the
parser, the response serializer and the timing harness, not request
throughput through the event loop. `static_small.cpp` and `static_large.cpp`
exist in the tree but are not registered in `CMakeLists.txt`.

- `benchmark_parser` parses a fixed GET and a fixed POST request 10 000 times
after 100 warm-up iterations and prints mean, p50, p95 and p99 parse latency
plus requests per second.
- `benchmark_keepalive` serialises a request and response exchange repeatedly
for one logical connection and prints per-exchange latency and throughput.
- `benchmark_concurrent` repeats that exchange from several threads and prints
aggregate throughput and latency.

Run them from a Release build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/benchmark_parser
./build/benchmark_keepalive
./build/benchmark_concurrent
```

No results are published here. The numbers this file used to carry came from a
Windows Debug run recorded outside the repository and cannot be reproduced
from it, so they were removed rather than carried forward.

Before publishing numbers, record the CPU model and core count, RAM, kernel,
compiler and version, build type and flags, commit SHA, benchmark arguments,
number of runs and the reported metric. Compare only runs from the same
machine and build type.
