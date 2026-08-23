import type { ReactElement } from "react";

export interface ConsoleFrame { sequence: number; source: "stdout" | "stderr" | "log" | "event" | "truncation"; text: string; }
export function Console({ frames }: { frames: readonly ConsoleFrame[] }): ReactElement { return <ol aria-label="JavaScript console">{[...frames].sort((a, b) => a.sequence - b.sequence).map((frame) => <li key={frame.sequence} data-source={frame.source}>{frame.text}</li>)}</ol>; }
