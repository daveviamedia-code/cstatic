---
name: cstatic-add-shortcode
description: Create a C-Static shortcode (reusable content component). Use when the user says "add a shortcode", "reusable component", "embed widget", "youtube/figure/note component", or wants {{< name >}} syntax in markdown. Covers inline vs block shortcodes and the render context.
---

# Skill: Add a shortcode

Shortcodes are reusable Inja HTML snippets invoked from Markdown via `{{< name params >}}`. They are stored as `shortcodes/<name>.html` and expand **before** the cmark-gfm render pass, so emitted HTML passes through.

## Render context

Every shortcode template receives:

| Variable | Type | Meaning |
|----------|------|---------|
| `params` | array | Positional args (`{{ params.0 }}`, `{{ params.1 }}`). |
| `named` | object | Named args (`{{ named.src }}`). |
| `content` | string | Inner markdown/HTML for block shortcodes. |
| `page` | object | Current page: `title`, `url`, `slug`, `date`. |

## Steps

1. **Choose inline vs block:**
   - **Inline** — has params on the opener: `{{< youtube ID >}}`, `{{< figure src="x" >}}`. Access via `params.N` / `named.key`.
   - **Block** — no params on the opener ⇒ treated as a block start; `{{ content }}` holds inner markdown: `{{< note >}}…{{< /note >}}`. Same-name blocks nest correctly (balanced-close search).

2. **Create the template** `shortcodes/<name>.html`. Example inline (figure):

   ```html
   <figure class="figure">
     <img src="{{ named.src }}" alt="{{ named.alt }}">
     {% if named.caption %}<figcaption>{{ named.caption }}</figcaption>{% endif %}
   </figure>
   ```

   Example block (note):

   ```html
   <div class="note">
     {{ content }}
   </div>
   ```

3. **Use it in Markdown:**

   ```markdown
   {{< figure src="/img/cat.jpg" alt="A cat" caption="Mittens" >}}

   {{< note >}}
   This is **important** and renders as HTML.
   {{< /note >}}
   ```

4. **Build:** `cstatic build`.

## Gotchas

- **Expand-before-render timing:** shortcode output is emitted into the markdown *before* cmark-gfm runs, so the HTML survives. Don't wrap shortcode output in surrounding markdown that depends on the shortcode being present — emit the raw tag directly.
- **Default directory** is `shortcodes/` (override with `build.markdown.shortcodes_dir`). Shortcodes auto-disable when the directory is empty or missing.
- **Unknown shortcode** → stderr notice + expands to nothing (no error). Check the name matches the filename stem.
- **Same-name nesting:** `{{< w >}}…{{< w >}}…{{< /w >}}…{{< /w >}}` is supported via balanced-close matching; a naive "first close wins" is wrong.
- **Inja syntax, not Go/Liquid:** `{{ params.0 }}`, `{% if named.caption %}…{% endif %}`. No piped filters.
- Shortcodes are cached (thread-safe). A shortcode template change triggers a rebuild of all pages that use it.
