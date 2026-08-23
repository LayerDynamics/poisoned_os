var process = require("./process.js");
function MiniPromise(executor) {
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
        try { handler.resolve(callback(value)); } catch (error) { handler.reject(error); }
    }
    function resolve(nextValue) { if (nextValue && typeof nextValue.then === "function") nextValue.then(resolve, reject); else settle("fulfilled", nextValue); }
    function reject(error) { settle("rejected", error); }
    this.then = function(onFulfilled, onRejected) { return new MiniPromise(function(resolveNext, rejectNext) { handle({ onFulfilled: onFulfilled, onRejected: onRejected, resolve: resolveNext, reject: rejectNext }); }); };
    this.catch = function(onRejected) { return this.then(null, onRejected); };
    this.finally = function(callback) { return this.then(function(value) { callback(); return value; }, function(error) { callback(); throw error; }); };
    try { executor(resolve, reject); } catch (error) { reject(error); }
}
MiniPromise.resolve = function(value) { return new MiniPromise(function(resolve) { resolve(value); }); };
MiniPromise.reject = function(error) { return new MiniPromise(function(_, reject) { reject(error); }); };
MiniPromise.all = function(values) { return new MiniPromise(function(resolve, reject) { var result = [], left = values.length; if (!left) { resolve(result); return; } values.forEach(function(value, index) { MiniPromise.resolve(value).then(function(item) { result[index] = item; if (!--left) resolve(result); }, reject); }); }); };
module.exports = { Promise: MiniPromise };
