__disableVerboseLogging();
__log("starting tests");

// methods that common tests need to run
var testContent = "";
__collect = gc;
TNSClearOutput = function () {
	testContent = "";
}
TNSLog = function (text) {
	testContent += text;
}
TNSGetOutput = function () {
	return testContent;
}
__approot = __dirname.substr(0, __dirname.length - 4);


// PASSING TESTS
//require("./tests/testMetadata");
//require("./tests/testWeakRef");
//require("./tests/testMethodResolution");
//require("./tests/testMethodResolutionWithNulls");
//require("./tests/testInterfaceStaticMethods");
//require("./tests/testInterfaceDefaultMethods");
//require("./tests/testFieldGetSet");
//require("./tests/field-access-test");
//require("./tests/java-array-test");
//require("./tests/byte-buffer-test");
//require("./tests/testInterfaceImplementation");
//require("./tests/console/logTests.js");
//require("./tests/dex-interface-implementation");
//require("./tests/kotlin/properties/testPropertiesSupport");
//require("./tests/kotlin/delegation/testDelegationSupport");
//require("./tests/kotlin/objects/testObjectsSupport");
//require("./tests/kotlin/functions/testTopLevelFunctionsSupport");
//require("./tests/kotlin/extensions/testExtensionFunctionsSupport");
//require("./tests/kotlin/enums/testEnumsSupport");
//require("./tests/kotlin/access/testInternalLanguageFeaturesSupport");
//require('./tests/testNativeTimers');
//require("./tests/testPackagePrivate");
//require("./tests/kotlin/properties/testPropertiesSupport.js");
//require("./tests/inheritanceChainResolutionTest");
//require("./tests/testRuntimeImplementedAPIs");
//require("./tests/testPrimitiveTypeConversion");
//require("./tests/numericConversionTests");
//require("./tests/testAsserts");
//require("./tests/stringConversionTests");
//require("./tests/testsInstanceOfOperator");
//require("./tests/extendedClassesTests");
//require("./tests/testPostFrameCallback");
//require("./tests/testsForRuntimeBindingGenerator");
//require("./tests/discardedExceptionsTest");
//require("./tests/requireExceptionTests");
//require("./tests/exceptionHandlingTests");

//require("./tests/tests");

//require("./tests/testsForTypescript");

//require("./tests/testGC");

//require("./tests/testReleaseNativeCounterpart");

//require("./tests/testArrays");

//require("./tests/testJniReferenceLeak");

//var shared = require("./shared");
//shared.runRequireTests();
//shared.runRuntimeTests();

//shared.runWorkerTests();

//require("./tests/dispatchCallbacksOnUiThreadTests");
//require("./tests/testsMemoryManagement");

//shared.runWeakRefTests();

//require("./tests/testWebAssembly");
////require("./tests/extendClassNameTests"); // as tests now run with SBG, this test fails the whole build process
//require("./tests/testNativeModules");
//require('./tests/testURLImpl.js');
//require('./tests/testURLSearchParamsImpl.js');

require("./tests/testMultithreadedJavascript"); //FAILS

