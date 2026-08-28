"""Build hook: repo-root includes and link rewriting.

Two jobs, both needed to keep ``mkdocs build --strict`` clean without
touching any existing repository file:

1. Expand ``--8<-- "FILE"`` include lines (pymdownx.snippets syntax,
   repo-root relative) *before* MkDocs' link validation runs, so that
   SPEC.md and CONTRIBUTING.md are rendered into the site from their
   single source of truth without duplication.

2. Rewrite repository-relative links that are correct on GitHub but have
   no meaning inside the built site:

   - ``../SPEC.md`` / ``SPEC.md``  -> the site's Specification page
     (docs/design*.md link to the spec by repository path, and so does
     the included CONTRIBUTING.md);
   - ``testvectors/...``, ``LICENSE``, ``scripts/...``, ``.github/...``
     and the per-language directories -> the file on GitHub;
   - cross-language suffix links (``design.ko.md`` <-> ``design.md``)
     -> the mkdocs-static-i18n localized URL, so the hand-written
     language-switcher line in the existing docs keeps working.

The source files stay untouched; only the in-memory Markdown fed to the
renderer changes.
"""

from __future__ import annotations

import os
import re

REPO_URL = "https://github.com/algorix-hq/dealcode"

_SNIPPET_RE = re.compile(r'^--8<--\s+"([^"]+)"\s*$', re.MULTILINE)

# Repository paths (as they appear inside existing Markdown links) that have
# no counterpart page on the site and should point at GitHub instead.
_GITHUB_DIR_RE = re.compile(
    r"\]\((?:\.\./)?(testvectors|scripts|python|js|go|java|rust|c|cpp|\.github)"
    r"(/[^)#\s]*)?\)"
)


def _repo_root(config) -> str:
    return os.path.dirname(config["config_file_path"])


def _expand_snippets(markdown: str, root: str) -> str:
    def include(match: re.Match) -> str:
        path = os.path.join(root, match.group(1))
        with open(path, encoding="utf-8") as fh:
            return fh.read()

    return _SNIPPET_RE.sub(include, markdown)


def _github_target(match: re.Match) -> str:
    head, tail = match.group(1), match.group(2) or ""
    kind = "blob" if "." in tail.rsplit("/", 1)[-1] else "tree"
    return f"]({REPO_URL}/{kind}/main/{head}{tail.rstrip('/')})"


# Sources concatenated into site/llms-full.txt (order matters; SPEC.md is
# included directly rather than via the docs/spec.md wrapper).
_LLMS_FULL_SOURCES = [
    "docs/index.md",
    "docs/getting-started.md",
    "docs/guide/configuration.md",
    "docs/guide/database.md",
    "docs/guide/security.md",
    "SPEC.md",
]


def on_post_build(config) -> None:
    """Emit llms-full.txt: the whole library documentation as one Markdown
    file, for AI agents that prefer a single fetch (llmstxt.org)."""
    root = _repo_root(config)
    parts = [
        "# dealcode — full documentation for LLMs\n\n"
        "> Concatenated from the documentation site and the normative "
        "SPEC.md.\n> Index: https://algorix-hq.github.io/dealcode/llms.txt\n"
    ]
    for rel in _LLMS_FULL_SOURCES:
        with open(os.path.join(root, rel), encoding="utf-8") as fh:
            parts.append(f"\n\n---\n<!-- source: {rel} -->\n\n" + fh.read())
    out = os.path.join(config["site_dir"], "llms-full.txt")
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("".join(parts))


def on_page_markdown(markdown: str, page, config, files) -> str:
    markdown = _expand_snippets(markdown, _repo_root(config))

    is_ko = page.file.src_uri.endswith(".ko.md")
    spec_page = "spec.ko.md" if is_ko else "spec.md"

    # The existing docs carry a hand-written language-switcher line
    # ("**English** | [한국어](design.ko.md)").  Cross-locale suffix links
    # are not documentation files within a single locale's build, so point
    # them at the deployed localized URL instead.
    site_url = (config["site_url"] or "/").rstrip("/") + "/"
    basename = os.path.basename(page.file.src_uri)
    if is_ko:
        counterpart = basename[: -len(".ko.md")] + ".md"
        markdown = markdown.replace(
            f"]({counterpart})",
            f"]({site_url}{counterpart[:-3]}/)",
            1,
        )
    else:
        counterpart = basename[: -len(".md")] + ".ko.md"
        markdown = markdown.replace(
            f"]({counterpart})",
            f"]({site_url}ko/{counterpart[:-len('.ko.md')]}/)",
            1,
        )

    # Links to the normative spec / contributing guide by repository path.
    markdown = markdown.replace("](../SPEC.md)", f"]({spec_page})")
    markdown = markdown.replace("](SPEC.md)", f"]({spec_page})")

    # Repository files and directories without site pages -> GitHub.
    markdown = _GITHUB_DIR_RE.sub(_github_target, markdown)
    markdown = markdown.replace("](LICENSE)", f"]({REPO_URL}/blob/main/LICENSE)")
    markdown = markdown.replace(
        "](../LICENSE)", f"]({REPO_URL}/blob/main/LICENSE)"
    )

    return markdown
