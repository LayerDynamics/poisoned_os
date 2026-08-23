import DOMPurify from "dompurify";
import { marked } from "marked";
import "./style.css";

type DocSource = string;
type Doc = { id: string; path: string; title: string; section: string; source: DocSource; html: string; text: string };

const modules = import.meta.glob(["../../docs/**/*.md", "../../README.md"], { eager: true, query: "?raw", import: "default" }) as Record<string, DocSource>;
const published = [
  ["getting-started", "README.md", "Start here", "Product guide"],
  ["architecture", "docs/specs/SPEC-1-poisonedos-for-the-flipper-zero.md", "Product specification", "Understand the system"],
  ["dashboard", "docs/runbooks/self-hosted-dashboard.md", "Self-hosted dashboard", "Operate"],
  ["evidence", "docs/decisions/ADR-0007-evidence-package-format.md", "Evidence package format", "Operate"],
  ["development", "docs/development/javascript-capability-map.md", "JavaScript capability map", "Build"],
  ["rust", "docs/development/tool-adapter-contract.md", "Tool adapter contract", "Build"],
  ["security", "docs/security/tool-capability-policy.md", "Tool capability policy", "Security"],
  ["updates", "docs/runbooks/release.md", "Release runbook", "Maintain"],
  ["limitations", "docs/release/v1-known-limitations.md", "Known limitations", "Maintain"],
] as const;

function sourceFor(path: string): DocSource {
  const key = Object.keys(modules).find((candidate) => candidate.endsWith(path));
  if (!key) throw new Error(`Published document is missing from the repository: ${path}`);
  return modules[key];
}

function stripMarkdown(value: string): string {
  return value.replace(/```[\s\S]*?```/g, " ").replace(/[#*_>`-]/g, " ").replace(/\[([^\]]+)\]\([^)]*\)/g, "$1").replace(/\s+/g, " ").trim();
}

const docs: Doc[] = published.map(([id, path, title, section]) => {
  const source = sourceFor(path);
  const html = DOMPurify.sanitize(marked.parse(source) as string, { USE_PROFILES: { html: true } });
  return { id, path, title, section, source, html, text: stripMarkdown(source).toLowerCase() };
});

const app = document.querySelector<HTMLDivElement>("#app");
if (!app) throw new Error("Documentation root is missing");

app.innerHTML = `<div class="docs-shell"><header class="topbar docs-topbar"><a class="brand" href="/"><span class="brand-mark">P_</span><span>Poisoned_Os</span></a><nav class="topnav" aria-label="Primary navigation"><a class="active" href="/docs/">Documentation</a><a href="/">Product</a><a href="https://github.com/LayerDynamics/poisoned_os">GitHub <span aria-hidden="true">↗</span></a></nav></header><div class="docs-layout"><aside class="docs-sidebar"><p class="eyebrow">Field manual / 01</p><h1>Documentation</h1><p class="sidebar-copy">Install, operate, build, and understand the browser-controlled Flipper Zero platform.</p><label class="search-box"><span aria-hidden="true">⌕</span><input id="doc-search" type="search" placeholder="Search the manual" autocomplete="off"></label><nav id="doc-nav" aria-label="Documentation sections"></nav><a class="sidebar-back" href="/">← Back to Poisoned_Os</a></aside><main class="docs-main"><div id="doc-results" class="doc-results" hidden></div><article id="doc-content" class="doc-content"></article></main></div></div>`;

const nav = document.querySelector<HTMLElement>("#doc-nav");
const content = document.querySelector<HTMLElement>("#doc-content");
const results = document.querySelector<HTMLElement>("#doc-results");
const search = document.querySelector<HTMLInputElement>("#doc-search");
if (!nav || !content || !results || !search) throw new Error("Documentation controls are missing");
const docNav = nav;
const docContent = content;
const docResults = results;
const docSearch = search;

const grouped = docs.reduce<Record<string, Doc[]>>((acc, doc) => ((acc[doc.section] ??= []).push(doc), acc), {});
docNav.innerHTML = Object.entries(grouped).map(([section, entries]) => `<div class="nav-group"><p>${section}</p>${entries.map((doc) => `<a href="#/${doc.id}" data-doc-id="${doc.id}">${doc.title}</a>`).join("")}</div>`).join("");

function renderDoc(doc: Doc): void {
  docResults.hidden = true;
  docContent.hidden = false;
  docContent.innerHTML = `<div class="doc-kicker"><span>${doc.section}</span><span>Source: ${doc.path}</span></div><h1>${doc.title}</h1><p class="doc-intro">Maintained from the Poisoned_Os repository so the public manual stays aligned with the system it describes.</p><div class="markdown">${doc.html}</div>`;
  docNav.querySelectorAll("a").forEach((link) => link.classList.toggle("active", link.getAttribute("data-doc-id") === doc.id));
  docContent.querySelectorAll<HTMLAnchorElement>("a[href]").forEach((link) => {
    const href = link.getAttribute("href") ?? "";
    const target = docs.find((candidate) => href.endsWith(candidate.path) || href.endsWith(candidate.path.replace("docs/", "")));
    if (target) link.href = `#/${target.id}`;
    else if (href.startsWith("http")) { link.target = "_blank"; link.rel = "noreferrer"; }
  });
  window.scrollTo({ top: 0, behavior: "instant" });
}

function renderSearch(query: string): void {
  const matches = docs.filter((doc) => `${doc.title} ${doc.section} ${doc.text}`.includes(query.toLowerCase().trim()));
  docContent.hidden = true;
  docResults.hidden = false;
  docResults.innerHTML = `<div class="doc-kicker"><span>Search</span><span>${matches.length} result${matches.length === 1 ? "" : "s"}</span></div><h1>Matches for “${query.replace(/[&<>]/g, "")}”</h1><div class="result-list">${matches.length ? matches.map((doc) => `<a href="#/${doc.id}"><span>${doc.section}</span><strong>${doc.title}</strong><small>${stripMarkdown(doc.source).slice(0, 190)}…</small></a>`).join("") : `<p class="empty-state">No manual entry matches that phrase. Try “pairing”, “evidence”, or “release”.</p>`}</div>`;
  docNav.querySelectorAll("a").forEach((link) => link.classList.remove("active"));
}

function route(): void {
  const id = window.location.hash.replace(/^#\//, "");
  const doc = docs.find((candidate) => candidate.id === id) ?? docs[0];
  renderDoc(doc);
}

docSearch.addEventListener("input", () => docSearch.value.trim() ? renderSearch(docSearch.value) : route());
window.addEventListener("hashchange", route);
route();
