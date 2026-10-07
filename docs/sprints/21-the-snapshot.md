# The snapshot

Begun 2026-10-08. Worlds on the server now live for ever (55681604, the user: "real fix is
snapshot"). A newcomer, though, raised his mirror from the world's seed and replayed every tick
since the raise. That costs about 72,000 ticks an hour, all of them held in the server's memory
and all sent on each join. A snapshot of the realm replaces that past.

## 1. The realm as bytes — done 2026-10-08

- **`Body` and `Player` are plain data.** Nothing else is in them, so a snapshot copies them
  whole and a field added later is carried without anyone listing it:
  - a body's route moved out to `Realm::routes_` (`route(body)`, `routeOf` for a reader), by the
    body's index;
  - the buy-back list is a fixed `Sales` of five;
  - the refusal words, for the log alone, are the realm's (`refusal_`).

  sim_test's output is identical to before, line for line.
- **`Realm::snapshot` / `restoreSnapshot`** (`sim/realm_snapshot.cpp`): one field list,
  `serialize()`, walked by a writer and a reader.
  - Plain data goes whole and containers by count.
  - The bytes are headed by the sizes of the main structs, and another layout is refused.
  - A townsperson's row goes by its index. A constexpr table in a header is each file's own copy,
    so its address means nothing in another file.
  - A grid the castle changed goes whole.
  - The raid's party is not carried: a raid realm says no. It is a local harness and never the
    server's.
  - Lorencia, two players, 303 bodies: 504 KB.
- **`testSnapshot`:** two knights hunt for 1500 ticks. A mirror is raised as a client raises one,
  laid with the snapshot, and says the same bytes back. Both then step 1500 more ticks on the
  same commands, and every tick's hash and happenings are alike, 8 deaths among them. A snapshot
  cut short is refused.

## Next

- **On the wire:** the server snapshots each world every minute and keeps only the ticks since.
  A Welcome sends the latest snapshot and those ticks.
