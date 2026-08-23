function normalize(path) {
    path = "" + path;
    var absolute = path[0] === "/";
    var parts = [], segment = "";
    for (let cursor = 0; cursor <= path.length; cursor++) {
        if (cursor === path.length || path[cursor] === "/") { parts.push(segment); segment = ""; }
        else segment += path[cursor];
    }
    var result = [];
    for (let i = 0; i < parts.length; i++) {
        if (!parts[i] || parts[i] === ".") continue;
        if (parts[i] === ".." && result.length && result[result.length - 1] !== "..") { result.splice(result.length - 1, 1); }
        else if (parts[i] !== ".." || !absolute) { result.push(parts[i]); }
    }
    var value = "";
    for (let index = 0; index < result.length; index++) value += (index ? "/" : "") + result[index];
    if (absolute) value = "/" + value;
    return value || (absolute ? "/" : ".");
}
function join(a, b, c, d, e, f, g, h) {
    var inputs = [a, b, c, d, e, f, g, h];
    var parts = [];
    for (let i = 0; i < inputs.length; i++) if (inputs[i]) parts.push("" + inputs[i]);
    var value = "";
    for (let index = 0; index < parts.length; index++) value += (index ? "/" : "") + parts[index];
    return normalize(value);
}
function isAbsolute(path) { return ("" + path)[0] === "/"; }
function resolve(a, b, c, d, e, f, g, h) {
    var inputs = [a, b, c, d, e, f, g, h];
    var value = "";
    for (let i = inputs.length - 1; i >= 0 && !isAbsolute(value); i--) if (inputs[i] !== undefined) value = ("" + inputs[i]) + "/" + value;
    if (!isAbsolute(value)) value = "/" + value;
    return normalize(value);
}
function dirname(path) {
    var value = normalize(path);
    if (value === "/") return "/";
    var index = -1;
    for (let i = 0; i < value.length; i++) if (value[i] === "/") index = i;
    return index < 0 ? "." : (index === 0 ? "/" : value.slice(0, index));
}
function basename(path, suffix) {
    var value = normalize(path);
    var index = -1;
    for (let i = 0; i < value.length; i++) if (value[i] === "/") index = i;
    var name = value.slice(index + 1);
    if (suffix && name.slice(-suffix.length) === suffix) name = name.slice(0, -suffix.length);
    return name;
}
function extname(path) {
    var name = basename(path);
    var index = -1;
    for (let i = 0; i < name.length; i++) if (name[i] === ".") index = i;
    return index <= 0 ? "" : name.slice(index);
}
function relative(from, to) {
    var a = [], b = [], left = normalize(resolve(from)), right = normalize(resolve(to)), segment = "";
    for (let x = 0; x <= left.length; x++) { if (x === left.length || left[x] === "/") { if (segment) a.push(segment); segment = ""; } else segment += left[x]; }
    for (let y = 0; y <= right.length; y++) { if (y === right.length || right[y] === "/") { if (segment) b.push(segment); segment = ""; } else segment += right[y]; }
    var common = 0;
    while (common < a.length && common < b.length && a[common] === b[common]) common++;
    var result = [];
    for (let i = common; i < a.length; i++) result.push("..");
    for (let j = common; j < b.length; j++) result.push(b[j]);
    var value = "";
    for (let k = 0; k < result.length; k++) value += (k ? "/" : "") + result[k];
    return value;
}
module.exports = { normalize: normalize, join: join, resolve: resolve, isAbsolute: isAbsolute, dirname: dirname, basename: basename, extname: extname, relative: relative, sep: "/", delimiter: ":" };
