---
name: cstatic-deploy
description: Deploy a C-Static site to Cloudflare Workers. Use when the user says "deploy", "publish", "ship to production", "cloudflare", "wrangler", or asks how the site goes live. Covers build --env production, the git-push flow, and the two Cloudflare secrets.
---

# Skill: Deploy to Cloudflare Workers

`cstatic init` scaffolds an assets-only Cloudflare Worker plus a GitHub Actions workflow. Deploy is **Local → GitHub → Cloudflare Workers**: push to `main` builds and deploys `output/`.

## Steps

1. **Build for production** with the production config overlay:

   ```bash
   cstatic build --env production
   ```

   - Deep-merges `config.production.toml` onto `config.toml` (tables deep-merge; scalars/arrays replaced wholesale).
   - Sets `{{ site.env }}` to `"production"` — use it to gate analytics, canonical URLs, etc.
   - Create `config.production.toml` for production-only settings (e.g. the real `base_url`).

2. **Push to `main`:**

   ```bash
   git add -A && git commit -m "Deploy site"
   git push origin main
   ```

   `.github/workflows/deploy.yml` then:
   - Downloads the latest `cstatic` release binary (`CSTATIC_REPO`, default `daveviamedia-code/cstatic`).
   - Runs `cstatic build --env production`.
   - Deploys `output/` to Cloudflare Workers via `cloudflare/wrangler-action`.

## One-time setup (Cloudflare secrets)

Add two repository secrets under *Settings → Secrets and variables → Actions*:

- `CLOUDFLARE_API_TOKEN` — a token with **Workers Scripts: Edit** + **Account: Read**.
- `CLOUDFLARE_ACCOUNT_ID` — your Cloudflare account ID.

The first push creates the Worker; later pushes update it. (Optional) add a custom domain in the Cloudflare dashboard.

## Manual alternative (no GitHub Actions)

```bash
cstatic build --env production
npx wrangler deploy
```

`wrangler.jsonc` is an assets-only Worker (no Worker script runs — `output/` is served directly, with `/404.html` handling unmatched paths).

## Gotchas

- **Worker names are unique per account.** `cstatic init --name "My Blog"` slugifies to `my-blog` and sets both `config.toml` title and the Worker name. Give each site a distinct name to avoid deploy collisions.
- **A `v*` GitHub release must exist** for the workflow's `curl …/releases/latest/download/cstatic-linux-x86_64` to succeed, otherwise the download 404s. If you fork or host the binary elsewhere, set `CSTATIC_REPO` at the top of `deploy.yml`.
- **Build before deploy.** The workflow runs `cstatic build` itself, so you only need to `git push`; for a manual `wrangler deploy`, run `cstatic build --env production` first.
- **Config overlays replace arrays.** A `[[collection]]` in `config.production.toml` replaces the base collections entirely (not appended).
- The release asset is named `cstatic-linux-x86_64` (matches the workflow matrix). Local/dev builds on macOS/Windows are fine; CI runs on Linux.
