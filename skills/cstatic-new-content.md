---
name: cstatic-new-content
description: Create a new C-Static page or blog post with correct frontmatter. Use when the user says "add a post", "new page", "write a blog post", "create content", or asks to scaffold markdown under src/. Covers archetypes, drafts, and scheduled publishing.
---

# Skill: Create new C-Static content

Create Markdown files with YAML frontmatter under `src/`. Prefer the archetype scaffolder for consistency.

## Steps

1. **Scaffold from an archetype** (preferred over hand-writing frontmatter):

   ```bash
   cstatic new --kind post posts/my-post.md   # archetypes/post.md
   cstatic new about.md                        # archetypes/default.md
   ```

   - The output path is `<source_dir>/<path>` (default `src/`). Parent dirs are created automatically.
   - The filename stem derives the title and slug (`my-post` → title "My Post", slug `my-post`).
   - **Existing files are never overwritten.** If the file exists, pick a different name.

2. **Edit the frontmatter.** Required/recommended fields:

   ```markdown
   ---
   title: My Post
   date: 2026-07-01
   draft: false
   tags: [web, writing]
   description: One-line summary for SEO and social cards.
   ---
   ```

   - `title` — defaults to the Title-cased filename stem if omitted.
   - `layout` — defaults to `default`; a `[[collection]] template` overrides it if the page is under that collection's dir and `layout` is unset.
   - `date` — `YYYY-MM-DD`; used for sorting. **A future date skips the build** unless `build.publish_future = true`.
   - `draft: true` — skipped in normal builds; include with `--drafts` or the dev server.
   - Extra fields become `page.<field>` in templates.

3. **Write the body** in Markdown under the frontmatter. GFM extensions (tables, task lists, strikethrough, autolinks) are on by default. Shortcodes (`{{< name >}}`) and wikilinks (`[[target]]`) expand here before the markdown render.

4. **Build and verify:**

   ```bash
   cstatic build
   cstatic serve          # preview at http://localhost:3000
   ```

## Gotchas

- **Scheduled publishing:** a `date` in the future is treated like a draft (excluded from build, collections, feeds, sitemap) until the date arrives. The build summary reports `(N scheduled)`. Bypass for preview with `--drafts` or `cstatic serve`.
- **Archetype placeholders** tolerate inner whitespace (`{{title}}` ≡ `{{ title }}`). The three supported placeholders are `{{ title }}`, `{{ slug }}`, `{{ date }}`.
- **Unknown `--kind`** prints a warning and falls back to `default`. Create `archetypes/<kind>.md` to add a new kind.
- Don't put content files outside `src/` — they won't be discovered.

## Example archetype (`archetypes/post.md`)

```markdown
---
title: {{ title }}
date: {{ date }}
draft: true
tags: []
---

Write your post here.
```
