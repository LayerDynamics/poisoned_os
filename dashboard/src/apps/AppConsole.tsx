import type { ReactElement } from "react";
import type { AppEvent } from "./AppEvents";
export function AppConsole({ events }: { events: readonly AppEvent[] }): ReactElement { return <pre aria-label="Application console">{events.map((event) => `[${event.level}] ${event.message}`).join("\n")}</pre>; }
