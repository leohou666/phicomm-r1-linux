# Repository instructions

## Start here

- Before working on this project, read `docs/index.md` and then the documents relevant to the task.
- Use `docs/reverse-engineering-journal.md` as the chronological learning and evidence log. Do not turn it into a polished design document.
- Treat `backup/` and locally extracted proprietary firmware as working evidence, not source files intended for publication.

## Documentation at stage boundaries

A stage is complete when it has a concrete artifact or a verified conclusion, for example a captured boot log, a readable backup, an extracted image, a decoded DTB, a booting kernel, or a working peripheral.

Whenever a stage is completed, update the documentation in the same change:

1. Append to `docs/reverse-engineering-journal.md`: what was attempted, exact reproducible commands, important observations, failures, conclusions, and lessons learned.
2. Update `docs/index.md`: current status, newly completed stage, immediate next step, and links to new artifacts or documents.
3. Update the corresponding checkboxes in `TODO.md`.
4. Update the relevant topic document when established facts, configuration, architecture, or procedures have changed.

Do not mark a stage complete merely because a command ran. Record how its result was verified and keep unresolved points visible.

## Evidence and writing rules

- Clearly distinguish verified facts, reasoned inferences, and open questions.
- Prefer evidence from the device, original images, boot logs, DTB, and reproducible tool output over generic RK322x assumptions.
- Preserve the provenance of every externally sourced technical claim in the same document where the claim is recorded. Link to the most specific available primary source: the exact forum post/comment rather than a thread index, the exact commit/blob rather than only a repository or moving branch, and the original datasheet or maintainer documentation rather than a search result or unsourced summary.
- Record enough source identity to recover and evaluate it later: author or maintainer, publication date when available, page/post/commit title or identifier, and the direct URL. When a branch, patch, binary, or build artifact is evidence, also record its commit ID and checksum when available.
- Keep source statements separate from project interpretation. State explicitly whether a conclusion is verified on the R1, inferred from R1 evidence, or only supported by evidence from another RK322x device.
- If the original source cannot be reopened or independently verified, retain the URL but mark the citation and the dependent claim as unverified; do not silently promote it to an established fact.
- Record commands in a form another person can reproduce, but omit secrets, device keys, credentials, and real MAC addresses.
- Keep documentation in plain Markdown. Do not add Mermaid diagrams unless the user explicitly requests them.
- Use repository-relative links and keep `docs/index.md` usable as the single documentation entry point.
- Preserve the user's existing edits and do not rewrite unrelated material.

## Device safety

- Prefer read-only inspection and RAM-only experiments until a recovery path has been tested.
- Before any device write, verify the selected USB device, storage target, offset, length, source image, current backup, and recovery procedure.
- Do not run destructive Rockchip operations such as `UL`, `WL`, `EF`, `GPT`, or `PRM` without explicit user authorization for that exact operation and target.
- Never commit `backup/`, full eMMC images, partition dumps, device-unique data, or proprietary firmware binaries to a public repository.

## Verification

- After documentation changes, check links, code fences, filenames, commands, and stated status against the workspace artifacts.
- After code or configuration changes, run the smallest relevant verification first and document any check that could not be performed.
- When a stage changes the known hardware map, propagate the result to the relevant bring-up, architecture, audio, or reverse-engineering document.
