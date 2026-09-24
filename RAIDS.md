# Raid resistance

A successful raid increases the target's resistance, regardless of who
attacked. Each level halves the loot left after the existing defenses: a raid
that would take 1,000 palle instead takes 1,000, then 500, then 250, then 125
on closely spaced attacks. Resistance caps at three levels; it does not block
attacks. One level recovers every two hours, even if the target is raided again
in the meantime.

A balloon that turns a raid back does not increase resistance. Neither does a
raid that takes no palle, including one stopped completely by the purchased
shield. Resistance remains effective when the owner is away; balloon and
purchased shield behavior is unchanged. Raid announcements report the amount
stolen but do not attribute reductions to resistance or the purchased shield.
Zodiac modifiers still affect the loot calculation but are not shown in raid
announcements.

Resistance belongs to the stable internal player key and is saved under
`raid_resistance_levels` and `raid_resistance_since` in `conquister.json`.
Older saves without these sections start with no resistance. The first raid
after the recovery interval computes the current level from the saved recovery
checkpoint.
