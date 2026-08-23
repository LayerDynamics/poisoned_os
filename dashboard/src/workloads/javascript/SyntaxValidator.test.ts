import { describe, expect, it } from "vitest";
import { validateJavaScriptSyntax } from "./SyntaxValidator";
import { runtimeBuiltinFiles } from "./RuntimeBuiltins";

describe("JavaScript syntax validation", () => {
  it("validates executable sources without parsing the dependency lock as JavaScript", () => {
    expect(validateJavaScriptSyntax({
      "main.js": "print('ready');\n",
      "poison-js.lock": "{\"note\": \"import x from 'y'\"}\n",
    }, "main.js")).toEqual([]);
  });

  it("keeps every injected Node-compatible shim inside the actual mJS grammar", () => {
    const main = [
      "assert", "buffer", "crypto", "events", "fs", "http", "https", "net", "os",
      "path", "process", "promise", "querystring", "stream", "string_decoder", "timers",
      "tls", "url", "util",
    ].map((name) => `require(${JSON.stringify(name)});`).join("\n");
    const files = { "main.js": main };
    expect(validateJavaScriptSyntax({ ...files, ...runtimeBuiltinFiles(files) }, "main.js"))
      .toEqual([]);
  });

  it("rejects loop forms that the device parser cannot compile", () => {
    const diagnostics = validateJavaScriptSyntax({
      "main.js": "for (var index = 0; index < 2; index++) print(index);\nfor (; ready; poll()) print('waiting');\n",
    }, "main.js");

    expect(diagnostics.map(({ code }) => code)).toEqual([
      "MJS_FOR_DECLARATION_UNSUPPORTED",
      "MJS_FOR_INITIALIZER_REQUIRED",
    ]);
  });

  it("rejects an unbraced if/else form that the device parser misparses", () => {
    const diagnostics = validateJavaScriptSyntax({
      "main.js": "if (ready) start(); else stop();\n",
    }, "main.js");

    expect(diagnostics.map(({ code }) => code)).toEqual(["MJS_IF_ELSE_BLOCK_REQUIRED"]);
  });
});
