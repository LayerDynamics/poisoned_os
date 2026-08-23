import path from "node:path";

const repositoryRoot = path.resolve(process.cwd(), "..");
const repositoryUrl = "https://github.com/LayerDynamics/poisoned_os";

function routeFor(sourcePath) {
  if (sourcePath === "README.md") return "/docs/getting-started/";
  if (!sourcePath.startsWith("docs/") || !sourcePath.endsWith(".md")) return null;
  const slug = sourcePath
    .replace(/^docs\//, "")
    .replace(/\.md$/, "")
    .toLowerCase()
    .replace(/[^a-z0-9/]+/g, "-")
    .replace(/-+/g, "-")
    .replace(/(^-|-$)/g, "");
  return `/docs/${slug}/`;
}

function repositoryLink(sourcePath) {
  return `${repositoryUrl}/blob/main/${sourcePath}`;
}

export default function repositoryLinks() {
  return (tree, file) => {
    const currentPath = file.path ? path.relative(repositoryRoot, path.resolve(file.path)).replaceAll(path.sep, "/") : "README.md";
    const currentDirectory = path.posix.dirname(currentPath);
    const visit = (node) => {
      if (node.type === "link" && node.url && !/^(?:[a-z]+:|#|\/)/i.test(node.url)) {
        const [target, fragment] = node.url.split("#", 2);
        const resolved = path.posix.normalize(path.posix.join(currentDirectory, target));
        const route = routeFor(resolved);
        node.url = route ? `${route}${fragment ? `#${fragment}` : ""}` : `${repositoryLink(resolved)}${fragment ? `#${fragment}` : ""}`;
      }
      node.children?.forEach(visit);
    };
    visit(tree);
  };
}
