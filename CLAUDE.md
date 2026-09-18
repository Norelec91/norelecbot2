# NorelecBot

C++23 with the STL, no patched libraries. Run `tools/check.sh` in the podman image from
`tools/Containerfile` before every commit, and deploy with `deploy/deploy.sh`. The rest of the
deployment notes are in `DEPLOY.md`, which is not tracked.

## The mishaps

`src/mishaps.hpp` holds the small things that happen to a player for no reason, and the pinball targets
a message can hit.

**Every change to the code rewrites the whole table, and each set is more absurd than the one before
it.** Half of them are nuisances worth a palla or two; the rest hand out something absurd — a balloon
arriving in the post, a triple on the next hold, a nick teleported elsewhere on the map, a penalty
quashed on a technicality, a point of simpatia won or lost. The cast of `Boon` says what a mishap can
leave behind; a new kind of gift means a new one there and in `mishap_strike`.

Nothing in the table is ever aimed at a particular player, and none of it moves more than a handful of
palle: the absurdity is in what happens, not in what it costs.
