# Working on eidos

This file is for anyone — person or agent — making changes in this repository.
It is not optional. If a tool or workflow conflicts with it, this file wins.

eidos is a native PC port of Pokémon Black built on the
[aletheia](https://github.com/Project-Holon/aletheia) decompilation. It has
real users and release builds. Changes are expected to be small, explained,
tested, and attributable to a person.

## 1. Authorship: terezi only

**Every commit in this repository MUST be authored and committed by:**

```
terezi <jules@foxtails.me>
```

That applies to every commit on every branch, including fixups, merge commits,
reverts, documentation commits, and commits made while testing.

**Never attribute work to an agent, assistant, model, or tool.** This means:

- No `Co-authored-by:` trailer naming an agent, assistant, model, or tool.
- No `Generated with …`, `Made with …`, `Assisted by …`, or similar lines in
  commit messages, pull request titles/bodies, comments, review replies, or
  documentation.
- No agent or tool names in the author or committer fields, and no shared or
  bot accounts.
- Do not mention the agent in commit messages. Describe the change itself.
- Do not add "Signed-off-by" lines with any identity other than terezi's.

Before every push, check the commit you are about to publish:

```bash
git log -1 --format='author:    %an <%ae>%ncommitter: %cn <%ce>%n%n%B'
```

The author and committer must both be `terezi <jules@foxtails.me>`, and the
message must contain no agent or tool attribution. If a commit is wrong,
rewrite it before it reaches the remote:

```bash
git commit --amend --reset-author   # the last commit, or after "edit" in a rebase

git rebase -i <base>                # for older commits: mark each one "edit", then
git commit --amend --reset-author   # fix the author
git rebase --continue
```

Never merge a branch that carries agent attribution. Fix the history first.
Set the identity locally when needed:

```bash
git config user.name terezi
git config user.email jules@foxtails.me
```

Merge pull requests with that same identity. If the forge would record a
different author or committer email on the merge commit (a GitHub `noreply`
address, for example), merge locally with the correct identity or fix the
account's recorded address instead of accepting the forge's identity.

## 2. One PR per piece of work

Every change lands through a pull request. A pull request is one coherent
piece of work, not a bundle of unrelated fixes. If you find two problems,
open two PRs.

Keep each PR:

- **Small.** Aim for the smallest change that fully solves one problem.
  Split refactors, formatting, cleanup, and behaviour changes into separate
  PRs.
- **Self-contained.** It builds and passes tests on its own.
- **On its own branch.** Every PR gets a new, short-lived topic branch. One
  branch, one PR, one purpose. Never reuse a branch for a second PR, put two
  PRs on one branch, or keep working on a branch after its PR is merged.
- **Documented in the same PR.** Update the affected `docs/` page, the README
  table, or `CHANGELOG.md` when behaviour changes. A PR that changes
  behaviour or user-visible output without a docs update is incomplete.
- **Forward-only.** No direct pushes to `main` or `integration`. No force
  pushes to shared branches.

### Write the PR so a stranger understands it

Every pull request description must answer, in plain language:

1. **What changed** — the user-visible or developer-visible effect, in one or
   two sentences.
2. **Why** — the problem, bug, or gap this closes. Link the issue when one
   exists.
3. **How it works** — a short explanation of the approach, and anything a
   reviewer might not expect.
4. **How it was tested** — commands run, platforms/compilers covered, and what
   you observed. Say explicitly what was *not* tested.
5. **Docs** — which docs were updated, or why none were needed.
6. **Risk and rollback** — what could break, and how to revert.

Avoid unexplained jargon, raw logs with no commentary, and one-line
descriptions. Link to the relevant `docs/` page instead of restating it.

### Commit messages

Match the existing style: a one-line, imperative, changelog-style subject
naming the area, then a short body explaining why when it is not obvious.

```
Save slots: New Game never writes over a file, even one the game would not load
```

Rules:

- Subject in the present tense, no trailing period, roughly 72 characters.
- Name the area first (`host:`, `docs:`, `Render:`, `Save slots:`).
- The body explains the reason and any trade-off; it does not narrate the
  tool session.
- No emoji, no tags, no generated-by footers.

## 3. Test the combined result on the integration branch

`main` is the release branch. `integration` is the shared branch where the
combined work is tested. Individual PRs are not enough on their own: changes
that pass separately can fail together.

The flow is:

1. Create a dedicated topic branch off `integration` (or `main` when
   `integration` is being rebuilt) for that one unit of work. Do not share the
   branch with another PR or reuse it after merge.
2. Open the PR for that branch against **`integration`**, not `main`.
3. Keep `integration` current with `main` by merging `main` into it — do not
   rebase shared history.
4. Once the PRs for a batch are merged, build and test the **combined**
   `integration` branch:

   ```bash
   cmake -S . -B build -G Ninja \
       -DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake \
       -DALETHEIA_ROOT=../aletheia -DEIDOS_BOOT=ON -DEIDOS_OVERLAYS=ALL
   cmake --build build --target eidos_boot
   ctest --test-dir build --output-on-failure
   ```

   Run the platform builds and smoke tests that the change affects. Check
   `docs/building.md` for the full matrix and the 64-bit build.
5. Only after the combined branch passes does it move to `main`, through a
   promotion PR from `integration` to `main`. That PR lists the included PRs
   and the test evidence.
6. Delete the topic branch after it is merged.

Never promote work to `main` on the strength of individual PR checks alone.
Never leave `integration` red: fix it or revert the offending PR before adding
more work.

## 4. Before you call it done

- [ ] The change is one coherent piece of work, split out from unrelated work.
- [ ] The PR has its own topic branch, branched from `integration` and not
      reused for anything else.
- [ ] The build passes and `ctest` passes on the combined `integration` branch.
- [ ] Affected docs and `CHANGELOG.md` are updated in the same PR.
- [ ] The PR description explains what, why, how, testing, docs, and risk.
- [ ] Every commit is authored and committed by `terezi <jules@foxtails.me>`.
- [ ] No commit, PR, comment, or doc credits an agent, assistant, model, or
      tool, including as a co-author.
- [ ] The history is clean: no debug leftovers, no unrelated formatting, no
      secrets, no game data from a ROM.
