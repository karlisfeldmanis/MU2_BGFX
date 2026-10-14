# MU2_BGFX

The game: C++ and bgfx, with its content made by the Cooker. The plan is docs/plans/order-plan.md.

## Making or changing content: work in the Cooker, with the user watching

Read **docs/content/cooker.md** before you make, improve or fix any content: an item, a monster, a world
object, a placement, a gate, a townsperson, the ground, or a map's light. In short:

- The user keeps the studio open (`build/mu2 --studio`), and you work in it through the
  **cooker MCP** tools (`.mcp.json`). Start with `status`, then `show` the thing you are
  working on: a monster at its spawn in its world, with a character fighting it. Then they
  watch each edit, cook and swap. Don't open and close clients and servers to look at your
  work.
- `content/` is the truth. Change it through the MCP's `edit` / `gate` / `light` / `do` (or
  `./mu edit` / `mu gate` / `mu paint`), which rewrite only what changed and journal it.
  Never hand-dump a JSON file.
- `plan` before any `cook`. Ask the user before a cook that changes what the game shows (a
  Blender build, a ground cook, every world's tables, realm and a rebuild).
- `shot` to see what the user sees; read the picture. `journal` for what they changed by hand.

## Always

- Commit straight to main with explicit paths (`git commit -F - -- <paths>`), after
  `git diff <file>`. Other sessions keep uncommitted edits in shared files. Never `git add -A`.
- Push only when asked, and then only this subtree to its own remote, never origin.
- Put back any real source file you changed to test something, through `mu`, and check
  `git status` is as before.
