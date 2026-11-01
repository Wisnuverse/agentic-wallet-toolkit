# Field note: `eth_call` passing does not mean the transaction can succeed

**Situation.** A "first card free" mint passed every preview and simulation,
then reverted on-chain twice at exactly the same gas usage with a custom
error. Root cause: the free path's Chainlink VRF randomness fee is paid out
of the **NFT contract's own balance** — and that pot held less than the fee.
Simulations did not enforce the pot; the real execution did.

Attempts to attach the fee from the user side reverted with a different
custom error: the free ordinal contractually rejects any attached value.

**Lesson.**
- When a revert carries a custom error at a suspiciously exact gas cost,
  decode the error and check **contract-side balances and state** (fee pots,
  reserves, vaults) — not just your own inputs.
- A green simulation only proves your calldata is well-formed against
  current state. It proves nothing about state the simulator doesn't model.
- The correct operational response to an externally-blocked free mint is a
  **watcher**: poll the blocking condition (here, the contract's balance)
  and fire the moment it clears, with all calldata rebuilt fresh at fire
  time. Deadlines and quotes must never be pre-built and reused.
