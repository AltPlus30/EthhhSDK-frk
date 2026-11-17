# Display Reset recovery

The installed log showed a repeatable windowed sequence: a failed `0x0` Reset
(`8876086C`), followed by a successful `1274x691` Reset after a verified
`1280x720` Apply. The `6x29` deficit is consistent with the window frame being
subtracted a second time by the native minimize/restore path.

Compatibility now suspends during failed, minimized or unobservable Resets,
without deleting the verified transaction or its remembered startup plan.
An exact successful swap-chain result resumes compatibility directly.

For a mismatched result, one exact-size re-request is allowed only after an
interruption and only when both dimensions differ by the **currently measured**
window frame, with the same windowed/zero-refresh semantics. The mismatched mode
is never treated as verified. Recovery waits for two seconds of fresh successful
game presents and a foreground, non-minimized game window without the overlay.
It keeps the original Restore baseline rather than starting a new transaction.
Another mismatch after that request fails closed; there is no retry loop.

Unrelated resolutions, fullscreen/windowed switches and refresh changes retain
the existing rejection policy. Manual Apply and Restore cancel queued recovery.
The existing separate, bounded late-startup recovery policy is unchanged.

`tests/EthhhPatchPolicyTests.cpp` covers the recorded sequence, exact resumption,
unrelated modes, measured-frame constraints, cancellation, unavailable parameters,
retry limits and repeated independent minimize cycles. These are portable policy
tests, not an automated live-game minimize test.
