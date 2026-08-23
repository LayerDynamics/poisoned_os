function text(value) {
    if (typeof value === "string") return value;
    if (value === undefined) return "undefined";
    if (value === null) return "null";
    return JSON.stringify(value);
}
function format(value, a, b, c, d, e, f, g, h) {
    var args = [a, b, c, d, e, f, g, h];
    while (args.length && args[args.length - 1] === undefined) args.splice(args.length - 1, 1);
    if (typeof value !== "string") { var plain = text(value); for (let item = 0; item < args.length; item++) plain += " " + text(args[item]); return plain; }
    var index = 0, output = "";
    for (let cursor = 0; cursor < value.length; cursor++) {
        if (value[cursor] === "%" && cursor + 1 < value.length) {
            var token = value[cursor + 1];
            if (token === "%") { output += "%"; cursor++; continue; }
            if ((token === "s" || token === "d" || token === "j") && index < args.length) { output += token === "j" ? JSON.stringify(args[index]) : text(args[index]); index++; cursor++; continue; }
        }
        output += value[cursor];
    }
    while (index < args.length) { output += " " + text(args[index]); index++; }
    return output;
}
function inspect(value) { return typeof value === "string" ? value : JSON.stringify(value); }
module.exports = { format: format, inspect: inspect, types: { isArrayBuffer: function(value) { return value && value.byteLength !== undefined; } } };
