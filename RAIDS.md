# Raids and their defenses

## Where each line works

At home every line works. From @TheConquister37, the lines naming the sender
(withdraw, deposit, buying or moving furniture) take him home first, paying
what the hold earned, and then do their job; leaving is instantaneous, since the
place is not on the map. Burning palle or emoji happens there too, while raids
and deliveries cannot set off from there. On the road
the only line that works is `We @yourname`, which turns him around; every other
one is refused with a reminder of that line.

## Balloon

Every player always has a balloon. It guards wherever he is: @TheConquister37 while he
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
survived; a fresh balloon has no entry. The old `/buyballoon` command is gone.

## Retired raid shield

The raid shield bought with `/buyshield` no longer exists, and neither does
the command. Shields saved
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
`We @yourname 1 2` moves the emoji in slot 1 to slot 2, swapping it with
whatever hangs there; it is free, works only at home and only on the sender's
own name. The old `/buyfurniture` command is gone.

## Lightning

Every ⚡ hanging on a player's name as he enters @TheConquister37 adds
`NORELECBOT_LIGHTNING_PERCENT` (50 by default) to that hold: one makes it worth
x1.5, two x2, three x2.5, ten x6. The bonus is fixed on the way in and saved with
the holder as `lightning_percent`, so a ⚡ hung, received or burnt during the hold
changes nothing until the next entry, and the balloon stays. Every ⚡ adds the
same, while its price doubles with every copy in the game, so the first ones pay
back quickly and the later ones hardly ever. A hold saved by an older version
with a whole `multiplier` keeps it: x3 becomes 300%. The old `/buyboost` command
is gone, and boosts bought with older versions are removed from
`conquister.json` on the first load, without a refund.

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
