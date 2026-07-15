# Wiki page sources

These are the source files for the project GitHub wiki
(<https://github.com/N8SDR1/Lyra-SDR-cpp/wiki>), kept here under version
control so they don't drift.

Curated pages (edit here, then push to the wiki):

- `Home.md` — landing page
- `Quick-Start.md`
- `Installing-and-first-connection.md`
- `First-Voice-Setup.md`
- `PC-Requirements.md`
- `Supported-Radios.md`
- `Feature-Status.md`
- `Roadmap.md`
- `SDRLogger-Plus-Combo.md`
- `FAQ-and-Troubleshooting.md`
- `_Sidebar.md` — wiki navigation (shows on every page)
- `_Footer.md`

The **User Guide** wiki page is **generated** from the canonical
[`docs/help/USER_GUIDE.md`](../help/USER_GUIDE.md) (the same file built into
the app) — it is *not* duplicated here. Regenerate + publish the whole wiki
after editing any source:

```sh
WK="$(mktemp -d)/w"
git clone https://github.com/N8SDR1/Lyra-SDR-cpp.wiki.git "$WK"
cp docs/wiki/*.md "$WK"/
rm -f "$WK"/README.md            # this file is repo-only, not a wiki page
{ printf '> **Lyra User Guide** — a mirror of the in-app guide (**Help → User Guide**).\n> Canonical source: [docs/help/USER_GUIDE.md](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/docs/help/USER_GUIDE.md).\n\n'; \
  cat docs/help/USER_GUIDE.md; } > "$WK"/User-Guide.md
cd "$WK" && git add -A && git commit -m "Wiki update" && git push
```

> GitHub wikis can't be initialized from the API or a first `git push` — the
> very first page must be created once in the web UI. After that the
> `…wiki.git` repo is clonable/pushable as above.
