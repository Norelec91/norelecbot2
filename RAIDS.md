# Raid resistance

## Balloon

Every player always has a balloon, unless he owns a boost, which rules it out
until the boost is spent. It guards wherever he is: @TheConquister37 while he
holds it, his home while he is there. On the road it guards nothing, neither
the place nor the house he left.

Claims against the holder and raids against a player at home wear the same
balloon: the first attempt has one chance in four of popping it, the second two,
the third three, and the fourth always does. A claim it holds off costs the
attacker a penalty and palle; a raid it holds off takes nothing, raises no
resistance, and the raider rides home empty-handed. When it pops, the claim or
the raid goes through as usual, and right after it a fresh balloon takes its
place. Being kicked out of @TheConquister37 always leaves the kicked player with
a fresh one. Entering the place or going home does not renew it: he carries the
same balloon, as worn as it is.

`conquister.json` keeps, under `balloons`, how many attempts each balloon has
survived; a fresh balloon has no entry. The old `/buyballoon` command remains as
an informational deprecation reply; it no longer buys anything.

## Retired raid shield

The raid shield bought with `/buyshield` no longer exists. `/buyshield`
remains as an informational deprecation reply and buys nothing. Shields saved
by older versions are removed from `conquister.json` when this version first
loads the game state, without refunding the palle they cost. Against raids
there remain the balloon, while its owner is home, and resistance.

## Resistance

A successful raid increases the target's resistance, regardless of who
attacked. Each level halves the loot: a raid
that would take 1,000 palle instead takes 1,000, then 500, then 250, then 125
on closely spaced attacks. Resistance caps at three levels; it does not block
attacks. One level recovers every two hours, even if the target is raided again
in the meantime.

A raid that takes no palle does not increase resistance. Resistance remains effective when the
owner is away, unlike the balloon, which guards only the place where he is. A
raid the balloon holds off never reaches resistance. Raid announcements report the amount
stolen but do not attribute reductions to resistance.
Zodiac modifiers still affect the loot calculation but are not shown in raid
announcements.

Resistance belongs to the stable internal player key and is saved under
`raid_resistance_levels` and `raid_resistance_since` in `conquister.json`.
Older saves without these sections start with no resistance. The first raid
after the recovery interval computes the current level from the saved recovery
checkpoint.

## Palle carried to another player

`We @someone N` (or `We ircnick N` on IRC) sets off on the same journey as a
raid, with `N` palle in the sack instead of empty hands. The amount must be
positive and no larger than the available score, and it leaves the sender the
moment he sets off, so nothing on the road can be taken from it. The traveller
is away from home for the whole ride, exactly as in a raid.

On arrival the palle are handed to the target, who receives them whether or not
he is at home: nothing is stolen, no resistance applies, and the
target's resistance does not rise. The traveller then rides home empty-handed.
Naming himself during the ride turns him around, and the undelivered palle come
back to him when he reaches home. Palle in transit are kept in the `gift` field
of the `raids` section of `conquister.json`.

## Palle brought back to the place

`We @TheConquister37 N` gives `N` palle back to the place they were earned in,
which takes them out of the game: nobody receives them, no journey is made, and
they are gone the moment the line is written. The amount must be positive and no
larger than the available score, and the place is recognised with or without the
mention. It is the only way palle leave the game, so it is also the only brake
on the wealth the prices follow.
