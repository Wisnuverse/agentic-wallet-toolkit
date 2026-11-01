# Agentic Wallet Toolkit

Tools built and used in production for **Binance Agentic Wallet / Agent ID**
workflows on BNB Smart Chain: pre-mint on-chain verification, Keccak-256
proof-of-work solvers, and EIP-712 signature-recovery checks.

Everything here was written to solve a real problem during live mint windows,
where a wrong call costs real money: a mispriced listing, a honeypot contract,
or a burned one-shot challenge. The guiding rule is simple — **verify on-chain
first, sign second**.

## Contents

### `skills/agent-mint-check-first/`
A checklist-style skill for evaluating any agent mint *before* signing:
contract verification, supply/quota/paused state, market & liquidity checks
(including permissionless indexers such as Element Market), gas-vs-reward
sanity, and exact-amount/decimal discipline. A clean simulation is necessary
but never sufficient — this skill encodes the failures that taught us that.

### `solvers/tailsolve.c`
Multithreaded (pthread) Keccak-256 **hash-tail solver**: finds a decimal nonce
such that `keccak256(prefix + nonce)` starts with at least *N* zero bits.
~0.8–1.1M hashes/sec on modest hardware. Built for timed, one-shot challenge
windows where a JavaScript solver is too slow — the solver is always proven
offline against representative data *before* a live attempt.

```sh
gcc -O3 -march=native -pthread -o tailsolve solvers/tailsolve.c
./tailsolve "attempt-id:0:your answer:" 34
./tailsolve --selftest
```

### `solvers/pixmine.c`
Keccak-256 leading-zero **proof-of-work dig miner**, adapted from
`tailsolve.c` for 24-hour dig-style free mints (~879k H/s in our benchmarks).

### `scripts/verify-recovery-byte.mjs`
EIP-712 **signature recovery-byte checker**. Some wallet signing flows return
a 64-byte signature plus a separate recovery flag; appending the wrong byte
(`1b` vs `1c`) produces a signature that fails verification everywhere. This
script tries both, recovers the signer for each, and tells you which byte
matches the expected wallet — locally, before you waste a login attempt.

```sh
node scripts/verify-recovery-byte.mjs typed-data.json <64-byte-signature-hex> <expected-wallet>
```

Requires [`viem`](https://viem.sh): `npm install viem`.

## Benchmarks

Throughput numbers and the preparation rules they bought us:
[BENCHMARKS.md](BENCHMARKS.md). The C solvers are built and self-tested by
CI on every push.

## Field notes

Short post-mortems from live operations — the failures that produced the
rules in the check-first skill:

- [“No market on the project site” ≠ “no market”](docs/field-notes/element-market-lesson.md)
- [Report what landed, never what the simulation promised](docs/field-notes/decaying-pool-lesson.md)
- [`eth_call` passing does not mean the transaction can succeed](docs/field-notes/vrf-pot-lesson.md)

## Safety notes

- No private keys, seed phrases, session tokens, or API keys are stored in
  this repository — every tool here works with public data, unsigned payloads,
  or signatures produced by an external wallet.
- Always read a contract's decimals from the contract itself before
  formatting any amount, and cross-check any listing price against floor data
  before signing.

## License

MIT
