function parse(value) {
    var text = "" + value;
    var protocol = "", authority = "", pathname = text, search = "", hash = "";
    var protocolEnd = text.indexOf(":");
    if (protocolEnd >= 0) { protocol = text.slice(0, protocolEnd + 1); pathname = text.slice(protocolEnd + 1); }
    if (pathname.slice(0, 2) === "//") {
        pathname = pathname.slice(2);
        var authorityEnd = pathname.indexOf("/");
        if (authorityEnd < 0) { authority = pathname; pathname = ""; }
        else { authority = pathname.slice(0, authorityEnd); pathname = pathname.slice(authorityEnd); }
    }
    var hashStart = pathname.indexOf("#");
    if (hashStart >= 0) { hash = pathname.slice(hashStart); pathname = pathname.slice(0, hashStart); }
    var searchStart = pathname.indexOf("?");
    if (searchStart >= 0) { search = pathname.slice(searchStart); pathname = pathname.slice(0, searchStart); }
    var at = authority.indexOf("@");
    var host = at >= 0 ? authority.slice(at + 1) : authority;
    var colon = host.indexOf(":");
    var port = colon > -1 ? host.slice(colon + 1) : "";
    return { protocol: protocol, auth: at >= 0 ? authority.slice(0, at) : null, host: host, hostname: colon > -1 ? host.slice(0, colon) : host, port: port, pathname: pathname, search: search, hash: hash, href: text };
}
function format(parts) { return (parts.protocol || "") + (parts.host ? "//" + parts.host : "") + (parts.pathname || "") + (parts.search || "") + (parts.hash || ""); }
module.exports = { parse: parse, format: format, URL: parse };
