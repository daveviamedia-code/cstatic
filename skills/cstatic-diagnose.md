---
name: cstatic-diagnose
description: Diagnose and fix a broken C-Static build or broken links. Use when the user says "build fails", "broken link", "page not generating", "template error", "frontmatter error", "debug build", or something is not appearing in output. Covers reading the error summary, cstatic check, and when to use --full.
---

# Skill: Diagnose a C-Static build

C-Static collects all errors instead of aborting on the first, and prints a numbered summary to stderr. Read that summary first.

## Steps

1. **Run a clean build and read the summary:**

   ```bash
   cstatic build --full -v
   ```

   - `--full` ignores the hash cache (rules out stale-cache false negatives — see gotchas).
   - `-v` prints per-phase timing (parse / render / data pages / assets / modules) to stderr, helpful to see where time/failures concentrate.
   - Errors print as a numbered list. Template/frontmatter errors with a known line show ±3 lines of source context with a `>` marker and a `^` caret at the column.

2. **Map common errors to fixes:**

   | Symptom | Likely cause | Fix |
   |---------|--------------|-----|
   | `template 'X' ... not found` | Missing `templates/X.html` | Create the template, or fix the `layout`/collection `template` name. |
   | `Variable 'Y' not found for json data` | Template references a missing variable | Inja renders missing vars as empty only for `{{ }}`; this error means a required lookup failed. Add the variable or guard with `{% if %}`. |
   | Frontmatter error with line/caret | Malformed YAML in frontmatter | Fix indentation/quotes on the indicated line. Dates must be `YYYY-MM-DD` (quote if needed). |
   | Page silently absent from output | `draft: true`, future `date`, or missing from `src/` | Build with `--drafts`; check `date`; confirm file is under `src/`. |
   | "N scheduled" in summary | Future-dated pages skipped | Intended unless you want them live: set `build.publish_future = true`. |
   | Asset not found / 404 in browser | Fingerprinting mismatch or wrong path | Use `{{ asset("path") }}`; run `--full` after enabling `fingerprint_assets`. |

3. **Verify links after a successful build:**

   ```bash
   cstatic check                 # internal links only
   cstatic check --external      # also probe external URLs via HTTP HEAD
   cstatic check --timeout 3000  # per-external-request timeout in ms
   ```

   - Exits `1` on issues — usable as a CI gate.
   - Internal links resolve root-relative paths against `output/`; trailing `/` or extensionless paths resolve to `index.html`.
   - External transport failures (DNS, timeout, refused) are **warnings**, not failures; only HTTP `>= 400` or missing internal files fail the check.
   - Configure defaults in `config.toml`: `[check] external = true`, `timeout_ms = 5000`.

4. **Preview interactively** to reproduce: `cstatic serve` (rebuilds on file change with live reload). Use `--drafts` to preview scheduled/draft content.

## Gotchas

- **Stale incremental cache:** `rm -rf output` alone can leave pages reported cached-but-missing. When smoke-testing, clear **both** `output/` and `.cstatic_cache/`, or just use `--full`.
- **HTML minifier is ON by default** (`build.minify.html = true`). If you're diffing generated HTML by hand, set it `false` to read it, or the attribute quotes are stripped.
- **Template errors surface per-page.** A bad template only fails pages using it; valid pages still build. The summary lists every failing page.
- **`cstatic check` runs after `cstatic build`** against `output/`. If you rebuilt and links still fail, confirm you're checking fresh output.
- **External checks need network + (on Linux) HTTPS support** in the binary. Transport errors there are warnings, not failures.

## When to use `--full`

- After enabling/changing `fingerprint_assets`.
- After manual edits to `output/` or `.cstatic_cache/`.
- When a page you expect is missing but the source is correct.
- In CI before `cstatic check`, for a deterministic result.
