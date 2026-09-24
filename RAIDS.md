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
