# Player identity

Game commands no longer use a public name as the player's key. On Telegram, the
account key is the numeric ID supplied by Telegram. On IRC, it is the NickServ
account reported by WHOIS `330`, or the identified nick when the server provides
only `307`. A change of display name does not move scores, planets, items, or
raids. Matching names on the two networks do **not** link accounts automatically.

`/link <IRC nick>` on Telegram and `!link @TelegramUsername` on IRC link two
accounts only after reciprocal requests from both authenticated accounts. Each
account must first use a game command. Automatic linking is possible only when
at most one profile already has assets or activity. If both do, the bot moves
nothing: merging scores, mutually exclusive items, planets, and active trips
requires a manual decision.

## Existing saves

The existing JSON format remains readable. On an account's first action, assets
previously keyed by name are rebound automatically **only** when the save
contains evidence: a matching Telegram ID in `telegram_ids`, the current
holder's `user_id`, or a previously seen IRC nick without a Telegram namesake.
A name recorded on both platforms remains unclaimed because the ownership of
assets already combined under that key cannot be determined. Entries without
ownership evidence also remain intact but unclaimed; a matching public name
alone is not proof of ownership.

Back up the live save before deploying this version. To reassign an unclaimed
entry, stop the bot and verify ownership with the player. Do not award it to the
first account that uses the same name. Deployment does not modify the remote
save on its own.
