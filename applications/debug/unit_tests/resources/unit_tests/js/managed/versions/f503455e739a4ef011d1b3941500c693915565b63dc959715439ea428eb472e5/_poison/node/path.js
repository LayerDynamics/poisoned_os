function normalize(path) {
    var absolute = path.charAt(0) === "/";
    var parts = path.split("/");
    var result = [];
    for (var i = 0; i < parts.length; i++) {
        if (!parts[i] || parts[i] === ".") continue;
        if (parts[i] === ".." && result.length && result[result.length - 1] !== "..") result.pop();
        else if (parts[i] !== ".." || !absolute) result.push(parts[i]);
    }
    var value = result.join("/");
    if (absolute) value = "/" + value;
    return value || (absolute ? "/" : ".");
}
function join() {
    var parts = [];
    for (var i = 0; i < arguments.length; i++) if (arguments[i]) parts.push(String(arguments[i]));
    return normalize(parts.join("/"));
}
function isAbsolute(path) { return path.charAt(0) === "/"; }
function resolve() {
    var value = "";
    for (var i = arguments.length - 1; i >= 0 && !isAbsolute(value); i--) value = String(arguments[i]) + "/" + value;
    if (!isAbsolute(value)) value = "/" + value;
    return normalize(value);
}
function dirname(path) {
    var value = normalize(path);
    if (value === "/") return "/";
    var index = value.lastIndexOf("/");
    return index < 0 ? "." : (index === 0 ? "/" : value.slice(0, index));
}
function basename(path, suffix) {
    var value = normalize(path);
    var name = value.slice(value.lastIndexOf("/") + 1);
    if (suffix && name.slice(-suffix.length) === suffix) name = name.slice(0, -suffix.length);
    return name;
}
function extname(path) {
    var name = basename(path);
    var index = name.lastIndexOf(".");
    return index <= 0 ? "" : name.slice(index);
}
function relative(from, to) {
    var a = resolve(from).split("/").filter(Boolean);
    var b = resolve(to).split("/").filter(Boolean);
    var common = 0;
    while (common < a.length && common < b.length && a[common] === b[common]) common++;
    var result = [];
    for (var i = common; i < a.length; i++) result.push("..");
    return result.concat(b.slice(common)).join("/") || "";
}
module.exports = { normalize: normalize, join: join, resolve: resolve, isAbsolute: isAbsolute, dirname: dirname, basename: basename, extname: extname, relative: relative, sep: "/", delimiter: ":" };
