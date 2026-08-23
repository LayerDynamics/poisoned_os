import type { ReactElement } from "react";

export interface LaunchableApp { name: string; version: string; capabilities: readonly string[]; locked: boolean; }
export function canLaunch(app: LaunchableApp): boolean { return !app.locked && app.name.length > 0 && app.version.length > 0; }
export function AppLauncher({ apps, onLaunch }: { apps: readonly LaunchableApp[]; onLaunch: (name: string) => void }): ReactElement {
  return <section aria-label="Applications"><h2>Applications</h2><ul>{apps.map((app) => <li key={app.name}><button type="button" disabled={!canLaunch(app)} onClick={() => onLaunch(app.name)}>{app.name}</button></li>)}</ul></section>;
}
