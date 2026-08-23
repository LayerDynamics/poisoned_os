var storage = require("@flipperdevices/fz-sdk/storage");
var process = require("./process.js");
var PromiseImpl = require("./promise.js").Promise;

function failure(operation, path) { return __poison_fail("fs." + operation + " failed for " + path); }
function readFileSync(path, options) {
    var encoding = options === "utf8" || (options && options.encoding === "utf8") ? "ascii" : "binary";
    var file = storage.openFile(path, "r", "open_existing");
    if (!file) failure("readFileSync", path);
    var result = file.read(encoding, file.size());
    file.close();
    return result;
}
function writeFileSync(path, data) {
    var file = storage.openFile(path, "w", "create_always");
    if (!file) failure("writeFileSync", path);
    file.write(data);
    file.close();
}
function existsSync(path) { return storage.fileOrDirExists(path); }
function mkdirSync(path) {
    if (!storage.makeDirectory(path) && !storage.directoryExists(path)) failure("mkdirSync", path);
}
function readdirSync(path) {
    var entries = storage.readDirectory(path);
    if (!entries) failure("readdirSync", path);
    var result = [];
    for (let i = 0; i < entries.length; i++) result.push(entries[i].path);
    return result;
}
function statSync(path) {
    var info = storage.stat(path);
    if (!info) failure("statSync", path);
    return {
        size: info.size,
        isFile: function() { return !info.isDirectory; },
        isDirectory: function() { return info.isDirectory; },
    };
}
function unlinkSync(path) { if (!storage.remove(path)) failure("unlinkSync", path); }
function renameSync(oldPath, newPath) { if (!storage.rename(oldPath, newPath)) failure("renameSync", oldPath); }
function callbackCall(callback, operation) {
    process.nextTick(function() { callback(null, operation()); });
}
function readFile(path, options, callback) { if (typeof options === "function") { callback = options; options = undefined; } callbackCall(callback, function() { return readFileSync(path, options); }); }
function writeFile(path, data, options, callback) { if (typeof options === "function") { callback = options; } else if (typeof callback !== "function") callback = options; callbackCall(callback, function() { return writeFileSync(path, data); }); }
function stat(path, callback) { callbackCall(callback, function() { return statSync(path); }); }
function readdir(path, callback) { callbackCall(callback, function() { return readdirSync(path); }); }
function mkdir(path, callback) { callbackCall(callback, function() { return mkdirSync(path); }); }
function unlink(path, callback) { callbackCall(callback, function() { return unlinkSync(path); }); }
function rename(oldPath, newPath, callback) { callbackCall(callback, function() { return renameSync(oldPath, newPath); }); }
var promises = {
    readFile: function(path, options) { return PromiseImpl.create(function(resolve) { resolve(readFileSync(path, options)); }); },
    writeFile: function(path, data) { return PromiseImpl.create(function(resolve) { writeFileSync(path, data); resolve(); }); },
    stat: function(path) { return PromiseImpl.create(function(resolve) { resolve(statSync(path)); }); },
    readdir: function(path) { return PromiseImpl.create(function(resolve) { resolve(readdirSync(path)); }); },
    mkdir: function(path) { return PromiseImpl.create(function(resolve) { mkdirSync(path); resolve(); }); },
    unlink: function(path) { return PromiseImpl.create(function(resolve) { unlinkSync(path); resolve(); }); },
    rename: function(oldPath, newPath) { return PromiseImpl.create(function(resolve) { renameSync(oldPath, newPath); resolve(); }); },
};

module.exports = {
    readFileSync: readFileSync,
    writeFileSync: writeFileSync,
    existsSync: existsSync,
    mkdirSync: mkdirSync,
    readdirSync: readdirSync,
    statSync: statSync,
    unlinkSync: unlinkSync,
    renameSync: renameSync,
    readFile: readFile,
    writeFile: writeFile,
    stat: stat,
    readdir: readdir,
    mkdir: mkdir,
    unlink: unlink,
    rename: rename,
    promises: promises,
};
