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

## 2. On the wire — done 2026-10-08

- **Protocol 6:**
  - The Welcome carries `snapshot`, the world as it last stood (empty before the first), and
    `backlog` is the ticks after it.
  - A client accepts frames up to 16 MB (`kMostFrame`). The server reads a client's frames at
    64 KB at most (`kMostAsked`), so a line cannot make it hold megabytes.
- **The server** snapshots each world every minute (`kSnapshotEvery`) after the tick, and lets go
  of the past before it. A world holds at most a minute of ticks and one snapshot, however long
  it lives. A raid realm, which says no to a snapshot, keeps its whole past as before.
- **The client:** `RemoteLink::catchUp` lays the mirror with the snapshot over what it raised
  from the world's start, then steps only the ticks after it. The figures are made after that, so
  every body the snapshot holds is drawn, other players among them.
- **Verified on loopback:**
  - a knight played a fresh Lorencia for 92 s;
  - at 72 s an elf joined, welcomed with "a 482 KB snapshot and 170 ticks after it", and his
    mirror was laid from the snapshot at tick 1200;
  - 24 of 24 of the elf's hashes agreed, and the knight's 89 of 89 throughout;
  - sim_test 6668 of 6668.
- **On the box** (deployed on the user's "deploy"; sim_test 6668 of 6668 there):
  - a knight held Noria (1005 monsters) for 92 s;
  - at 70 s an elf joined from this Mac, from "a 1585 KB snapshot and 197 ticks after it", taken
    by the Linux server and laid on the Mac's mirror;
  - 24 of 24 and 91 of 91 hashes agreed. The snapshot's plain data reads the same on both machines.
  - GCC's one warning (a castle's rune loop it could not bound) is quieted by an explicit
    `std::min`, and will go out with the next deploy.
- **The cost:** a busy map's snapshot is its size, 1.6 MB for Noria's thousand monsters, sent once
  per join. Squeezing it (most of a Body is zero) is cheap if joins on slow lines need it.
