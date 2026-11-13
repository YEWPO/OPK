# Repository agent rules

[简体中文](AGENTS.zh-CN.md)

These rules apply to the entire repository. Every modification task, including documentation changes, is complete only after all remote GitHub Actions for its final commit have passed.

## Changes, commits and pushes

1. Inspect the worktree before editing and preserve the user's existing changes. Commit only files or change fragments belonging to the current task.
2. Run relevant local checks and review the diff after editing. Passing local checks does not replace remote verification.
3. Automatically commit this task's changes and push the current working branch without asking each time. Never force-push, overwrite or discard user changes. If a push is rejected, inspect the remote branch and resolve it safely before continuing.

## Remote Actions completion gate

1. Query remote Actions using the final commit's complete SHA and confirm the expected workflows were triggered. Success for an older commit is not evidence.
2. Wait for all Actions runs for that commit to finish successfully. Both `linux` and `windows` jobs in the existing `Build and verify` workflow must pass, including packaging, installation and uninstallation verification triggered by that run.
3. Queued, running, failed, cancelled, timed out, awaiting approval and untriggered checks do not count as passing. Steps legitimately skipped by workflow conditions are acceptable; skipped expected workflows or required jobs are not.
4. On failure, read the failing job's logs, fix the problem, rerun relevant local checks, commit and push. Repeat until all remote Actions for the final commit succeed. Every new commit requires fresh verification.
5. Never disable workflows, delete checks, skip required tests, ignore failures or lower verification standards to obtain a passing result.
6. If permissions, networking, external services or a required user decision block verification, report the reason and current status explicitly. Do not claim completion or successful remote verification.

## Final response

Explain the changes and local validation results. Include the final commit SHA, corresponding Actions run links and remote verification results. Report completion only after the gate above is satisfied.
