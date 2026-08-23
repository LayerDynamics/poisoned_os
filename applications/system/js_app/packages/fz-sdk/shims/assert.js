function fail(message) {
    return __poison_fail(message || "Assertion failed");
}

function same(a, b) {
    if (a === b) return true;
    if (!a || !b || typeof a !== "object" || typeof b !== "object") return false;
    var ak = [];
    var bk = [];
    for (let akey in a) ak.push(akey);
    for (let bkey in b) bk.push(bkey);
    if (ak.length !== bk.length) return false;
    for (let i = 0; i < ak.length; i++) {
        if (b[ak[i]] === undefined || !same(a[ak[i]], b[ak[i]])) return false;
    }
    return true;
}

function ok(value, message) { if (!value) fail(message); }
function strictEqual(actual, expected, message) { if (actual !== expected) fail(message || "Values are not strictly equal"); }
function notStrictEqual(actual, expected, message) { if (actual === expected) fail(message || "Values are strictly equal"); }
function deepStrictEqual(actual, expected, message) { if (!same(actual, expected)) fail(message || "Values are not deeply equal"); }
function notDeepStrictEqual(actual, expected, message) { if (same(actual, expected)) fail(message || "Values are deeply equal"); }

module.exports = {
    ok: ok,
    assert: ok,
    fail: fail,
    strictEqual: strictEqual,
    notStrictEqual: notStrictEqual,
    deepStrictEqual: deepStrictEqual,
    notDeepStrictEqual: notDeepStrictEqual,
};
