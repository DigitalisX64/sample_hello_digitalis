package com.example.helloobjectbox;

import io.objectbox.annotation.Entity;
import io.objectbox.annotation.Id;

/**
 * A persisted object. The ObjectBox annotation processor reads @Entity / @Id at
 * build time and generates the matching MyObjectBox schema, a Note_ metadata
 * class, and a Cursor that the native store uses to read/write instances.
 *
 * It is a plain Java class rather than a Kotlin data class so the JSR-269
 * ObjectBox processor runs through the standard javac {@code annotationProcessor}
 * path — AGP 9's built-in Kotlin support provides no kapt for processing Kotlin
 * sources.
 */
@Entity
public class Note {
    @Id
    public long id;
    public String text;
    public int num;

    public Note() {}

    public Note(String text, int num) {
        this.text = text;
        this.num = num;
    }
}
