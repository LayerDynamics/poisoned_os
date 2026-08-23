export interface SyntaxDiagnostic { file: string; line: number; column: number; code: string; message: string; }

const UNSUPPORTED: readonly [RegExp, string, string][] = [
  [/\b(?:async\s+function|await)\b/, "MJS_ASYNC_UNSUPPORTED", "async/await is not supported by poison-mjs-1"],
  [/\b(?:import|export)\s+/, "MJS_MODULE_SYNTAX_UNSUPPORTED", "module syntax must be bundled before transfer"],
  [/`/, "MJS_TEMPLATE_UNSUPPORTED", "template literals are not supported by poison-mjs-1"],
  [/\b(?:case|delete|do|instanceof|new|switch|throw|try|void|with)\b/, "MJS_KEYWORD_UNSUPPORTED", "source uses a keyword that poison-mjs-1 cannot execute"],
  [/\bMath\s*\./, "MJS_MATH_GLOBAL_UNSUPPORTED", "the Math global is not available in poison-mjs-1"],
  [/\bfor\s*\(\s*(?:var|const)\b/, "MJS_FOR_DECLARATION_UNSUPPORTED", "poison-mjs-1 requires let for loop declarations"],
  [/\bfor\s*\(\s*;/, "MJS_FOR_INITIALIZER_REQUIRED", "poison-mjs-1 requires an explicit for-loop initializer"],
  [/\bif\s*\([^)]*\)\s*(?!\{)[^;{}]+;\s*else\b/, "MJS_IF_ELSE_BLOCK_REQUIRED", "poison-mjs-1 requires braces around an if/else branch"],
];

export function validateJavaScriptSyntax(files: Readonly<Record<string, string>>, entrypoint: string): SyntaxDiagnostic[] {
  const diagnostics: SyntaxDiagnostic[] = [];
  for (const [file, source] of Object.entries(files).sort(([a], [b]) => a.localeCompare(b))) {
    if (!/\.(?:js|mjs|cjs)$/.test(file)) continue;
    let depth = 0;
    source.split("\n").forEach((line, index) => {
      for (let column = 0; column < line.length; column += 1) {
        if ("({[".includes(line[column])) depth += 1;
        if (")}]".includes(line[column])) depth -= 1;
        if (depth < 0) diagnostics.push({ file, line: index + 1, column: column + 1, code: "MJS_UNBALANCED_DELIMITER", message: "closing delimiter has no matching opener" });
      }
      for (const [pattern, code, message] of UNSUPPORTED) {
        const match = pattern.exec(line);
        if (match) diagnostics.push({ file, line: index + 1, column: match.index + 1, code, message });
      }
    });
    if (depth !== 0) diagnostics.push({ file, line: source.split("\n").length, column: 1, code: "MJS_UNBALANCED_DELIMITER", message: "source has unbalanced delimiters" });
  }
  if (!files[entrypoint]) diagnostics.push({ file: entrypoint, line: 1, column: 1, code: "MJS_ENTRYPOINT_MISSING", message: "entrypoint is not present in the project" });
  return diagnostics;
}
