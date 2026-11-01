# Check-first agent mint

## Verify admission and quota before a preview

- Check wallet connection, gas balance, paused state, total supply and remaining per wallet quota on chain before you discuss a mint.

- Treat a single successful gateway response as unproven until a second independent read confirms the same admission state.

- Stop at a blocker and report the blocker plainly instead of retrying a busy endpoint until it answers differently.

- Record the exact supply, quota and gas figures you verified so a later step can compare them with the execution result.

## Test the exact function and price

- Simulate the exact function, arguments, value and recipient you intend to execute and require a clean simulation before you ask for confirmation.

- Test alternative function signatures one at a time when the generic function reverts, and keep only the signature that simulates cleanly.

- Verify the exact amount and decimal places in the simulation output against the listing or claim terms before you present a preview.

- Present the preview with the function, value, gas estimate, expected balance change and any approval, and get explicit confirmation for that exact preview.

- Treat a predicted token identifier as a prediction only, because concurrent mints can shift the identifier before execution.

## Confirm market and liquidity before a fee

- Verify the market contract that will actually trade the token and confirm it matches the claim or mint contract before any fee paid claim.

- Check a live liquidity route for the token, including whether a listing exists and whether it can be filled, instead of assuming a claim implies a sale.

- Stop when the market contract differs from the claim contract or when no verifiable market route exists, and say which check failed.

- Test a listing price above a displayed floor when a floor price can be a minimum boundary, and confirm the new price with the user before you list.

## Execute and verify on chain

- Execute only the exact preview the user confirmed, with no added payment, approval, swap, transfer or recipient change.

- Stop without forcing execution when a simulation disagrees with a direct contract read, and label the disagreement as a simulator mismatch rather than a listing change.

- Verify ownership, balance, allowance and total supply on chain after execution, and report the actual token identifier rather than the predicted one.

- Revoke an approval promptly when the user asks to cancel it, and verify the allowance reads zero after the revocation.

## Available connectors

- Use `muse.connector_search` to find other connectors and `muse.skill_search` to find related skills.

- If nothing here fits, look on the web for a maintained API or MCP server and set it up with [Custom Connectors and MCPs](/opt/muse/skills/custom-connector-setup/SKILL.md), or build a custom connector.

## Alternatives for better results

- Skip a mint when the check shows no liquid market, even when the simulation is clean, because a clean simulation proves execution safety rather than resale value.

- Ask the user to top up gas before a window opens when the gas balance is close to the expected cost, instead of starting a mint that will stall midway.

- Keep a bounded autonomous arrangement project specific and time limited, and never extend it to a new project, payment or approval without a fresh explicit instruction.
