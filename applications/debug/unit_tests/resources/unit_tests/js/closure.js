let tests = require("tests");

let make_reader = function(value) {
    return function() {
        return value;
    };
};

let first_reader = make_reader(7);
let second_reader = make_reader(11);
tests.assert_eq(7, first_reader());
tests.assert_eq(11, second_reader());

let make_counter = function() {
    let value = 0;
    return function() {
        value++;
        return value;
    };
};

let counter = make_counter();
tests.assert_eq(1, counter());
tests.assert_eq(2, counter());

let captured = 3;
let read_captured = function() {
    return captured;
};
let invoke_with_shadow = function(callback) {
    let captured = 99;
    return callback();
};
tests.assert_eq(3, invoke_with_shadow(read_captured));
