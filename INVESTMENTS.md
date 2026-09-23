# Planet investments

`We @YourTelegramUsername N` deposits `N` palle when the sender is on their own
planet. On IRC, the equivalent is `We YourIrcNick N`. The target must resolve to
the sender's own stable player key on the selected platform. The amount must be
positive and cannot exceed the available score. Further deposits are allowed;
each keeps its own start time.

Deposited palle leave the score immediately. They cannot be stolen or spent on
items or quotes while invested. Interest continues to accrue even when the
player later travels or occupies @TheConquister37, but deposits and withdrawals
are possible only while the player is back on their own planet.

`We @YourTelegramUsername` (or `We YourIrcNick` on IRC) withdraws the entire
investment if the sender is home. Without an investment, the existing `We`
behavior applies, including turning around during travel and leaving
@TheConquister37. The interest rate is an effective 3% per day, compounded with
one-second time resolution: each deposit has value
`amount * 1.03^(elapsed_seconds / 86400)`. The sum is rounded down to whole
palle only on withdrawal, so additional deposits do not discard fractional
interest. Time before a deposit never earns interest on that deposit.

Deposits are persisted in the `investments` array of `conquister.json` by stable
player key, amount and start timestamp. Older saves without the array load with
no investments. If a withdrawal would exceed the game's signed 64-bit score
limit, it is refused without changing the investment; an administrator must
resolve the oversized balance.
