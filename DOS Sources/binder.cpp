//compiled with borland tcc compiler , made by rajeev tiwari with the help of google gemini
#include <stdio.h>
#include <stdlib.h>

void append_file(FILE* out, const char* name, long* size_out) {
    FILE* in = fopen(name, "rb");
    char buffer[1024];
    int bytes;
    long total = 0;
    
    if (!in) { printf("Missing %s!\n", name); exit(1); }
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        fwrite(buffer, 1, bytes, out);
        total += bytes;
    }
    fclose(in);
    *size_out = total;
}

int main() {
    FILE* out;
    long size_16 = 0, size_32 = 0, dump_size = 0;

    /* Output the universal launcher as NOTEPAD.EXE */
    out = fopen("NOTEPAD.EXE", "wb");
    if (!out) return 1;

    printf("Packing nloader.exe...\n");
    append_file(out, "nloader.exe", &dump_size);

    printf("Packing NOTE16.EXE...\n");
    append_file(out, "NOTE16.EXE", &size_16);

    /* Grab the renamed 32-bit executable */
    printf("Packing NOTE32.EXE...\n");
    append_file(out, "NOTE32.EXE", &size_32);

    printf("Writing Footer Indices...\n");
    fwrite(&size_16, sizeof(long), 1, out);
    fwrite(&size_32, sizeof(long), 1, out);
    fwrite("FATB", 1, 4, out);

    fclose(out);
    printf("Success! The Universal NOTEPAD.EXE is ready.\n");
    return 0;
}