var process = require("./process.js");
function MiniPromise(executor) {
    var self = {};
    var state = "pending", value, handlers = [];
    function settle(nextState, nextValue) {
        if (state !== "pending") return;
        state = nextState; value = nextValue;
        process.nextTick(function() { var pending = handlers.splice(0, handlers.length); for (var i = 0; i < pending.length; i++) handle(pending[i]); });
    }
    function handle(handler) {
        if (state === "pending") { handlers.push(handler); return; }
        var callback = state === "fulfilled" ? handler.onFulfilled : handler.onRejected;
        if (!callback) { (state === "fulfilled" ? handler.resolve : handler.reject)(value); return; }
        handler.resolve(callback(value));
    }
    function resolve(nextValue) { if (nextValue && typeof nextValue.then === "function") nextValue.then(resolve, reject); else settle("fulfilled", nextValue); }
    function reject(error) { settle("rejected", error); }
    self.then = function(onFulfilled, onRejected) { return MiniPromise(function(resolveNext, rejectNext) { handle({ onFulfilled: onFulfilled, onRejected: onRejected, resolve: resolveNext, reject: rejectNext }); }); };
    self["catch"] = function(onRejected) { return self.then(null, onRejected); };
    self["finally"] = function(callback) { return self.then(function(value) { callback(); return value; }, function(error) { callback(); return MiniPromise.reject(error); }); };
    executor(resolve, reject);
    return self;
}
MiniPromise.resolve = function(value) { return MiniPromise(function(resolve) { resolve(value); }); };
MiniPromise.reject = function(error) { return MiniPromise(function(_, reject) { reject(error); }); };
MiniPromise.all = function(values) { return MiniPromise(function(resolve, reject) { var result = [], left = values.length; if (!left) { resolve(result); return; } for(var index = 0; index < values.length; index++) (function(position) { MiniPromise.resolve(values[position]).then(function(item) { result[position] = item; if (!--left) resolve(result); }, reject); })(index); }); };
module.exports = { Promise: MiniPromise };
