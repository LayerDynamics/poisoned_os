var PromiseImpl = require("promise").Promise;
var timers = require("timers");
var value = 0;
PromiseImpl.resolve(7).then(function(result) { value = result; });
timers.setTimeout(function() {
    if(value !== 7) throw new Error("promise callback did not run");
    print("managed async ok");
}, 20);
