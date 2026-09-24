# Raid resistance

## Temporary Conquister balloon

A successful entry into @TheConquister37 grants a free balloon unless the
player has a boost for that hold. It protects only that hold: it can repel at
most three claim attempts, and the fourth attempt always pops it. Leaving the
place voluntarily or being displaced removes it. It never protects against a
raid at home. A purchased home shield is kept when the temporary balloon is
granted, but the shield works only while the owner is home. Old balloons saved
for players outside @TheConquister37 are removed when this version first loads
the game state. The old `/buyballoon` command remains as an informational
deprecation reply; it no longer buys anything.

## Resistance

A successful raid increases the target's resistance, regardless of who
attacked. Each level halves the loot left after the existing defenses: a raid
that would take 1,000 palle instead takes 1,000, then 500, then 250, then 125
on closely spaced attacks. Resistance caps at three levels; it does not block
attacks. One level recovers every two hours, even if the target is raided again
in the meantime.

A raid that takes no palle, including one stopped completely by the purchased
shield, does not increase resistance. Resistance remains effective when the
owner is away; a balloon protects only the holder of @TheConquister37 and never
blocks a raid. Raid announcements report the amount
stolen but do not attribute reductions to resistance or the purchased shield.
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
he is at home: nothing is stolen, no shield or resistance applies, and the
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
