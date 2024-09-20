package org.nativescript.runtime.napi;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

import org.nativescript.runtime.napi.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {



    // Used to load the 'napi' library on application startup.
    static {
        System.loadLibrary("napi-runtime");
    }

    private ActivityMainBinding binding;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        callFromJS();

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        // Example of a call to a native method
        TextView tv = binding.sampleText;
        tv.setText(stringFromJNI());
    }

    public int number;

    public void javaMethod() {
        ++number;
//        Log.d("BRIDGE", "Called from QuickJS");
    }


    /**
     * A native method that is implemented by the 'napi' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
    public native void callFromJS();
}