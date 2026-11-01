// Dumps the decrypted, decompressed image and its sections from a XEX2 using XenonUtils.
#include <image.h>
#include <file.h>
#include <cstdio>
int main(int argc, char** argv) {
    auto file = LoadFile(argv[1]);
    if (file.empty()) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    Image img = Image::ParseImage(file.data(), file.size());
    if (!img.data) { fprintf(stderr, "parse failed\n"); return 1; }
    printf("base=%zx size=%x entry=%zx\n", img.base, img.size, img.entry_point);
    for (auto& s : img.sections)
        printf("section %-8s %zx-%zx flags=%x\n", s.name.c_str(), s.base, s.base + s.size, s.flags);
    FILE* f = fopen(argv[2], "wb"); fwrite(img.data.get(), 1, img.size, f); fclose(f);
}
