# C-Static Skills (opt-in)

Optional **Claude Code skills** that encode common C-Static site tasks. These are *source files* — nothing here is auto-installed. Copy the ones you want into your site project.

## What these are

A "skill" is a markdown file with YAML frontmatter (`name`, `description`) plus a body of steps, gotchas, and copy-paste examples. When present in a project's `.claude/skills/` directory, Claude Code can match a skill to the user's request and load its instructions. They are tuned for C-Static specifically (Inja templates, frontmatter, config keys) so the agent does not fall back on Hugo/Jekyll/Zola patterns.

## Install

1. Copy the desired `.md` files from this repo's `skills/` into your site project:

   ```bash
   mkdir -p .claude/skills
   cp /path/to/cstatic/skills/cstatic-*.md .claude/skills/
   ```

2. Restart Claude Code (or start a new session) so the skills are discovered.

3. Install only what you need — each skill is independent.

These skills are **not** scaffolded by `cstatic init`. They live here as the source of truth.

## Skill index

| Skill | Use when… |
|-------|-----------|
| `cstatic-new-content.md` | Adding a page or blog post (correct frontmatter, archetypes, drafts, scheduling). |
| `cstatic-add-collection.md` | Defining a content collection (posts, projects) with item + index templates. |
| `cstatic-add-shortcode.md` | Creating a reusable content component (inline vs block shortcodes). |
| `cstatic-configure-seo.md` | Enabling sitemap/RSS/JSON Feed/robots, OG images, `{{ seo_meta }}`. |
| `cstatic-diagnose.md` | Debugging a broken build or broken links (`cstatic build`/`check`, `--full`). |
| `cstatic-deploy.md` | Shipping to Cloudflare Workers (`build --env production`, git push, secrets). |

## Keeping them current

These skills summarize C-Static's stable surface. For the authoritative reference, see `AGENTS.md` and `docs/config.md` in the C-Static repo. If a skill contradicts those docs, the docs win.
