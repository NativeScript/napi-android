package org.nativescript.runtime.napi;

import androidx.appcompat.app.AppCompatActivity;

import android.content.Context;
import android.os.Bundle;
import android.util.Log;

import org.nativescript.runtime.napi.databinding.ActivityMainBinding;

import java.io.File;

public class MainActivity extends AppCompatActivity {
    Runtime m_runtime;



    // Used to load the 'napi' library on application startup.
    static {
        System.loadLibrary("napi-runtime");
    }

    private ActivityMainBinding binding;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LogcatLogger logger = new LogcatLogger(getApplicationContext());

        DefaultExtractPolicy extractPolicy = new DefaultExtractPolicy(logger);

        Context ctx = getApplicationContext();
        String appDir = null;
        try {
            appDir = getApplicationContext().getFilesDir().getCanonicalPath();

            AssetExtractor aE = new AssetExtractor(null, logger);

            String outputDir = ctx.getFilesDir().getPath() + File.separator;
            aE.extractAssets(ctx, "metadata", outputDir, extractPolicy, false);
            aE.extractAssets(ctx, "app", outputDir, extractPolicy, true);

        } catch (Exception e) {

        }

        if (appDir != null) {
            m_runtime = new Runtime();
            m_runtime.logger = new LogcatLogger(this.getApplicationContext());
            m_runtime.startRuntimeBridge(appDir);
        }

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
    }

    public int number;

    public void javaMethod() {
        ++number;
//        Log.d("BRIDGE", "Called from QuickJS");
    }

}