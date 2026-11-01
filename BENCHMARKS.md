# Benchmarks

Measured during live operations (Oct–Nov 2026) on a modest cloud VM.
Keccak-256 throughput is the only number that matters for timed challenge
windows: required work grows as 2^bits, so a 2× faster solver turns an
impossible tier into a comfortable one.

| Solver | Workload | Throughput |
| --- | --- | --- |
| `solvers/tailsolve.c` | hash-tail search (leading zero bits, decimal nonce) | ~0.81–1.10M H/s (multithreaded) |
| `solvers/pixmine.c` | dig-style proof of work (leading zero bits) | ~879k H/s |

## Rules these numbers bought us

- **Never enter a timed challenge with an unproven solver.** The solver must
  first solve representative offline data of the same tier/size inside the
  time limit, with margin. Real challenge data from failed attempts is kept
  and used as benchmark input.
- **Expected time ≈ 2^bits / hashrate.** At 34 bits and ~1M H/s the mean is
  ~4.8 hours of pure search for a single thread-equivalent — know this
  *before* the window opens, not during it.
- All slow work (contract verification, solver benchmarking, session
  warm-up) happens **before** the opportunity window. Inside the window,
  execution should take seconds.

Reproduce the tail-solver benchmark:

```sh
gcc -O3 -march=native -pthread -o tailsolve solvers/tailsolve.c
./tailsolve --selftest
```
