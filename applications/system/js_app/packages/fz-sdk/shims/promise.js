var process = require("./process.js");
function createPromise(executor) {
    var self = {};
    var state = "pending", value, handlers = [];
    var handlersScheduled = false;
    function scheduleHandlers() {
        if (handlersScheduled) return undefined;
        handlersScheduled = true;
        process.nextTick(function() {
            handlersScheduled = false;
            var pending = handlers.splice(0, handlers.length);
            for (let i = 0; i < pending.length; i++) handle(pending[i]);
        });
    }
    function settle(nextState, nextValue) {
        if (state !== "pending") return undefined;
        state = nextState; value = nextValue;
        scheduleHandlers();
    }
    function handle(handler) {
        if (state === "pending") { handlers.push(handler); return undefined; }
        var callback = state === "fulfilled" ? handler.onFulfilled : handler.onRejected;
        if (!callback) { (state === "fulfilled" ? handler.resolve : handler.reject)(value); return undefined; }
        handler.resolve(callback(value));
    }
    function resolve(nextValue) {
        var nextType = typeof nextValue;
        var canHaveThen = nextValue && (nextType === "object" || nextType === "function");
        if (canHaveThen && typeof nextValue.then === "function") {
            nextValue.then(resolve, reject);
        } else {
            settle("fulfilled", nextValue);
        }
    }
    function reject(error) { settle("rejected", error); }
    self.then = function(onFulfilled, onRejected) {
        return createPromise(function(resolveNext, rejectNext) {
            handlers.push({ onFulfilled: onFulfilled, onRejected: onRejected, resolve: resolveNext, reject: rejectNext });
            if (state !== "pending") scheduleHandlers();
        });
    };
    self["catch"] = function(onRejected) { return self.then(null, onRejected); };
    self["finally"] = function(callback) { return self.then(function(value) { callback(); return value; }, function(error) { callback(); return PromiseApi.reject(error); }); };
    executor(resolve, reject);
    return self;
}
function resolvePromise(value) { return createPromise(function(resolve) { resolve(value); }); }
function rejectPromise(error) { return createPromise(function(_, reject) { reject(error); }); }
function allPromises(values) { return createPromise(function(resolve, reject) { var result = [], left = values.length; if (!left) { resolve(result); return undefined; } for(let index = 0; index < values.length; index++) (function(position) { resolvePromise(values[position]).then(function(item) { result[position] = item; if (!--left) resolve(result); }, reject); })(index); }); }
var PromiseApi = { create: createPromise, resolve: resolvePromise, reject: rejectPromise, all: allPromises };
module.exports = { Promise: PromiseApi };
