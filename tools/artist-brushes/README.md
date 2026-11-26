# Artist-check brush fixtures

`generate.cpp` writes the seven `.kb` files in `docs/testing/brushes`. They are
human-test material, not product defaults. The Windows ZIP copies them into
`Human-Test/Brushes`.

Rebuild them from the repository root with:

```sh
g++ -std=c++20 -O2 -Isrc -o /tmp/generate-artist-brushes \
    tools/artist-brushes/generate.cpp \
    src/brush/BrushModel.cpp src/brush/BrushParameters.cpp \
    src/brush/BrushTypes.cpp src/brush/BrushFormat.cpp src/brush/BrushFile.cpp
/tmp/generate-artist-brushes docs/testing/brushes
```

The settings in the table are the exact values in the saved documents. The
test `test_brush_ui` opens these same files, so a fixture change is exercised by
CI before it reaches the human build.
