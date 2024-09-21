package org.nativescript.runtime.napi;

public interface ExtractPolicy {
    boolean shouldExtract(android.content.Context context);

    boolean forceOverwrite();

    FileExtractor extractor();
}
