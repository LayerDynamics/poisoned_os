var value = require("tiny-value");
if(value !== 7) throw new Error("bare dependency failed");
print("managed commonjs ok");
