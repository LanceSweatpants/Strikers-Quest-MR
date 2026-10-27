package io.github.newcoke.strikers.guests;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.file.Files;

/**
 * A game's files from folders (the PC checks: the files the PC tools pulled off the same discs). A disc path is
 * looked for as it is under each folder, then by its name alone (some folders hold the files flat).
 */
public final class DirSource implements Source {
    private final String id;
    private final File[] roots;

    public DirSource(String id, File... roots) {
        this.id = id;
        this.roots = roots;
    }

    @Override
    public String id() {
        return id;
    }

    private File find(String path) {
        final String name = path.substring(path.lastIndexOf('/') + 1);
        for (File r : roots) {
            File f = new File(r, path);
            if (f.isFile()) return f;
        }
        for (File r : roots) {
            File f = new File(r, name);
            if (f.isFile()) return f;
        }
        return null;
    }

    @Override
    public boolean has(String path) {
        return find(path) != null;
    }

    @Override
    public byte[] read(String path) throws IOException {
        File f = find(path);
        if (f == null) throw new FileNotFoundException(id + ": " + path);
        return Files.readAllBytes(f.toPath());
    }
}
