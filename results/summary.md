# Benchmark results

Medians of 5 runs. Spread is how far the furthest run strayed from the median. The machine and build settings are in [env.txt](env.txt).

| Workload | Keys | M ops/s | Spread | p50 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| get_hit | 10,000 | 22.16 | ±13.6% | 90 ns | 181 ns |
| get_hit | 100,000 | 11.15 | ±14.8% | 164 ns | 606 ns |
| get_hit | 1,000,000 | 3.66 | ±9.1% | 559 ns | 1.4 µs |
| get_miss | 10,000 | 21.49 | ±6.1% | 76 ns | 159 ns |
| get_miss | 100,000 | 19.36 | ±23.0% | 89 ns | 457 ns |
| get_miss | 1,000,000 | 7.02 | ±25.2% | 292 ns | 1.2 µs |
| insert | 10,000 | 9.78 | ±7.7% | 108 ns | 241 ns |
| insert | 100,000 | 6.44 | ±4.0% | 128 ns | 344 ns |
| insert | 1,000,000 | 3.08 | ±9.4% | 259 ns | 954 ns |
| mixed | 10,000 | 20.49 | ±22.1% | 102 ns | 216 ns |
| mixed | 100,000 | 9.65 | ±15.3% | 207 ns | 793 ns |
| mixed | 1,000,000 | 2.90 | ±5.5% | 677 ns | 1.6 µs |

Load-factor sweep at 1M keys:

| Max load | Actual load | Bucket array | get_hit M ops/s | get_miss M ops/s |
| ---: | ---: | ---: | ---: | ---: |
| 0.25 | 0.24 | 32 MiB | 3.56 | 7.56 |
| 0.5 | 0.48 | 16 MiB | 3.52 | 7.70 |
| 1 | 0.95 | 8 MiB | 3.47 | 5.23 |
| 2 | 1.91 | 4 MiB | 3.17 | 3.28 |
| 4 | 3.81 | 2 MiB | 2.15 | 1.69 |

Insert timeline: the slowest insert took 25.1 ms (in the window from insert 786,000); the slowest insert in a typical window of 1,000 took 1.3 µs.
