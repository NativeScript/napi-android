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
        File rootDir = new File(this.getApplicationInfo().dataDir);
        String appDir = null;
        try {
            appDir = getApplicationContext().getFilesDir().getCanonicalPath();

            AssetExtractor aE = new AssetExtractor(null, logger);

            String outputDir = ctx.getFilesDir().getPath() + File.separator;
            aE.extractAssets(ctx, "metadata", outputDir, extractPolicy, false);
            aE.extractAssets(ctx, "app", outputDir, extractPolicy, true);
        } catch (Exception e) {

        }

        try {
            if (appDir != null) {
                Module.init(logger, rootDir, getApplicationContext().getFilesDir());
                m_runtime = new Runtime(this.getApplication(), logger);
                m_runtime.startRuntimeBridge(appDir);
            }
        } catch (Exception e) { }

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
    }

    public int number;

    public void javaMethod() {
        ++number;
//        Log.d("BRIDGE", "Called from QuickJS");
    }

}