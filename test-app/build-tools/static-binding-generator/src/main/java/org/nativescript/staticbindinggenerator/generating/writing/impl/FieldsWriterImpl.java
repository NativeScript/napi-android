package org.nativescript.staticbindinggenerator.generating.writing.impl;

import org.nativescript.staticbindinggenerator.Writer;
import org.nativescript.staticbindinggenerator.generating.writing.FieldsWriter;

public class FieldsWriterImpl implements FieldsWriter {


    private static final String THIZ_KEYWORD = "thiz";
    private static final String RUNTIME_ID_KEYWORD = "runtimeId";

    private final Writer writer;

    public FieldsWriterImpl(final Writer writer) {
        this.writer = writer;
    }

    @Override
    public void writeStaticThizField(String className) {
        writer.write(TABULATION_LITERAL);
        writer.write(PRIVATE_MODIFIER);
        writer.write(SPACE_LITERAL);
        writer.write(STATIC_MODIFIER);
        writer.write(SPACE_LITERAL);
        writer.write(className);
        writer.write(SPACE_LITERAL);
        writer.write(THIZ_KEYWORD);
        writer.writeln(END_OF_STATEMENT_LITERAL);
        writer.writeln();
    }

    @Override
    public void writePublicRuntimeIdField() {
        writer.write(TABULATION_LITERAL);
        writer.write(PUBLIC_MODIFIER);
        writer.write(SPACE_LITERAL);
        writer.write("int");
        writer.write(SPACE_LITERAL);
        writer.write(RUNTIME_ID_KEYWORD);
        writer.write(SPACE_LITERAL);
        writer.write("=");
        writer.write(SPACE_LITERAL);
        writer.write("0");
        writer.writeln(END_OF_STATEMENT_LITERAL);
        writer.writeln();
    }
}
