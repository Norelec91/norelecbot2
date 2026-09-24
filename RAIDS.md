# Raids and their defenses

## Balloon

Every player always has a balloon, unless he owns a boost, which rules it out
until the boost is spent. It guards wherever he is: @TheConquister37 while he
holds it, his home while he is there. On the road it guards nothing, neither
the place nor the house he left.

Claims against the holder and raids against a player at home wear the same
balloon: the first attempt has one chance in four of popping it, the second two,
the third three, and the fourth always does. A claim it holds off costs the
attacker a penalty and palle; a raid it holds off takes nothing, and the raider
rides home empty-handed. When it pops, the claim or
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
loads the game state, without refunding the palle they cost.

## Retired raid resistance

Raids no longer build resistance: every raid that gets past the balloon takes
the loot the road allows, however many came before it. The
`raid_resistance_levels` and `raid_resistance_since` sections saved by older
versions are removed from `conquister.json` on the first load. The balloon,
while its owner is home, is the only defense left against raids.

## Palle carried to another player

`We @someone N` (or `We ircnick N` on IRC) sets off on the same journey as a
raid, with `N` palle in the sack instead of empty hands. The amount must be
positive and no larger than the available score, and it leaves the sender the
moment he sets off, so nothing on the road can be taken from it. The traveller
is away from home for the whole ride, exactly as in a raid.

On arrival the palle are handed to the target, who receives them whether or not
he is at home: nothing is stolen and his balloon is not touched. The traveller
then rides home empty-handed.
Naming himself during the ride turns him around, and the undelivered palle come
back to him when he reaches home. Palle in transit are kept in the `gift` field
of the `raids` section of `conquister.json`.

## Furniture

`We @yourname 🍕` (or `We yournick 🍕` on IRC) hangs one emoji on the
sender's own name, in the first empty slot; `We @yourname 🍕 3` puts it in slot
3, overwriting what hung there. Names have `NORELECBOT_FURNITURE_LIMIT` slots
(10 by default), and empty slots between emoji show as `[]`. It works only at
home, not on the road nor from @TheConquister37. The price is
`NORELECBOT_FURNITURE_COST`, following the group's wealth like every price,
doubled for every copy of that emoji already hanging from anybody's name.
`/buyfurniture` remains as a deprecation reply that shows the new line with
the sender's own name.

## Emoji carried to another player

`We @someone 🍕` (or `We ircnick 🍕` on IRC) makes the same journey with one
emoji of the sender's own instead of palle. The first slot holding that emoji
is emptied as he sets off, leaving a hole. He must have that emoji, the target
must be somebody else, and the target's name must have an empty slot; all three
are checked before he leaves. On arrival the emoji goes into the target's first
empty slot. If the target's name filled up on the way, the emoji rides home
with the traveller, as it does when he turns around, and goes back into his own
first empty slot. It always finds one: furniture is bought only at home, so the
traveller cannot fill the slot it left, and a gift brought to him while it
travels needs two empty slots on his name, so that one is always left for it. Carrying
an emoji does not change how many copies of it hang in the game, so it does not
change its price. An emoji in transit is kept in the `gift_emoji` field of the
`raids` section.

## Palle brought back to the place

`We @TheConquister37 N` gives `N` palle back to the place they were earned in,
which takes them out of the game: nobody receives them, no journey is made, and
they are gone the moment the line is written. The amount must be positive and no
larger than the available score, and the place is recognised with or without the
mention. It is the only way palle leave the game, so it is also the only brake
on the wealth the prices follow.

`We @TheConquister37 🍕` does the same with an emoji: the first slot holding it
is emptied on the spot and the emoji leaves the game, which lowers the price of
the copies still hanging.
