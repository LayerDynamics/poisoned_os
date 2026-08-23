export function strictCspPolicy(): string {
  return "default-src 'none'; script-src blob:; style-src blob:; img-src blob:; font-src blob:; media-src blob:; connect-src 'none'; base-uri 'none'; form-action 'none'; frame-src 'none'; frame-ancestors 'none'; object-src 'none'; worker-src 'none'; manifest-src 'none'; navigate-to 'none';";
}
