---
name: cstatic-add-collection
description: Add a C-Static content collection (posts, projects, etc.) with item and index templates. Use when the user says "add a collection", "blog section", "set up posts", "content type", or wants auto-generated index pages and {{ collections.X }} in templates.
---

# Skill: Add a content collection

A collection is a content type (e.g. "posts") with a default item template, optional URL pattern, sort order, and an auto-generated index page.

## Steps

1. **Define the collection** in `config.toml`:

   ```toml
   [[collection]]
   name = "posts"                # REQUIRED — matches src/posts/*.md
   template = "post"             # REQUIRED — default item template (no .html)
   index_template = "posts-index"  # optional; default <name>-index
   url_pattern = "/blog/{{ slug }}/"  # optional; empty = auto from file path
   sort_by = "date"              # optional; default "date"
   sort_order = "desc"           # optional; "desc" (newest) | "asc"
   ```

   Notes:
   - `name` must match the directory under `src/` (e.g. `src/posts/`).
   - The `slug` in `url_pattern` is the filename stem.

2. **Create the item template** `templates/post.html`. Available context: `page`, `site`, `pages`. Example:

   ```html
   {% extends "base" %}
   {% block content %}
   <article>
     <h1>{{ page.title }}</h1>
     <time>{{ page.date }}</time>
     {{ page.content }}
   </article>
   {% endblock %}
   ```

3. **Create the index template** `templates/posts-index.html`. Context: `collection.name`, `collection.pages`, plus `site`, `pages`. Example:

   ```html
   {% extends "base" %}
   {% block content %}
   <h1>{{ collection.name }}</h1>
   <ul>
     {% for p in collection.pages %}
     <li><a href="{{ p.url }}">{{ p.title }}</a> — {{ p.date }}</li>
     {% endfor %}
   </ul>
   {% endblock %}
   ```

   The index page is generated at `/<name>/index.html` (e.g. `/posts/`).

4. **Add an archetype** (optional, for `cstatic new --kind <name>`): `archetypes/post.md`.

5. **Use the collection anywhere.** Every template gains `{{ collections.posts }}` (sorted). Example for a homepage:

   ```html
   {% for p in collections.posts %}
   <a href="{{ p.url }}">{{ p.title }}</a>
   {% endfor %}
   ```

6. **Build:** `cstatic build`.

## Gotchas

- A page's explicit `layout` in frontmatter takes precedence over the collection `template`. To use the collection template, **omit** `layout` (or set it to the collection template name).
- The auto index page is generated even if `index_template` is omitted (defaults to `<name>-index`); create that template or set `index_template` explicitly.
- `url_pattern` only affects output URLs; it does not move the source files. Keep sources under `src/<name>/`.
- Sort falls back gracefully when `sort_by` is missing on a page (those pages sort last).
- Drafts and future-dated pages are excluded from `collection.pages` (same rules as `{{ pages }}`).
