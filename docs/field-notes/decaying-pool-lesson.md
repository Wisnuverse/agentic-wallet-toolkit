# Field note: Report what landed, never what the simulation promised

**Situation.** A free token claim simulated a payout of ~2.9M tokens.
The executed claim landed exactly **one third** of that: the reward pool
decays as claims come in, and the simulation read a state that was already
gone by the time the transaction confirmed.

**Lesson.**
- A simulation is a photograph, not a promise. For any claim, mint, or
  swap, the reportable number is the **post-transaction on-chain balance
  delta**, re-read after confirmation, with the transaction hash.
- If a preview and the landed amount disagree, the landed amount is the
  story — including when it makes the result look worse.
- This applies doubly to anything described to the user as "income":
  an entry, a claim, or a token with no real market price is a *position
  secured*, not a harvest.
