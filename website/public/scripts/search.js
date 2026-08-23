const input = document.querySelector("#search-input");
const results = document.querySelector("#search-results");
const query = new URLSearchParams(window.location.search).get("q")?.trim() ?? "";
if (input instanceof HTMLInputElement) input.value = query;

const escape = (value) => value.replace(/[&<>"']/g, (character) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[character] ?? character));

fetch("/search-index.json")
  .then((response) => response.json())
  .then((entries) => {
    const normalized = query.toLowerCase();
    const matches = normalized ? entries.filter((entry) => `${entry.title} ${entry.section} ${entry.sourcePath} ${entry.text}`.toLowerCase().includes(normalized)) : entries;
    if (!(results instanceof HTMLElement)) return;
    results.innerHTML = `<div class="search-result-heading"><span>${query ? `Matches for “${escape(query)}”` : "All repository sources"}</span><strong>${matches.length} result${matches.length === 1 ? "" : "s"}</strong></div>`;
    if (!matches.length) { results.insertAdjacentHTML("beforeend", `<p class="empty-state">No source matches that phrase. Try “pairing”, “evidence”, or “release”.</p>`); return; }
    const list = document.createElement("div");
    list.className = "result-list";
    matches.forEach((entry) => {
      const link = document.createElement("a");
      link.href = `/docs/${entry.slug}/`;
      link.innerHTML = `<span>${escape(entry.section)}</span><strong>${escape(entry.title)}</strong><small>${escape(entry.description)}</small>`;
      list.append(link);
    });
    results.append(list);
  })
  .catch(() => { if (results instanceof HTMLElement) results.innerHTML = `<p class="empty-state">The repository index could not be loaded.</p>`; });
