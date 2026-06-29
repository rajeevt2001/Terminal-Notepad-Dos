//compiled with borland tcc compiler , made by rajeev tiwari with the help of google gemini
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <io.h>

/* Safely check the CPU level using Intel Flag bits */
int get_cpu_type() {
    asm pushf;
    asm pop ax;
    asm mov cx, ax;
    asm and ax, 0x0FFF;
    asm push ax;
    asm popf;
    asm pushf;
    asm pop ax;
    asm and ax, 0xF000;
    asm cmp ax, 0xF000;
    asm je is_8086;
    
    asm mov bx, 386;
    asm jmp done;
    
is_8086:
    asm mov bx, 86;
    
done:
    asm push cx;
    asm popf;
    
    return _BX; 
}

int main(int argc, char* argv[]) {
    FILE *f, *out;
    long file_size, offset_16, offset_32, size_16, size_32;
    char magic[5];
    char temp_name[] = "~NTERUN.EXE"; /* Hidden temporary file */
    int cpu = get_cpu_type();
    char buffer[1024];
    int bytes_read;
    long bytes_to_copy;
    int chunk;

    (void)argc; /* FIX: Suppresses the unused variable warning! */

    /* Open our own .EXE file to read the attached payloads */
    f = fopen(argv[0], "rb");
    if (!f) {
        printf("Error: Cannot read archive.\n");
        return 1;
    }

    /* Jump to the very end of the file to read our custom 12-byte footer */
    fseek(f, -12, SEEK_END);
    file_size = ftell(f) + 12;
    fread(&size_16, sizeof(long), 1, f);
    fread(&size_32, sizeof(long), 1, f);
    fread(magic, 1, 4, f);
    magic[4] = '\0';

    if (strcmp(magic, "FATB") != 0) {
        printf("Error: Archive corrupted.\n");
        fclose(f);
        return 1;
    }

    /* Calculate where the internal files begin */
    offset_16 = file_size - 12 - size_32 - size_16;
    offset_32 = file_size - 12 - size_32;

    out = fopen(temp_name, "wb");
    if (!out) {
        printf("Error: Cannot allocate memory.\n");
        fclose(f);
        return 1;
    }

    /* Seek to the correct internal file based on the CPU */
    if (cpu >= 386) {
        fseek(f, offset_32, SEEK_SET);
        bytes_to_copy = size_32;
    } else {
        fseek(f, offset_16, SEEK_SET);
        bytes_to_copy = size_16;
    }

    /* Stream the internal file out to the hard drive */
    while (bytes_to_copy > 0) {
        chunk = (bytes_to_copy > sizeof(buffer)) ? sizeof(buffer) : (int)bytes_to_copy;
        bytes_read = fread(buffer, 1, chunk, f);
        if (bytes_read <= 0) break;
        fwrite(buffer, 1, bytes_read, out);
        bytes_to_copy -= bytes_read;
    }

    fclose(out);
    fclose(f);

    /* Hand control over to the extracted editor */
    spawnl(P_WAIT, temp_name, temp_name, argv[1], NULL);

    /* Clean up the tracks when the user exits the editor! */
    remove(temp_name);
    return 0;
}