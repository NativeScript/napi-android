// demonstrates how to extend class in JavaScript with prebuilt Java proxy

var MyApp = android.app.Application.extend("com.tns.NativeScriptApplication", {
  onCreate: function () {
    console.log("Hello MyApp::onCreate()");
  },
});
