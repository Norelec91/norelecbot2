# Investments

`We @YourTelegramUsername N` deposits `N` palle when the sender is home. On IRC,
the equivalent is `We YourIrcNick N`. The target must resolve to
the sender's own stable player key on the selected platform; any other name
makes the same line a delivery instead, described in `RAIDS.md`. The amount must be
positive and cannot exceed the available score. Further deposits are allowed;
each keeps its own start time.

Deposited palle leave the score immediately. They cannot be stolen or spent on
items or quotes while invested. Interest continues to accrue even when the
player later travels or occupies @TheConquister37, but deposits and withdrawals
are possible only while the player is back home. Written from @TheConquister37,
both lines take the holder home first, paying what the hold earned, and then do
their job there. A deposit with a non-positive amount is refused before he
leaves; if the palle are not enough even after the hold is paid, he is home
anyway and nothing is deposited. A withdrawal takes him home only when he has
something invested: otherwise `We @YourTelegramUsername` is just the way home.
On the road neither works: the reply reminds him how to turn back, or tells him
when he is home if he is already on his way back.

`We @YourTelegramUsername` (or `We YourIrcNick` on IRC) withdraws the entire
investment if the sender is home. Without an investment, the existing `We`
behavior applies, including turning around during travel and leaving
@TheConquister37. Each local calendar day draws one integer magnitude uniformly
from 0% through 100%, shared by every player. The owner's zodiac determines
only its sign: favorable (125%) earns the magnitude, neutral (100%) earns 0%,
and unfavorable (75%) loses the magnitude. Thus the daily rate ranges from
-100% to +100%. Within each local calendar day, the change is prorated linearly
by elapsed seconds over that day's actual length. The remaining balance carries
into the next day, so daily returns compound. A full day at -100% wipes out
that deposit permanently; a full day at +100% doubles it. A complete local day
always applies the full rate, including on daylight-saving days. The rate
changes at local midnight and is saved when first needed, so a restart cannot
reroll it.
The sum is rounded down to whole palle only on withdrawal. A later deposit has
its own start time and does not receive returns for time before it was made.

Deposits are persisted in the `investments` array of `conquister.json` by stable
player key, amount and start timestamp. Daily magnitudes are persisted in
`investment_magnitudes`, keyed by local day start. Deposits created before
zodiac returns retain their accrued 3% daily compound return up to the first
startup of the new version; from that point onward they use the daily zodiac
return. The changeover time is recorded per deposit as `fixed_until`, without
rounding or re-pricing past returns. If a withdrawal would exceed the game's
signed 64-bit score limit, it is refused without changing the investment; an
administrator must resolve the oversized balance.
