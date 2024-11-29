exports.runImportTests = function() {
    require("./Import");
}

exports.runRequireTests = function() {
    require("./Require");
}

exports.runWeakRefTests = function() {
    require("./WeakRef");
}

exports.runRuntimeTests = function() {
    require("./RuntimeTests");
}

exports.runWorkerTests = function() {
    require("./Workers");
}

exports.runAllTests = function() {
    exports.runImportTests();
    exports.runRequireTests();
    exports.runWeakRefTests();
    exports.runRuntimeTests();
    exports.runWorkerTests();
}
