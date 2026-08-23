import { useState, type ReactElement } from "react";

export interface ThemeDraft { id: string; foreground: string; background: string; }

function channel(value: number): number {
  const normalized = value / 255;
  return normalized <= 0.04045 ? normalized / 12.92 : ((normalized + 0.055) / 1.055) ** 2.4;
}

function luminance(color: string): number | null {
  if (!/^#[0-9a-f]{6}$/i.test(color)) return null;
  const rgb = [1, 3, 5].map((offset) => channel(Number.parseInt(color.slice(offset, offset + 2), 16)));
  return 0.2126 * rgb[0]! + 0.7152 * rgb[1]! + 0.0722 * rgb[2]!;
}

export function themeContrastRatioX10(theme: ThemeDraft): number {
  const foreground = luminance(theme.foreground);
  const background = luminance(theme.background);
  if (foreground === null || background === null) return 0;
  return Math.round(((Math.max(foreground, background) + 0.05) / (Math.min(foreground, background) + 0.05)) * 10);
}

export function isAccessibleTheme(theme: ThemeDraft): boolean {
  return /^[a-z0-9][a-z0-9._-]{0,63}$/.test(theme.id) && themeContrastRatioX10(theme) >= 45;
}

export function ThemeEditor({ theme, onChange }: { theme: ThemeDraft; onChange?: (theme: ThemeDraft) => void }): ReactElement {
  const [draft, setDraft] = useState(theme);
  const update = (next: ThemeDraft) => { setDraft(next); onChange?.(next); };
  const contrast = themeContrastRatioX10(draft);
  return <section aria-label="Theme editor">
    <h2>Theme preview</h2>
    <label>Package ID<input value={draft.id} onChange={(event) => update({ ...draft, id: event.target.value })} /></label>
    <label>Foreground<input type="color" value={draft.foreground} onChange={(event) => update({ ...draft, foreground: event.target.value })} /></label>
    <label>Background<input type="color" value={draft.background} onChange={(event) => update({ ...draft, background: event.target.value })} /></label>
    <output>{contrast / 10}:1 — {isAccessibleTheme(draft) ? "Accessible contrast" : "Contrast validation required"}</output>
    <div aria-label="Theme sample" style={{ color: draft.foreground, backgroundColor: draft.background, padding: "0.75rem" }}>Poisoned_Os field console</div>
  </section>;
}
