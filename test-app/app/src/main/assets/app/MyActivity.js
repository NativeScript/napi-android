/*

    // demonstrates how to extend class in TypeScript with prebuilt Java proxy
	declare module android {
		export module app {
			export class Activity {
				onCreate(bundle: android.os.Bundle);
			}
		}
		export module os {
			export class Bundle {}
		}
	}

	@JavaProxy("com.tns.NativeScriptActivity")
	class MyActivity extends android.app.Activity
	{
		onCreate(bundle: android.os.Bundle)
		{
			super.onCreate(bundle);
		}
	}
*/
var MyActivity = (function (_super) {
  __extends(MyActivity, _super);
  function MyActivity() {
    _super.apply(this, arguments);
  }
  MyActivity.prototype.onCreate = function (bundle) {
    _super.prototype.onCreate.call(this, bundle);
    // Launch mode is selected by an intent extra so we don't have to rebuild to
    // switch between the jasmine regression suite and the perf benchmark:
    //   default            -> run the jasmine test suite (regression check)
    //   --ez bench true     -> run the marshalling benchmark in isolation
    var runBench = false;
    try {
      runBench = this.getIntent().getBooleanExtra("bench", false);
    } catch (e) {}
    if (!runBench) {
      require('./tests/testsWithContext').run(this);
      // run jasmine
      execute();
    }
    var layout = new android.widget.LinearLayout(this);
    layout.setOrientation(1);
    this.setContentView(layout);
    var textView = new android.widget.TextView(this);
    textView.setText("It's a button!");
    textView.setTextIsSelectable(true);
    layout.addView(textView);

    var button = new android.widget.Button(this);
    button.setText("Hit me");
    layout.addView(button);

    var button2 = new android.widget.Button(this);
    button2.setText("Run Benchmark");
    layout.addView(button2);

    var button3 = new android.widget.Button(this);
    button3.setText("Run Marshalling Benchmark");
    layout.addView(button3);

    var Color = android.graphics.Color;
    var colors = [
      Color.BLUE,
      Color.RED,
      Color.MAGENTA,
      Color.YELLOW,
      Color.parseColor("#FF7F50"),
    ];
    var taps = 0;

    var dum = com.tns.tests.DummyClass.null;

    button.setOnClickListener(
      new android.view.View.OnClickListener("AppClickListener", {
        onClick: function () {
          button.setBackgroundColor(colors[taps % colors.length]);
          taps++;
        },
      })
    );
    var benchmarkWorker = null;
    button2.setOnClickListener(
          new android.view.View.OnClickListener("AppClickListener", {
            onClick: function () {
              // Run the benchmark on a worker thread so the UI thread stays
              // responsive and Android does not raise an ANR.
              if (benchmarkWorker) {
                return;
              }
              button2.setText("Running Benchmark...");
              textView.setText("Running benchmark, please wait...");

              benchmarkWorker = new Worker("./benchmark-worker.js");
              benchmarkWorker.onmessage = function (msg) {
                textView.setText(msg.data);
                button2.setText("Run Benchmark");
                benchmarkWorker.terminate();
                benchmarkWorker = null;
              };
              benchmarkWorker.onerror = function (err) {
                textView.setText("Benchmark error: " + (err && err.message ? err.message : err));
                button2.setText("Run Benchmark");
                benchmarkWorker.terminate();
                benchmarkWorker = null;
              };
              benchmarkWorker.postMessage("start");
            },
          })
    );
    var marshallingWorker = null;
    button3.setOnClickListener(
          new android.view.View.OnClickListener("AppClickListener", {
            onClick: function () {
              // Run the marshalling benchmark on a worker thread so the UI thread
              // stays responsive and Android does not raise an ANR.
              if (marshallingWorker) {
                return;
              }
              button3.setText("Running Marshalling Benchmark...");
              textView.setText("Running marshalling benchmark, please wait...");

              marshallingWorker = new Worker("./marshalling-benchmark-worker.js");
              marshallingWorker.onmessage = function (msg) {
                textView.setText(msg.data);
                button3.setText("Run Marshalling Benchmark");
                marshallingWorker.terminate();
                marshallingWorker = null;
              };
              marshallingWorker.onerror = function (err) {
                textView.setText("Marshalling benchmark error: " + (err && err.message ? err.message : err));
                button3.setText("Run Marshalling Benchmark");
                marshallingWorker.terminate();
                marshallingWorker = null;
              };
              marshallingWorker.postMessage("start");
            },
          })
    );

    // Report Time To Interactive: process start -> first activity fully built.
    require("./tti").reportTTI("app launch");

    // Benchmark mode (--ez bench true): auto-run the marshalling benchmark on
    // launch. Results stream to logcat tagged NS_ENGINE_BENCHMARK.
    if (runBench) {
      var w = new Worker("./marshalling-benchmark-worker.js");
      w.onmessage = function (msg) {
        textView.setText(msg.data);
        w.terminate();
      };
      w.onerror = function (err) {
        console.log("NS_ENGINE_BENCHMARK_DONE error=" + (err && err.message ? err.message : err));
        w.terminate();
      };
      w.postMessage("start");
    }
  };
  MyActivity = __decorate(
    [JavaProxy("com.tns.NativeScriptActivity")],
    MyActivity
  );
  return MyActivity;
})(android.app.Activity);
