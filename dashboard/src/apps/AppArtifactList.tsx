import type { ReactElement } from "react";
export function AppArtifactList({ artifactIds }: { artifactIds: readonly string[] }): ReactElement { return <ul aria-label="Application artifacts">{artifactIds.map((id) => <li key={id}>{id}</li>)}</ul>; }
