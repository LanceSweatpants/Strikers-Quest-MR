package io.github.newcoke.strikers.guests;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * Our own guest data that ships in the APK (assets/guestdata: bone maps, picks, colours, stats, the voice clip
 * list, shiny colour maps ...): numbers, names and our own art only, never another game's pixels, vertices or
 * sound. On the headset it comes from the APK's assets, on the PC from the same folder in the source tree.
 */
public interface Data {
    byte[] read(String name) throws IOException;

    boolean has(String name);

    default String text(String name) throws IOException {
        return new String(read(name), StandardCharsets.UTF_8);
    }

    /** The source tree's assets/guestdata (the PC checks). */
    final class Dir implements Data {
        private final File root;

        public Dir(File root) {
            this.root = root;
        }

        @Override
        public byte[] read(String name) throws IOException {
            File f = new File(root, name);
            if (!f.isFile()) throw new FileNotFoundException("guestdata/" + name);
            return Files.readAllBytes(f.toPath());
        }

        @Override
        public boolean has(String name) {
            return new File(root, name).isFile();
        }
    }
}
