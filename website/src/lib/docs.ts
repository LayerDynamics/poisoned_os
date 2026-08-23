import type { CollectionEntry } from "astro:content";
import path from "node:path";

export type SourceDoc = CollectionEntry<"docs">;

export type RepositoryDoc = {
  entry: SourceDoc;
  title: string;
  description: string;
  section: string;
  sourcePath: string;
  slug: string;
  plainText: string;
  headings: string[];
};

const repositoryRoot = path.resolve(process.cwd(), "..");

const sectionNames: Record<string, string> = {
  decisions: "Decisions",
  development: "Development",
  plans: "Plans",
  privacy: "Privacy",
  release: "Release",
  runbooks: "Runbooks",
  security: "Security",
  specs: "Specifications",
  testing: "Testing",
};

const sectionOrder = [
  "Start here",
  "Specifications",
  "Understand the system",
  "Operate",
  "Development",
  "Security",
  "Testing",
  "Release",
  "Runbooks",
  "Decisions",
  "Privacy",
  "Plans",
];

function sourcePathFor(entry: SourceDoc): string {
  if (!entry.filePath) throw new Error(`Documentation entry has no source path: ${entry.id}`);
  return path.relative(repositoryRoot, path.resolve(entry.filePath)).replaceAll(path.sep, "/");
}

function cleanInlineMarkdown(value: string): string {
  return value
    .replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")
    .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
    .replace(/[`*~]/g, "")
    .replace(/<[^>]+>/g, "")
    .trim();
}

function titleFor(entry: SourceDoc): string {
  const heading = (entry.body ?? "").match(/^#\s+(.+)$/m)?.[1];
  if (heading) return cleanInlineMarkdown(heading);
  const name = path.basename(sourcePathFor(entry), ".md");
  return name.replace(/[-_]+/g, " ").replace(/\b\w/g, (character) => character.toUpperCase());
}

function sectionFor(sourcePath: string): string {
  if (sourcePath === "README.md") return "Start here";
  const directory = sourcePath.split("/")[1];
  return sectionNames[directory] ?? "Repository docs";
}

function slugFor(sourcePath: string): string {
  if (sourcePath === "README.md") return "getting-started";
  return sourcePath
    .replace(/^docs\//, "")
    .replace(/\.md$/, "")
    .toLowerCase()
    .replace(/[^a-z0-9/]+/g, "-")
    .replace(/-+/g, "-")
    .replace(/(^-|-$)/g, "");
}

function plainTextFor(body: string): string {
  return body
    .replace(/```[\s\S]*?```/g, " ")
    .replace(/~~~[\s\S]*?~~~/g, " ")
    .replace(/<[^>]+>/g, " ")
    .replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")
    .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
    .replace(/^#{1,6}\s+/gm, "")
    .replace(/[>*_`~-]/g, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function descriptionFor(body: string, title: string): string {
  const paragraph = body
    .split(/\n\s*\n/)
    .filter((block) => !block.trim().startsWith("-") && !block.trim().startsWith("|") && !block.trim().startsWith("```"))
    .map((block) => cleanInlineMarkdown(block.replace(/^>\s?/gm, "").replace(/^#{1,6}\s+.*$/gm, "")))
    .find((block) => block.length > 50 && !block.startsWith("|"));
  return (paragraph ?? `${title} from the Poisoned_Os repository.`).slice(0, 190);
}

function headingsFor(body: string): string[] {
  return [...body.matchAll(/^#{2,3}\s+(.+)$/gm)].map((match) => cleanInlineMarkdown(match[1]));
}

export function toRepositoryDoc(entry: SourceDoc): RepositoryDoc {
  const sourcePath = sourcePathFor(entry);
  const body = entry.body ?? "";
  const title = titleFor(entry);
  return {
    entry,
    title,
    description: descriptionFor(body, title),
    section: sectionFor(sourcePath),
    sourcePath,
    slug: slugFor(sourcePath),
    plainText: plainTextFor(body),
    headings: headingsFor(body),
  };
}

export function sortDocs(docs: RepositoryDoc[]): RepositoryDoc[] {
  return [...docs].sort((left, right) => {
    const sectionDifference = sectionOrder.indexOf(left.section) - sectionOrder.indexOf(right.section);
    if (sectionDifference !== 0) return sectionDifference;
    return left.title.localeCompare(right.title);
  });
}

export function docsBySection(docs: RepositoryDoc[]): Map<string, RepositoryDoc[]> {
  const groups = new Map<string, RepositoryDoc[]>();
  for (const doc of sortDocs(docs)) groups.set(doc.section, [...(groups.get(doc.section) ?? []), doc]);
  return groups;
}

export function sourceUrl(sourcePath: string): string {
  return `https://github.com/LayerDynamics/poisoned_os/blob/main/${sourcePath}`;
}
