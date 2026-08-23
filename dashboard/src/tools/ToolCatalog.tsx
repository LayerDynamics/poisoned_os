import type { ReactElement } from "react";

export interface ToolCatalogEntry { id: string; family: string; status: "foundation" | "verified" | "unavailable"; purpose: string; capabilities: readonly string[]; sample: string; }
export function canRunTool(tool: ToolCatalogEntry): boolean { return tool.status === "verified" && tool.capabilities.length > 0; }
export function ToolCatalog({ tools, onSelect }: { tools: readonly ToolCatalogEntry[]; onSelect?: (id: string) => void }): ReactElement {
  return <section aria-label="Tool catalog"><h2>Tools</h2><ul>{tools.map((tool) => <li key={tool.id}><span>{tool.id} · {tool.status}</span><p>{tool.purpose}</p><button type="button" disabled={!canRunTool(tool)} onClick={() => onSelect?.(tool.id)}>Open</button></li>)}</ul></section>;
}
