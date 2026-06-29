//compiled with borland tcc compiler with -ml optimizations, made by rajeev tiwari with the help of google gemini
#include <dos.h>
#include <dir.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Downgraded to 16-Bit Real Mode (Turbo C++)
#include <alloc.h>

/* 16-bit tick counter macro */
#define GET_TICKS() (*(volatile unsigned long far *)MK_FP(0x0040, 0x006C))

/* --- Macros & Constants --- */
#define bool int
#define MOUSE_INT 0x33
#define INIT_MOUSE 0x00
#define SHOW_MOUSE 0x01
#define HIDE_MOUSE 0x02
#define GET_MOUSE_STATUS 0x03
#define VIDEO_ADDRESS 0xB800

#define INITIAL_LINES 100

#define CURSOR_CHAR 0x0D

/* --- Software Mouse State --- */
char use_software_cursor = 0;
char software_cursor_visible = 0;
int saved_mouse_x = -1;
int saved_mouse_y = -1;
unsigned char saved_char = ' ';
unsigned char saved_attr = 0x07;

/* --- Dynamic Screen Globals --- */
int SCREEN_COLS = 80;
int SCREEN_ROWS = 25;
int font_height = 16;
int video_mode = 3;
unsigned int video_segment = 0xB800; /* 16-Bit Memory Segment Pointer */

/* --- Global Editor State --- */
long full_file_chars = 0;
/* --- Disk Paging & Memory Engine --- */
char** text_buffer;
long current_chunk_start_log = 0;
int CHUNK_LOG_SIZE = 100; /* FIX: Drastically lowered to prevent 64KB VisMap crashes! */

/* --- Piece Table Architecture (Disk Streaming Mode) --- */
#define SOURCE_DISK 0
#define SOURCE_RAM  1

typedef struct {
    char source;     /* SOURCE_DISK or SOURCE_RAM (Optimized) */
    long offset;     /* Starting byte offset in the specific source */
    long length;     /* Number of characters in this piece */
} Piece;

Piece* piece_table = NULL;
int num_pieces = 0;
int piece_capacity = 0;

/* The Append-Only RAM Bucket for new edits */
char* add_buffer = NULL;
long add_buffer_size = 0;
long add_buffer_capacity = 0;

/* Fast Vertical Scrolling Index */
typedef struct {
    long global_offset; /* Exact character offset from the very start of the document where this line begins */
} LineIndex;

LineIndex* stream_lines = NULL;
int total_stream_lines = 0;
int stream_line_capacity = 0;

FILE* stream_file_handle = NULL;

 
int total_lines = 0;
int buffer_capacity = INITIAL_LINES;

long cursor_x = 0; /* Logical X (0-based) */
long cursor_y = 0; /* Logical Y (0-based) */
long scroll_x = 0; /* Horizontal Scroll Offset */
long scroll_y = 0; /* Vertical Scroll Offset */

volatile int mouse_x = 0;
volatile int mouse_y = 0;
volatile char mouse_left = 0;
volatile char mouse_right = 0;

/* --- Advanced Features State --- */
char active_menu = 0;
int active_menu_hover = -1; /* Tracks keyboard selection index in menus */
int active_tab_element = 0; /* Tracks keyboard Tab navigation in dialogs */
char show_context_menu = 0;
int ctx_x = 0, ctx_y = 0;

char show_exit_dialog = 0;
char show_find_dialog = 0;
char* find_text;
char* replace_text;
int find_cursor = 0;
int replace_cursor = 0;
char is_document_dirty = 0;
char last_was_typing = 0; /* For smart Undo grouping */
char* current_filename;
char is_file_truncated = 0;
char* exe_dir; /* Stores the directory where the .exe resides */

char has_selection = 0;
char is_dragging = 0;
long sel_start_x = 0;
long sel_start_y = 0;

char* internal_clipboard = NULL;

/* --- Settings State (RESTORED FOR CONFIG.TXT) --- */
char show_settings = 0;
char setting_hover_effects = 1;
char setting_ansi_mode = 1;
char setting_color = 1;
char setting_bg_color = 0;
char setting_fg_color = 7;
char setting_sel_color = 1;
char setting_crit_color = 4;
char setting_disk_stream = 1; 
char setting_read_only = 0;
char use_mouse = 1;
char running = 1;

/* --- Dynamic Layout Engine (Word Wrap) --- */
char setting_word_wrap = 1;
char setting_respect_words = 1;
char setting_h_scrollbar = 0;
char setting_v_scrollbar = 1;
char needs_recalc = 1;
char needs_ui_redraw = 1;
int last_hover_id = 0;
char force_cursor_snap = 1; /* Decoupled Camera Engine */

typedef struct {
    int log_y;
    int log_x;
    int len;
} VisualLine;

VisualLine* vis_map = NULL;
int vis_lines = 0;
int vis_cap = 0;
long vis_cx = 0, vis_cy = 0;
long max_line_len = 0; /* Longest line length for Horizontal Scroll */

/* --- File Browser State --- */
typedef struct {
    char name[128];
    char is_dir;     /* Boolean flag (Optimized) */
    long size;
} FileItem;

FileItem* all_browser_files = NULL;
int num_all_browser_files = 0;
FileItem* browser_files = NULL;
int num_browser_files = 0;
int browser_scroll_offset = 0;
int browser_selected_index = -1;

char** path_history = NULL;
int path_history_count = 0;
int path_history_index = -1;

char show_open_dialog = 0;
char show_save_dialog = 0;
char* dialog_input_text;
char* dialog_search_text;
char* dialog_address_text;
char* current_browser_path;
char* dialog_status_msg;
int active_input_field = 0; // 0 = Filename, 1 = Search, 2 = Address Bar
int dialog_input_cursor = 0;
int dialog_search_cursor = 0;
int dialog_address_cursor = 3;

/* --- Undo / Redo Engine (Extreme Memory Saver) --- */
#define MAX_UNDO_LEVELS 4

typedef struct {
    char** lines;
    int num_lines;
    
    Piece* piece_table;
    int num_pieces;
    /* LineIndex array is NO LONGER BACKED UP. Saves 99% of RAM! */
    long add_buffer_size;
    
    long cur_x;
    long cur_y;
} EditorSnapshot;

EditorSnapshot* undo_stack;
EditorSnapshot* redo_stack;
int undo_count = 0;
int redo_count = 0;

/* --- mouse --- */
unsigned char mouse_arrow[16] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xFF, 0xF8, 0xEC, 0xCC, 0x86, 0x02, 0x00, 0x00
};

unsigned char mouse_arrow88[8] = {
    0x00, 0x80, 0xE0, 0xF8, 0xFE, 0xFF, 0xEC, 0x86
};

void load_cursor_glyph88() {
    struct REGPACK r;
    r.r_ax = 0x1100;
    r.r_bx = 0x1000;
    r.r_cx = 1;
    r.r_dx = CURSOR_CHAR;
    r.r_es = FP_SEG(mouse_arrow88);
    r.r_bp = FP_OFF(mouse_arrow88);
    intr(0x10, &r);
}

/* --- Prototypes--- */

/* Piece Table Engine */
char* get_streamed_segment(int line_idx, long start_col, int max_chars); 
void init_empty_piece_table(void);   
void init_piece_table(const char* filepath); 
void insert_into_stream(char c); 
char get_stream_char(long global_offset);      
void delete_from_stream(long target_global);   

/* 32-Bit Math Wrappers */
long get_stream_logical_len(int log_y);
long get_stream_total_vis_lines(int max_c);
void get_stream_vis_mapping_long(long target_v_line, int max_c, int* out_log_y, long* out_start_x);
void get_stream_vis_info(long target_v_line, int max_c, int* out_log_y, long* out_start_x, int* out_len);

int save_document(const char* filepath);
void clear_entire_history(void);
void draw_editor(void);
void build_visual_map(void);
void draw_settings(void);
void draw_file_dialog(int is_save);
void draw_main_static_ui(int force);
void draw_main_element(int id, int is_hover);
void draw_context_menu(void);
void execute_menu_action(int menu_id, int action_y);
void handle_file_ok_click(void);
void load_directory(const char* path, int update_history);
void filter_browser_files(void);

void load_cursor_glyph() {
    struct REGPACK r;
    r.r_ax = 0x1100;
    r.r_bx = 0x1000;
    r.r_cx = 1;
    r.r_dx = CURSOR_CHAR;
    r.r_es = FP_SEG(mouse_arrow);
    r.r_bp = FP_OFF(mouse_arrow);
    intr(0x10, &r);
}

/* --- BIOS Hardware Polling (16-bit version) --- */
int is_shift_pressed() {
    unsigned char far* kbd_flags = (unsigned char far*)MK_FP(0x0040, 0x0017);
    return (*kbd_flags) & 0x03; 
}

int is_alt_pressed() {
    unsigned char far* kbd_flags = (unsigned char far*)MK_FP(0x0040, 0x0017);
    return (*kbd_flags) & 0x08; 
}

void hide_hardware_cursor() {
    union REGS regs;
    regs.x.ax = 0x0100; /* Set cursor shape */
    regs.x.cx = 0x2000; /* Bit 5 of CH set to 1 hides the cursor */
    int86(0x10, &regs, &regs);
}

void show_hardware_cursor() {
    union REGS regs;
    regs.x.ax = 0x0100; /* Set cursor shape */
    regs.x.cx = 0x0607; /* Standard underline shape */
    int86(0x10, &regs, &regs);
}

/* --- Configuration System (Memory Optimized for 16-bit) --- */
void load_config() {
    FILE* f = fopen("config.txt", "r");
    if (f) {
        char line[64];
        char key[32];
        int val;
        while (fgets(line, sizeof(line), f)) {
            /* Split the line at the '=' sign */
            if (sscanf(line, "%31[^=]=%d", key, &val) == 2) {
                if (strcmp(key, "setting_hover_effects") == 0) setting_hover_effects = val;
                else if (strcmp(key, "setting_ansi_mode") == 0) setting_ansi_mode = val;
                else if (strcmp(key, "setting_color") == 0) setting_color = val;
                else if (strcmp(key, "setting_bg_color") == 0) setting_bg_color = val;
                else if (strcmp(key, "setting_fg_color") == 0) setting_fg_color = val;
                else if (strcmp(key, "setting_sel_color") == 0) setting_sel_color = val;
                else if (strcmp(key, "setting_crit_color") == 0) setting_crit_color = val;
                else if (strcmp(key, "word_wrap") == 0) setting_word_wrap = val;
                else if (strcmp(key, "respect_whole_words") == 0) setting_respect_words = val;
                else if (strcmp(key, "h_scrollbar") == 0) setting_h_scrollbar = val;
                else if (strcmp(key, "v_scrollbar") == 0) setting_v_scrollbar = val;
            }
        }
        fclose(f);
    }
}

/* --- Time Machine (Undo/Redo) Core --- */

/* NEW: Rebuild Line Index dynamically instead of storing it! */
void rebuild_stream_lines() {
    int p;
    long global_offset = 0;
    long line_length = 0;

    if (stream_lines) free(stream_lines);
    stream_line_capacity = 1000;
    total_stream_lines = 0;
    stream_lines = (LineIndex*)malloc(stream_line_capacity * sizeof(LineIndex));

    stream_lines[total_stream_lines++].global_offset = 0;

    for (p = 0; p < num_pieces; p++) {
        Piece* piece = &piece_table[p];
        long bytes = piece->length;

        if (piece->source == SOURCE_DISK && stream_file_handle) {
            fseek(stream_file_handle, piece->offset, SEEK_SET);
            while (bytes > 0) {
                int ch = fgetc(stream_file_handle);
                if (ch == EOF) break;
                global_offset++;
                line_length++;

                if (ch == '\n' || line_length >= 4096) {
                    if (total_stream_lines >= stream_line_capacity) {
                        long new_cap = stream_line_capacity * 2;
                        if (new_cap * sizeof(LineIndex) <= 64000L) {
                            LineIndex* temp = (LineIndex*)realloc(stream_lines, (size_t)(new_cap * sizeof(LineIndex)));
                            if (temp) { stream_line_capacity = (int)new_cap; stream_lines = temp; }
                            else break; /* FIX: Safety brake */
                        } else break; /* FIX: Safety brake */
                    }
                    stream_lines[total_stream_lines++].global_offset = global_offset;
                    line_length = 0;
                }
                bytes--;
            }
        } else if (piece->source == SOURCE_RAM) {
            long r_idx = piece->offset;
            while (bytes > 0 && r_idx < add_buffer_size) {
                char ch = add_buffer[r_idx++];
                global_offset++;
                line_length++;

                if (ch == '\n' || line_length >= 4096) {
                    if (total_stream_lines >= stream_line_capacity) {
                        long new_cap = stream_line_capacity * 2;
                        if (new_cap * sizeof(LineIndex) <= 64000L) {
                            LineIndex* temp = (LineIndex*)realloc(stream_lines, (size_t)(new_cap * sizeof(LineIndex)));
                            if (temp) { stream_line_capacity = (int)new_cap; stream_lines = temp; }
                            else break; /* FIX: Safety brake */
                        } else break; /* FIX: Safety brake */
                    }
                    stream_lines[total_stream_lines++].global_offset = global_offset;
                    line_length = 0;
                }
                bytes--;
            }
        }
    }
}

void free_snapshot(EditorSnapshot* snap) {
    if (snap->lines) {
        int i;
        for (i = 0; i < snap->num_lines; i++) free(snap->lines[i]);
        free(snap->lines);
        snap->lines = NULL;
    }
    if (snap->piece_table) { free(snap->piece_table); snap->piece_table = NULL; }
    snap->num_lines = 0;
    snap->num_pieces = 0;
}

EditorSnapshot create_snapshot() {
    EditorSnapshot snap;
    snap.cur_x = cursor_x;
    snap.cur_y = cursor_y;
    
    if (setting_disk_stream) {
        snap.lines = NULL; snap.num_lines = 0;
        
        snap.num_pieces = num_pieces;
        snap.piece_table = (Piece*)malloc(num_pieces * sizeof(Piece));
        memcpy(snap.piece_table, piece_table, num_pieces * sizeof(Piece));
        
        /* stream_lines is NO LONGER COPIED to save memory! */
        
        snap.add_buffer_size = add_buffer_size;
    } else {
        int i;
        snap.piece_table = NULL; 
        snap.num_pieces = 0; 
        
        snap.num_lines = total_lines;
        snap.lines = (char**)malloc(total_lines * sizeof(char*));
        for (i = 0; i < total_lines; i++) {
            snap.lines[i] = (char*)malloc(strlen(text_buffer[i]) + 1);
            strcpy(snap.lines[i], text_buffer[i]);
        }
    }
    return snap;
}

void save_state() {
    int i;
    if (undo_count == MAX_UNDO_LEVELS) {
        free_snapshot(&undo_stack[0]);
        for (i = 0; i < MAX_UNDO_LEVELS - 1; i++) {
            undo_stack[i] = undo_stack[i + 1];
        }
        undo_count--;
    }
    undo_stack[undo_count++] = create_snapshot();
    
    /* Clearing Redo Stack when new action occurs */
    for (i = 0; i < redo_count; i++) {
        free_snapshot(&redo_stack[i]);
    }
    redo_count = 0;
}

void clear_entire_history() {
    int i;
    for (i = 0; i < undo_count; i++) free_snapshot(&undo_stack[i]);
    for (i = 0; i < redo_count; i++) free_snapshot(&redo_stack[i]);
    undo_count = 0;
    redo_count = 0;
    last_was_typing = 0;
}

void restore_from_snapshot(EditorSnapshot* snap) {
    if (setting_disk_stream) {
        free(piece_table);
        
        num_pieces = snap->num_pieces;
        piece_capacity = num_pieces + 10;
        piece_table = (Piece*)malloc(piece_capacity * sizeof(Piece));
        memcpy(piece_table, snap->piece_table, num_pieces * sizeof(Piece));
        
        add_buffer_size = snap->add_buffer_size;
        
        /* Rebuild Line Map dynamically to save massive amounts of RAM! */
        rebuild_stream_lines();
    } else {
        int i;
        for (i = 0; i < total_lines; i++) free(text_buffer[i]);
        free(text_buffer);
        
        total_lines = snap->num_lines;
        buffer_capacity = total_lines + INITIAL_LINES;
        text_buffer = (char**)malloc(buffer_capacity * sizeof(char*));
        
        for (i = 0; i < total_lines; i++) {
            text_buffer[i] = (char*)malloc(strlen(snap->lines[i]) + 1);
            strcpy(text_buffer[i], snap->lines[i]);
        }
    }
    cursor_x = snap->cur_x;
    cursor_y = snap->cur_y;
    has_selection = 0;
    is_dragging = 0;
    needs_recalc = 1;
    force_cursor_snap = 1;
    is_document_dirty = 1;
    last_was_typing = 0;
}

void perform_undo() {
    if (setting_read_only) return;
    if (undo_count == 0) return;
    if (redo_count == MAX_UNDO_LEVELS) {
        free_snapshot(&redo_stack[0]);
        int i; for (i = 0; i < MAX_UNDO_LEVELS - 1; i++) redo_stack[i] = redo_stack[i + 1];
        redo_count--;
    }
    redo_stack[redo_count++] = create_snapshot();
    undo_count--;
    restore_from_snapshot(&undo_stack[undo_count]);
    free_snapshot(&undo_stack[undo_count]);
}

void perform_redo() {
    if (setting_read_only) return;
    if (redo_count == 0) return;
    if (undo_count == MAX_UNDO_LEVELS) {
        free_snapshot(&undo_stack[0]);
        int i; for (i = 0; i < MAX_UNDO_LEVELS - 1; i++) undo_stack[i] = undo_stack[i + 1];
        undo_count--;
    }
    undo_stack[undo_count++] = create_snapshot();
    redo_count--;
    restore_from_snapshot(&redo_stack[redo_count]);
    free_snapshot(&redo_stack[redo_count]);
}

/* --- VGA Memory Rendering --- */
unsigned char make_color(unsigned char bg, unsigned char fg) { 
    return (bg << 4) | fg; 
}

/* --- Dynamic Theme Engine --- */
unsigned char get_bg_color() {
    if (!setting_color) {
        unsigned char bg = (setting_bg_color == 7) ? 7 : 0;
        unsigned char fg = (bg == 7) ? 0 : 7;
        return make_color(bg, fg);
    }
    return make_color((unsigned char)setting_bg_color, (unsigned char)setting_fg_color);
}

unsigned char get_cursor_color() {
    if (!setting_color) {
        return (setting_bg_color == 7) ? make_color(0, 7) : make_color(7, 0);
    }
    unsigned char bg = (unsigned char)(setting_fg_color & 7); 
    unsigned char fg = (unsigned char)setting_bg_color;
    if (bg == fg) fg = (unsigned char)((bg == 0) ? 7 : 0); 
    return make_color(bg, fg);
}

unsigned char get_hl_color() {
    if (!setting_color) {
        return (setting_bg_color == 7) ? make_color(0, 7) : make_color(7, 0);
    }
    unsigned char bg = (unsigned char)setting_sel_color;
    unsigned char fg = 15;
    if (bg == 7) fg = 0; /* Enforce Contrast */
    return make_color(bg, fg);
}

unsigned char get_cr_color() {
    if (!setting_color) {
        return (setting_bg_color == 7) ? make_color(0, 7) : make_color(7, 0);
    }
    unsigned char bg = (unsigned char)setting_crit_color;
    unsigned char fg = 15;
    if (bg == 7) fg = 0; /* Enforce Contrast */
    return make_color(bg, fg);
}

unsigned char get_dim_color() {
    if (!setting_color) {
        return (setting_bg_color == 7) ? make_color(7, 8) : make_color(0, 8);
    }
    return make_color((unsigned char)setting_bg_color, 8); /* 8 is Dark Gray */
}

void set_char(int r, int c, char ch, unsigned char color) {
    /* 16-Bit Real Mode physical memory access */
    char far* video = (char far*)MK_FP(video_segment, (r * SCREEN_COLS + c) * 2);
    *video = ch;
    *(video + 1) = color;
}


void print_str(int r, int c, const char* str, unsigned char color) {
    int i = 0; 
    while (str[i] != '\0' && (c + i) < SCREEN_COLS) { // Fixed Boundary!
        set_char(r, c + i, str[i], color); 
        i++; 
    }
}

unsigned char decode_char(char ch) {
    if (setting_ansi_mode == 0) {
        switch(ch) {
            case '|': case '!': case '@': case '$': case '?': return '|';
            case '*': case '=': case '~': case '^': case '_': return '-';
            case '[': case ']': case '{': case '}': case '+': case '`': case '%': case '<': case '&': case '>': return '+';
            case '1': return '^';
            case '2': return 'v';
            case '3': return '*';
            case '4': return '<';
            case '5': return '>';
            default: return (unsigned char)ch;
        }
    } else {
        switch(ch) {
            case '|': return 179; // Vertical Line
            case '*': return 196; // Horizontal Line
            case '[': return 218; // Top Left Corner
            case ']': return 191; // Top Right Corner
            case '{': return 192; // Bottom Left Corner
            case '}': return 217; // Bottom Right Corner
            case '^': return 194; // T Junction Up
            case '_': return 193; // T Junction Down
            case '@': return 195; // T Junction Left
            case '$': return 180; // T Junction Right
            case '+': return 197; 
            case '-': return 196;
            case '!': return 186;
            case '=': return 205; 
            case '~': return 209; 
            case '`': return 201; 
            case '%': return 187;
            case '<': return 200; 
            case '&': return 207; 
            case '>': return 188; 
            case '?': return 219;
            case '1': return 30;  
            case '2': return 31;  
            case '3': return 15; 
            case '4': return 17; 
            case '5': return 16; 
            default: return (unsigned char)ch;
        }
    }
}

void print_mapped_str(int r, int c, const char* str, unsigned char color) {
    int i = 0; 
    while (str[i] != '\0' && (c + i) < SCREEN_COLS) { // Fixed Boundary!
        set_char(r, c + i, decode_char(str[i]), color); 
        i++; 
    }
}


/* --- Mouse Driver --- */
void show_mouse() { 
    if (!use_mouse) return; 

    /* 16-bit interrupt for hardware cursor */
    if (!use_software_cursor) {
        union REGS regs; 
        regs.x.ax = SHOW_MOUSE; 
        int86(MOUSE_INT, &regs, &regs); 
    }

    if (use_software_cursor && !software_cursor_visible) {
        /* 16-bit direct memory access */
        if (mouse_y < SCREEN_ROWS && mouse_x < SCREEN_COLS) {
            char far* video = (char far*)MK_FP(video_segment, (mouse_y * SCREEN_COLS + mouse_x) * 2);
            saved_char = *video;
            saved_attr = *(video + 1);
            *video = CURSOR_CHAR;
            *(video + 1) = (saved_attr & 0xF0) | 0x0F;
        }
        saved_mouse_x = mouse_x;
        saved_mouse_y = mouse_y;
        software_cursor_visible = 1;
    }
}

void hide_mouse() { 
    if (!use_mouse) return; 

    /* 16-bit interrupt for hardware cursor */
    if (!use_software_cursor) {
        union REGS regs; 
        regs.x.ax = HIDE_MOUSE; 
        int86(MOUSE_INT, &regs, &regs); 
    }

    if (use_software_cursor && software_cursor_visible) {
        /* 16-bit direct memory access */
        if (saved_mouse_y < SCREEN_ROWS && saved_mouse_x < SCREEN_COLS) {
            char far* video = (char far*)MK_FP(video_segment, (saved_mouse_y * SCREEN_COLS + saved_mouse_x) * 2);
            *video = saved_char;
            *(video + 1) = saved_attr;
        }
        software_cursor_visible = 0;
    }
}

int init_mouse() {
    union REGS regs; 
    regs.x.ax = INIT_MOUSE; 
    int86(MOUSE_INT, &regs, &regs);
    if (regs.x.ax == 0xFFFF) {
        regs.x.ax = 0x000A; 
        regs.x.bx = 0x0000; 
        regs.x.cx = 0xF000; 
        regs.x.dx = 0x0F00 | CURSOR_CHAR; 
        int86(MOUSE_INT, &regs, &regs); 
        show_mouse(); 
        return 1;
    }
    use_mouse = 0; /* FIX: Explicitly disable the mouse engine if no driver is detected! */
    return 0;
}

void set_mouse_range(int cols, int rows) {
    if (!use_mouse) return; /* Guard against firing INT 33 without a driver */
    union REGS regs;
    int max_x = (cols <= 80) ? 639 : (cols * 8 - 1);
    int max_y = (cols <= 80) ? ((rows * 8) - 1) : (rows * font_height - 1);

    regs.x.ax = 0x0007; regs.x.cx = 0; regs.x.dx = max_x; int86(MOUSE_INT, &regs, &regs);
    regs.x.ax = 0x0008; regs.x.cx = 0; regs.x.dx = max_y; int86(MOUSE_INT, &regs, &regs);
}

void update_mouse() {
    union REGS regs;
    if (!use_mouse) { 
        mouse_x = 0; mouse_y = 0; mouse_left = 0; mouse_right = 0; 
        return; 
    }

    regs.x.ax = GET_MOUSE_STATUS; 
    int86(MOUSE_INT, &regs, &regs);

    int divisor_x = (SCREEN_COLS < 80) ? 16 : 8;
    int divisor_y = (SCREEN_COLS > 80) ? font_height : 8;
    
    int new_x = regs.x.cx / divisor_x; 
    int new_y = regs.x.dx / divisor_y; 

    /* Software cursor logic remains the same, using 32-bit logic */
    if (use_software_cursor && software_cursor_visible) {
        if (new_x != mouse_x || new_y != mouse_y) {
            hide_mouse();
            mouse_x = new_x;
            mouse_y = new_y;
            show_mouse();
        }
    } else {
        mouse_x = new_x;
        mouse_y = new_y;
    }

    /* BX bitmasking works the same in 32-bit registers */
    mouse_left = (regs.x.bx & 1);
    mouse_right = (regs.x.bx & 2);
}


/* --- Disk I/O & Browser --- */
void filter_browser_files() {
    int i;
    num_browser_files = 0;
    browser_scroll_offset = 0;
    browser_selected_index = -1;

    for (i = 0; i < num_all_browser_files; i++) {
        if (dialog_search_text[0] == '\0') {
            browser_files[num_browser_files++] = all_browser_files[i];
        } else {
            /* Simple case-insensitive search */
            char lower_name[128];
            char lower_query[256];
            int j;
            strcpy(lower_name, all_browser_files[i].name);
            strcpy(lower_query, dialog_search_text);
            for (j = 0; lower_name[j]; j++) lower_name[j] = (char)tolower(lower_name[j]);
            for (j = 0; lower_query[j]; j++) lower_query[j] = (char)tolower(lower_query[j]);

            if (strstr(lower_name, lower_query) != NULL) {
                browser_files[num_browser_files++] = all_browser_files[i];
            }
        }
    }
}

void load_directory(const char* path, int update_history) {
    struct ffblk file_block; 
    int done, i, j; 
    char search_path[256];
    
    num_all_browser_files = 0; 
    
    strcpy(current_browser_path, path);
    if (current_browser_path[strlen(current_browser_path) - 1] != '\\') {
        strcat(current_browser_path, "\\");
    }
    
    strcpy(dialog_address_text, current_browser_path); 
    dialog_address_cursor = (int)strlen(dialog_address_text);
    
    dialog_search_text[0] = '\0';
    dialog_search_cursor = 0;
    
    if (active_input_field == 3) active_input_field = 0;

    if (update_history) {
        if (path_history_index < path_history_count - 1) {
            path_history_count = path_history_index + 1;
        }
        if (path_history_count == 0 || stricmp(path_history[path_history_count - 1], current_browser_path) != 0) {
            if (path_history_count < 20) {
                strcpy(path_history[path_history_count++], current_browser_path);
                path_history_index++;
            } else {
                for (i = 1; i < 20; i++) strcpy(path_history[i-1], path_history[i]);
                strcpy(path_history[19], current_browser_path);
            }
        }
    }
    
    strcpy(search_path, current_browser_path); 
    strcat(search_path, "*.*");
    
    done = findfirst(search_path, &file_block, FA_DIREC | FA_ARCH | FA_RDONLY);
    while (!done && num_all_browser_files < 100) {
        if (strcmp(file_block.ff_name, ".") != 0) { 
            int is_dir = (file_block.ff_attrib & FA_DIREC) ? 1 : 0; 
            int add_item = is_dir;
            if (!is_dir) { 
                int len = (int)strlen(file_block.ff_name); 
                if (len >= 4 && stricmp(&file_block.ff_name[len - 4], ".TXT") == 0) {
                    add_item = 1; 
                }
            }
            if (add_item) { 
                strcpy(all_browser_files[num_all_browser_files].name, file_block.ff_name); 
                all_browser_files[num_all_browser_files].is_dir = is_dir; 
                all_browser_files[num_all_browser_files].size = file_block.ff_fsize; 
                num_all_browser_files++; 
            }
        }
        done = findnext(&file_block);
    }
    
    for (i = 0; i < num_all_browser_files - 1; i++) {
        for (j = i + 1; j < num_all_browser_files; j++) {
            int swap = 0;
            if (all_browser_files[i].is_dir != all_browser_files[j].is_dir) { 
                if (all_browser_files[j].is_dir) swap = 1; 
            }
            else { 
                if (stricmp(all_browser_files[i].name, all_browser_files[j].name) > 0) swap = 1; 
            }
            if (swap) { 
                FileItem temp = all_browser_files[i]; 
                all_browser_files[i] = all_browser_files[j]; 
                all_browser_files[j] = temp; 
            }
        }
    }
    
    filter_browser_files();
}

/* --- Dynamic Memory Logic --- */
int is_in_selection(long y, long x) {
    long sy = sel_start_y, sx = sel_start_x, ey = cursor_y, ex = cursor_x, temp;
    if (!has_selection) return 0;
    if (sy > ey || (sy == ey && sx > ex)) { 
        temp = sy; sy = ey; ey = temp; 
        temp = sx; sx = ex; 
        ex = temp + 1; /* Include anchor char when dragging backwards */
        long line_len = setting_disk_stream ? get_stream_logical_len((int)ey) : (long)strlen(text_buffer[ey]);
        if (ex > line_len) ex = line_len;
    }
    if (y < sy || y > ey) return 0;
    if (sy == ey) return (x >= sx && x < ex);
    if (y == sy) return x >= sx;
    if (y == ey) return x < ex;
    return 1;
}

void delete_selection() {
    long sy, sx, ey, ex, temp, i; 
    int lines_removed; char *first, *last, *line;
    
    if (setting_read_only) return; /* GUARD: Read-Only Mode */
    if (!has_selection) return;
    
    sy = sel_start_y; sx = sel_start_x; ey = cursor_y; ex = cursor_x;
    if (sy > ey || (sy == ey && sx > ex)) { 
        temp = sy; sy = ey; ey = temp; 
        temp = sx; sx = ex; 
        ex = temp + 1; /* Include anchor char when dragging backwards */
        long line_len = setting_disk_stream ? get_stream_logical_len((int)ey) : (long)strlen(text_buffer[ey]);
        if (ex > line_len) ex = line_len;
    }
    
    if (setting_disk_stream) {
        long start_g = stream_lines[sy].global_offset + sx;
        long end_g = stream_lines[ey].global_offset + ex;
        long del_count = end_g - start_g;
        
        /* Chomp through the stream cleanly! */
        for (i = 0; i < del_count; i++) delete_from_stream(start_g);
        
        cursor_x = sx; cursor_y = sy; has_selection = 0;
        is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
        return;
    }
    
    if (sy == ey) {
        line = text_buffer[sy]; temp = (int)strlen(line);
        memmove(&line[sx], &line[ex], temp - ex + 1);
        text_buffer[sy] = (char*)realloc(line, strlen(line) + 1);
    } else {
        first = text_buffer[sy]; last = text_buffer[ey]; first[sx] = '\0';
        first = (char*)realloc(first, strlen(first) + strlen(&last[ex]) + 1);
        strcat(first, &last[ex]); text_buffer[sy] = first;
        for(i = sy + 1; i <= ey; i++) free(text_buffer[i]);
        lines_removed = (int)(ey - sy);
        for(i = (int)ey + 1; i < total_lines; i++) text_buffer[i - lines_removed] = text_buffer[i];
        total_lines -= lines_removed;
    }
    cursor_x = sx; cursor_y = sy; has_selection = 0; 
    is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
}

void copy_selection() {
    int sy = sel_start_y, sx = sel_start_x, ey = cursor_y, ex = cursor_x;
    int temp, i; size_t buf_size = 1;
    if (!has_selection) return;
    if (sy > ey || (sy == ey && sx > ex)) { 
        temp = sy; sy = ey; ey = temp; 
        temp = sx; sx = ex; 
        ex = temp + 1; /* Include anchor char when dragging backwards */
        int line_len = (int)strlen(text_buffer[ey]);
        if (ex > line_len) ex = line_len;
    }
    if (internal_clipboard) free(internal_clipboard);
    
    if (sy == ey) buf_size += (ex - sx);
    else {
        buf_size += strlen(&text_buffer[sy][sx]) + 1;
        for(i = sy + 1; i < ey; i++) buf_size += strlen(text_buffer[i]) + 1;
        buf_size += ex;
    }
    
    internal_clipboard = (char*)malloc(buf_size); 
    if (!internal_clipboard) return;
    internal_clipboard[0] = '\0';
    
    if (sy == ey) {
        strncat(internal_clipboard, &text_buffer[sy][sx], ex - sx);
    } else {
        strcat(internal_clipboard, &text_buffer[sy][sx]); 
        strcat(internal_clipboard, "\n");
        for(i = sy + 1; i < ey; i++) { 
            strcat(internal_clipboard, text_buffer[i]); 
            strcat(internal_clipboard, "\n"); 
        }
        strncat(internal_clipboard, text_buffer[ey], ex);
    }
}

void init_buffer() {
    text_buffer = (char**)malloc(buffer_capacity * sizeof(char*));
    text_buffer[0] = (char*)malloc(1); text_buffer[0][0] = '\0'; total_lines = 1;
    vis_cap = 200; vis_map = (VisualLine*)malloc(vis_cap * sizeof(VisualLine));
}

void insert_char(char c) {
    char* line; int len;
    char* new_ptr;
    
    if (setting_read_only) return; /* GUARD: Read-Only Mode */
    
    if (setting_disk_stream) {
        insert_into_stream(c);
        return;
    }
    
    line = text_buffer[cursor_y]; 
    len = (int)strlen(line);
    
    /* Safety Net: Ask for memory, but don't overwrite the original pointer yet! */
    new_ptr = (char*)realloc(line, len + 2);
    if (!new_ptr) return; /* OOM GUARD: If MS-DOS is out of RAM, safely ignore the keystroke */
    
    line = new_ptr;
    memmove(&line[cursor_x + 1], &line[cursor_x], len - cursor_x + 1); 
    line[cursor_x] = c; 
    text_buffer[cursor_y] = line;
    cursor_x++; is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
}

void backspace() {
    char* line;
    if (setting_read_only) return; /* GUARD: Read-Only Mode */
    if (setting_disk_stream) {
        if (cursor_x == 0 && cursor_y == 0) return;
        long target_global = stream_lines[cursor_y].global_offset + cursor_x - 1;
        
        if (cursor_x > 0) {
            cursor_x--;
        } else {
            cursor_y--;
            cursor_x = get_stream_logical_len(cursor_y);
        }
        
        delete_from_stream(target_global);
        is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
        return;
    }
    
    line = text_buffer[cursor_y];;
    if (cursor_x > 0) { 
        int len = (int)strlen(line); 
        memmove(&line[cursor_x - 1], &line[cursor_x], len - cursor_x + 1); 
        cursor_x--; is_document_dirty = 1; 
        text_buffer[cursor_y] = (char*)realloc(line, (size_t)len); 
    }
    else if (cursor_y > 0) { 
        int prev_len = (int)strlen(text_buffer[cursor_y - 1]); 
        text_buffer[cursor_y - 1] = (char*)realloc(text_buffer[cursor_y - 1], (size_t)(prev_len + strlen(line) + 1));
        strcat(text_buffer[cursor_y - 1], line); 
        free(text_buffer[cursor_y]); 
        { 
            int i; 
            for (i = cursor_y; i < total_lines - 1; i++) text_buffer[i] = text_buffer[i + 1]; 
        }
        total_lines--; cursor_y--; cursor_x = prev_len; is_document_dirty = 1; 
    }
    needs_recalc = 1; force_cursor_snap = 1;
}

void insert_newline() {
    char *current_line, *new_line; int i;
    char **new_buf;
    
    if (setting_read_only) return; /* GUARD: Read-Only Mode */
    if (setting_disk_stream) {
        insert_into_stream('\n');
        return;
    }
    
    if (total_lines >= buffer_capacity) { 
        int new_cap = buffer_capacity * 2; 
        new_buf = (char**)realloc(text_buffer, new_cap * sizeof(char*)); 
        if (!new_buf) return; /* OOM GUARD: Safely stop if out of RAM */
        buffer_capacity = new_cap;
        text_buffer = new_buf;
    }
    
    current_line = text_buffer[cursor_y]; 
    new_line = (char*)malloc(strlen(&current_line[cursor_x]) + 1);
    if (!new_line) return; /* OOM GUARD */
    
    for (i = total_lines; i > cursor_y + 1; i--) text_buffer[i] = text_buffer[i - 1];
    
    strcpy(new_line, &current_line[cursor_x]); 
    current_line[cursor_x] = '\0';
    
    /* Shrinking realloc rarely fails, but we catch it just in case */
    char* shrunk_line = (char*)realloc(current_line, (size_t)(cursor_x + 1));
    if (shrunk_line) text_buffer[cursor_y] = shrunk_line;
    
    text_buffer[cursor_y + 1] = new_line; 
    total_lines++; cursor_y++; cursor_x = 0; 
    is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
}

void paste_clipboard() {
    int i, len;
    if (setting_read_only) return; /* GUARD: Read-Only Mode */
    if (!internal_clipboard) return;
    if (has_selection) {
        save_state(); last_was_typing = 0;
        delete_selection();
    }
    len = (int)strlen(internal_clipboard);
    for(i = 0; i < len; i++) {
        if (internal_clipboard[i] == '\n') insert_newline();
        else if (internal_clipboard[i] != '\r') insert_char(internal_clipboard[i]);
    }
}

/* --- Dynamic Disk Stream Word Wrap (Math Based) --- */
long get_stream_logical_len(int log_y) {
    long line_start, line_end, total_chars = 0;
    int i;
    if (log_y < 0 || log_y >= total_stream_lines) return 0;
    
    line_start = stream_lines[log_y].global_offset;
    
    if (log_y < total_stream_lines - 1) {
        line_end = stream_lines[log_y+1].global_offset; 
    } else {
        for (i = 0; i < num_pieces; i++) total_chars += piece_table[i].length;
        line_end = total_chars;
    }
    
    /* FAST TYPING FIX: Only perform laggy disk I/O to check for \r\n on the active cursor line! 
       Bypassing this for the other 99 visual lines eliminates ~200 disk reads per keystroke! */
    if (log_y == cursor_y && line_end > line_start) {
        char last_c = get_stream_char(line_end - 1);
        if (last_c == '\n') {
            line_end--;
            if (line_end > line_start && get_stream_char(line_end - 1) == '\r') line_end--;
        }
    }
    return line_end - line_start;
}

long get_stream_total_vis_lines(int max_c) {
    long total_v = 0; int i;
    for (i = 0; i < total_stream_lines; i++) {
        long len = get_stream_logical_len(i);
        if (len <= 0) total_v += 1;
        else total_v += (len + max_c - 1) / max_c;
    }
    return total_v;
}

void get_stream_vis_mapping_long(long target_v_line, int max_c, int* out_log_y, long* out_start_x) {
    long current_v = 0; int i;
    for (i = 0; i < total_stream_lines; i++) {
        long len = get_stream_logical_len(i);
        long v_lines_for_this_log = (len <= 0) ? 1 : (len + max_c - 1) / max_c;
        
        if (target_v_line >= current_v && target_v_line < current_v + v_lines_for_this_log) {
            *out_log_y = i;
            *out_start_x = (target_v_line - current_v) * max_c;
            return;
        }
        current_v += v_lines_for_this_log;
    }
    *out_log_y = total_stream_lines - 1;
    *out_start_x = 0;
}

/* NEW HELPER: For Arrow Key Navigation */
void get_stream_vis_info(long target_v_line, int max_c, int* out_log_y, long* out_start_x, int* out_len) {
    get_stream_vis_mapping_long(target_v_line, max_c, out_log_y, out_start_x);
    long log_len = get_stream_logical_len(*out_log_y);
    long remaining = log_len - *out_start_x;
    *out_len = (remaining > max_c) ? max_c : (int)remaining;
    if (*out_len < 0) *out_len = 0;
}

void build_visual_map() {
    int i, j, cp, sp, len, max_c = setting_v_scrollbar ? (SCREEN_COLS - 4) : (SCREEN_COLS - 2); 
    vis_lines = 0; max_line_len = 0;

    int num_logical_lines = setting_disk_stream ? total_stream_lines : total_lines;
    
    long start_log = 0;
    long end_log = num_logical_lines;
    
    /* THE SLIDING WINDOW (PAGING) */
    if (setting_disk_stream) {
        start_log = current_chunk_start_log;
        end_log = start_log + CHUNK_LOG_SIZE;
        if (end_log > num_logical_lines) end_log = num_logical_lines;
    }

    for (i = start_log; i < end_log; i++) {
        char* line_text = NULL;
        if (setting_disk_stream) {
            len = (int)get_stream_logical_len(i);
            
            /* 32-BIT OPTIMIZATION: Only load the line into RAM if we ACTUALLY 
               need to search it for spaces to calculate Word Wrap! */
            if (setting_word_wrap && len > max_c) {
                line_text = get_streamed_segment(i, 0, len);
            }
        } else {
            line_text = text_buffer[i];
            len = (int)strlen(line_text);
        }
        
        if (len > max_line_len) max_line_len = len;
        
        if (!setting_word_wrap || len <= max_c) {
            if (vis_lines >= vis_cap) { 
                long new_cap = vis_cap * 2L;
                if (new_cap * sizeof(VisualLine) >= 64000L) break; /* FIX: Safety Brake! */
                vis_cap = (int)new_cap;
                vis_map = (VisualLine*)realloc(vis_map, vis_cap * sizeof(VisualLine)); 
            }
            vis_map[vis_lines].log_y = i; vis_map[vis_lines].log_x = 0; 
            vis_map[vis_lines].len = len; vis_lines++;
        } else {
            cp = 0;
            while (cp < len) {
                int remain = len - cp; 
                if (remain <= max_c) sp = remain;
                else {
                    sp = max_c;
                    if (setting_respect_words) {
                        int ls = -1; 
                        for(j = cp + sp; j >= cp; j--) { 
                            if (line_text && line_text[j] == ' ') { ls = j - cp; break; } 
                        }
                        if (ls > 0) sp = ls;
                    }
                }
                if (vis_lines >= vis_cap) { 
                    long new_cap = vis_cap * 2L;
                    if (new_cap * sizeof(VisualLine) >= 64000L) break; /* FIX: Safety Brake! */
                    vis_cap = (int)new_cap;
                    vis_map = (VisualLine*)realloc(vis_map, vis_cap * sizeof(VisualLine)); 
                }
                vis_map[vis_lines].log_y = i; vis_map[vis_lines].log_x = cp; 
                vis_map[vis_lines].len = sp; vis_lines++;
                cp += sp; 
                if (setting_respect_words && cp < len && line_text && line_text[cp] == ' ') cp++;
            }
        }
        if (setting_disk_stream && line_text) free(line_text);
    }
}

void update_vis_cursor() {
    int i; vis_cx = 0; vis_cy = 0;
    
    if (setting_disk_stream) {
        int shift_needed = 0;
        
        /* 1. Absolute out-of-bounds (Failsafe) */
        if (cursor_y < current_chunk_start_log || cursor_y >= current_chunk_start_log + CHUNK_LOG_SIZE) {
            shift_needed = 1;
        }
        /* 2. The YouTube Buffer (Pre-load when approaching the edge) */
        else if (current_chunk_start_log > 0 && cursor_y < current_chunk_start_log + 20) {
            shift_needed = 1;
        }
        else if (current_chunk_start_log + CHUNK_LOG_SIZE < total_stream_lines && cursor_y > current_chunk_start_log + CHUNK_LOG_SIZE - 20) {
            shift_needed = 1;
        }

        if (shift_needed) {
            current_chunk_start_log = cursor_y - (CHUNK_LOG_SIZE / 2); 
            if (current_chunk_start_log < 0) current_chunk_start_log = 0;
            build_visual_map();
        }
    }

    if (!setting_word_wrap) { 
        vis_cy = setting_disk_stream ? (cursor_y - current_chunk_start_log) : cursor_y; 
        vis_cx = cursor_x; 
    }
    else {
        for (i = 0; i < vis_lines; i++) {
            if (vis_map[i].log_y == cursor_y) {
                if (cursor_x >= vis_map[i].log_x && cursor_x <= vis_map[i].log_x + vis_map[i].len) {
                    vis_cy = i; vis_cx = cursor_x - vis_map[i].log_x;
                    if (cursor_x < vis_map[i].log_x + vis_map[i].len || i == vis_lines - 1 || vis_map[i+1].log_y != cursor_y) break;
                }
            }
        }
    }
}

/* --- Redraw Logic --- */
int get_hover_id(int mx, int my) {
    if (show_settings) {
        int right = SCREEN_COLS - 1;
        if (my >= 3 && my <= 5 && mx >= right - 13 && mx <= right - 4) return 99;
        return 0;
    }
    if (show_open_dialog || show_save_dialog) {
        int right = SCREEN_COLS - 1;
        int bottom = SCREEN_ROWS - 1;
        if (SCREEN_COLS < 80) {
            if (my == 1 && mx >= 36 && mx <= 38) return 4;
            if (my == 20 && mx >= 23 && mx <= 28) return 5;
            if (my == 20 && mx >= 30 && mx <= 37) return 6;
            if (my == 7 && mx == 36) return 65;
            if (my == 17 && mx == 36) return 66;
            if (my == 4) {
                if (mx >= 2 && mx <= 4) return 70;
                if (mx >= 5 && mx <= 7) return 71;
                if (mx >= 9 && mx <= 11) return 72;
            }
        } else {
            if (my == 1 && mx >= right - 3 && mx <= right - 1) return 4;
            if (my == bottom - 4 && mx >= right - 17 && mx <= right - 12) return 5;
            if (my == bottom - 4 && mx >= right - 8 && mx <= right - 3) return 6;
            if (my == 7 && mx == right - 3) return 65;
            if (my == bottom - 7 && mx == right - 3) return 66;
            if (my == 4) {
                if (mx >= 2 && mx <= 5) return 70;
                if (mx >= 7 && mx <= 10) return 71;
                if (mx >= 12 && mx <= 14) return 72;
            }
        }
        return 0;
    }
    if (show_exit_dialog) {
        int is_40 = (SCREEN_COLS < 80);
        int box_w = is_40 ? 38 : 44;
        int start_x = (SCREEN_COLS - box_w) / 2;
        int start_y = (SCREEN_ROWS - 10) / 2;
        int s1 = is_40 ? 2 : 4;  int e1 = is_40 ? 9 : 11;
        int s2 = is_40 ? 12 : 15; int e2 = is_40 ? 25 : 28;
        int s3 = is_40 ? 28 : 32; int e3 = is_40 ? 35 : 39;
        
        if (my == start_y + 7) { 
            if (mx >= start_x + s1 && mx <= start_x + e1) return 80; 
            if (mx >= start_x + s2 && mx <= start_x + e2) return 81; 
            if (mx >= start_x + s3 && mx <= start_x + e3) return 82; 
        }
        return 0;
    }
    if (show_find_dialog) {
        if (SCREEN_COLS < 80) {
            if (my >= 3 && my <= 5 && mx >= 22 && mx <= 28) return 90;
            if (my >= 3 && my <= 5 && mx >= 30 && mx <= 36) return 91;
            if (my >= 6 && my <= 8 && mx >= 22 && mx <= 28) return 92;
            if (my >= 6 && my <= 8 && mx >= 30 && mx <= 36) return 93;
        } else {
            if (my >= 3 && my <= 5 && mx >= 53 && mx <= 63) return 90;
            if (my >= 3 && my <= 5 && mx >= 65 && mx <= 75) return 91;
            if (my >= 6 && my <= 8 && mx >= 53 && mx <= 63) return 92;
            if (my >= 6 && my <= 8 && mx >= 65 && mx <= 75) return 93;
        }
        /* Do not return 0 here, because scrollbars and menus can still be hovered! */
    }
    if (show_context_menu) {
        if (my >= ctx_y + 1 && my <= ctx_y + 10 && mx >= ctx_x + 1 && mx <= ctx_x + 14) return 100 + (my - ctx_y);
        return 100;
    }
    if (active_menu == 1) { 
        if (my >= 3 && my <= 12 && mx >= 1 && mx <= 15) return 10 + (my - 3); 
    }
    if (active_menu == 2) { 
        if (my >= 3 && my <= 12 && mx >= 7 && mx <= 22) return 20 + (my - 3); 
    }
    if (active_menu == 3) { 
        if (my >= 3 && my <= 11 && mx >= 14 && mx <= 36) return 30 + (my - 3); 
    }
    if (my == 1) { 
        if (mx >= 1 && mx <= 6) return 1; 
        if (mx >= 8 && mx <= 13) return 2; 
        if (mx >= 15 && mx <= 20) return 3; 
        if (mx >= SCREEN_COLS - 4 && mx <= SCREEN_COLS - 2) return 4; // X Button
    }
    if (my == SCREEN_ROWS - 2 && mx == SCREEN_COLS - 2) return 60;
    if (setting_v_scrollbar && my == 3 && mx == SCREEN_COLS - 2) return 61;
    if (setting_v_scrollbar && my == SCREEN_ROWS - 4 && mx == SCREEN_COLS - 2) return 62;
    if (setting_h_scrollbar && my == SCREEN_ROWS - 2 && mx == 1) return 63;
    if (setting_h_scrollbar && my == SCREEN_ROWS - 2 && mx == (setting_v_scrollbar ? SCREEN_COLS - 4 : SCREEN_COLS - 2)) return 64;
    return 0;
}


void draw_main_static_ui(int force) {
    static int last_dirty = -1;
    if (!force && is_document_dirty == last_dirty) return;
    last_dirty = is_document_dirty;
    
    unsigned char bg = get_bg_color(), dim = get_dim_color();
    unsigned char base = show_exit_dialog ? dim : bg;
    char title_buf[150]; int title_len;
    int right = SCREEN_COLS - 1, bottom = SCREEN_ROWS - 1, x_div = right - 4;
    
    /* FIX: Restored the missing title rendering logic! */
    strcpy(title_buf, is_document_dirty ? "*" : ""); 
    strncat(title_buf, current_filename, 140); 
    if ((int)strlen(title_buf) > (right - 7)) strcpy(&title_buf[right - 10], "...");
    print_str(1, 23, title_buf, base);
    
    print_str(1, x_div + 1, " X ", base);
    set_char(bottom - 1, right - 1, decode_char('3'), base);
    if (setting_v_scrollbar) {
        set_char(3, right - 1, decode_char('1'), base);

        set_char(4, right - 1, decode_char('*'), base); /* FIX: Top track border */
        set_char(4, right - 2, decode_char('@'), base); /* FIX: Top track border left*/
        set_char(4, right, decode_char('$'), base); /* FIX: Top track border left*/

        set_char(bottom - 4, right - 1, decode_char('*'), base); /* FIX: Bottom track border */
        set_char(bottom - 4, right - 2, decode_char('@'), base); /* FIX: Bottom track border left*/
        set_char(bottom - 4, right, decode_char('$'), base); /* FIX: Bottom track border right*/

        set_char(bottom - 3, right - 1, decode_char('2'), base);
    }
    if (setting_h_scrollbar) {
        set_char(bottom - 1, 1, decode_char('4'), base);
        set_char(bottom - 1, setting_v_scrollbar ? right - 3 : right - 1, decode_char('5'), base);
    }
}

void draw_main_element(int id, int is_hover) {
    unsigned char bg = get_bg_color(), hl = get_hl_color(), cr = get_cr_color();
    unsigned char base = show_exit_dialog ? get_dim_color() : bg;
    unsigned char c_col = is_hover ? hl : base;
    
    if (!is_hover) {
        if (id == 1 && active_menu == 1) c_col = hl;
        if (id == 2 && active_menu == 2) c_col = hl;
        if (id == 3 && active_menu == 3) c_col = hl;
    } else if (id == 4) c_col = cr;

    int right = SCREEN_COLS - 1, bottom = SCREEN_ROWS - 1, x_div = right - 4;
    
    if (id == 1) print_str(1, 1, " File ", c_col);
    else if (id == 2) print_str(1, 8, " Edit ", c_col);
    else if (id == 3) print_str(1, 15, " View ", c_col);
    else if (id == 4) print_str(1, x_div + 1, " X ", c_col);
    else if (id == 60) set_char(bottom - 1, right - 1, decode_char('3'), c_col);
    else if (id == 61 && setting_v_scrollbar) set_char(3, right - 1, decode_char('1'), c_col);
    else if (id == 62 && setting_v_scrollbar) set_char(bottom - 3, right - 1, decode_char('2'), c_col);
    else if (id == 63 && setting_h_scrollbar) set_char(bottom - 1, 1, decode_char('4'), c_col);
    else if (id == 64 && setting_h_scrollbar) set_char(bottom - 1, setting_v_scrollbar ? right - 3 : right - 1, decode_char('5'), c_col);
}

void draw_settings() {
    int r, c;
    int right = SCREEN_COLS - 1;
    int bottom = SCREEN_ROWS - 1;
    unsigned char bg = get_bg_color();
    unsigned char cr = get_cr_color();

    /* FIX: Clear the entire inner canvas area to erase the text file behind it! */
    for (r = 3; r <= bottom - 3; r++) {
        for (c = 1; c < right; c++) {
            set_char(r, c, ' ', bg);
        }
    }

    /* Print the Lite instructions */
    print_str(4, 2, " Modify 'config.txt' to change settings. ", bg);
    print_str(6, 2, " Restart the editor to apply changes. ", bg);

    /* Draw Back Button (Fixed Hover Logic!) */
    unsigned char back_col = (last_hover_id == 99 && setting_hover_effects) ? cr : bg;
    print_mapped_str(3, right - 13, "[********]", bg);
    set_char(4, right - 13, decode_char('|'), bg); print_str(4, right - 12, "  Back  ", back_col); set_char(4, right - 4, decode_char('|'), bg);
    set_char(2, right - 2, decode_char('*'), bg);
    set_char(bottom - 2, right - 2, decode_char('^'), bg);
    //draw missing vertical borders to complete the tab look 
    set_char(7, 0, decode_char('|'), bg);
    set_char(4, right, decode_char('|'), bg);
    set_char(bottom - 4, right, decode_char('|'), bg);
    set_char(9, 0, decode_char('|'), bg);
    set_char(11, 0, decode_char('|'), bg);
    set_char(13, 0, decode_char('|'), bg);
    set_char(2, 15, decode_char('*'), bg);
    print_mapped_str(5, right - 13, "{********}", bg);
}

void draw_context_menu() {
    int i; 
    unsigned char mnu_color = get_bg_color();
    unsigned char hl_color = get_hl_color();
    unsigned char cr_color = get_cr_color(); 
    const char* ctx_menu[] = {
        "[**************]",
        "| Undo         |",
        "| Redo         |",
        "@**************$",
        "| Cut          |",
        "| Copy         |",
        "| Paste        |",
        "| Delete       |",
        "@**************$",
        "| Select All   |",
        "{**************}"
    };
    for (i = 0; i < 11; i++) {
        print_mapped_str(ctx_y + i, ctx_x, ctx_menu[i], mnu_color);
    }
    
    if (setting_hover_effects && mouse_y >= ctx_y + 1 && mouse_y <= ctx_y + 10 && mouse_x >= ctx_x + 1 && mouse_x <= ctx_x + 14) {
        if (mouse_y == ctx_y + 1) print_str(ctx_y + 1, ctx_x + 1, " Undo         ", hl_color);
        else if (mouse_y == ctx_y + 2) print_str(ctx_y + 2, ctx_x + 1, " Redo         ", hl_color);
        else if (mouse_y == ctx_y + 4) print_str(ctx_y + 4, ctx_x + 1, " Cut          ", hl_color);
        else if (mouse_y == ctx_y + 5) print_str(ctx_y + 5, ctx_x + 1, " Copy         ", hl_color);
        else if (mouse_y == ctx_y + 6) print_str(ctx_y + 6, ctx_x + 1, " Paste        ", hl_color);
        else if (mouse_y == ctx_y + 7) print_str(ctx_y + 7, ctx_x + 1, " Delete       ", cr_color);
        else if (mouse_y == ctx_y + 9) print_str(ctx_y + 9, ctx_x + 1, " Select All   ", hl_color);
    }
}


void draw_file_dialog_element(int id, int is_hover) {
    unsigned char bg = get_bg_color(), hl = get_hl_color(), cr = get_cr_color();
    unsigned char c_col = is_hover ? hl : bg;
    
    /* Preserve keyboard tab-navigation highlights when mouse un-hovers */
    if (!is_hover) {
        if (id == 4 && active_tab_element == 10) c_col = cr;
        else if (id == 5 && active_tab_element == 8) c_col = hl;
        else if (id == 6 && active_tab_element == 9) c_col = hl;
        else if (id == 70 && active_tab_element == 4) c_col = hl;
        else if (id == 71 && active_tab_element == 5) c_col = hl;
        else if (id == 72 && active_tab_element == 6) c_col = hl;
    } else if (id == 4) {
        c_col = cr; /* X button is always red on hover */
    }
    
    int right = SCREEN_COLS - 1;
    int bottom = SCREEN_ROWS - 1;
    
    if (SCREEN_COLS < 80) {
        if (id == 4) print_str(1, 36, " X ", c_col);
        else if (id == 5) print_str(20, 24, " ok ", c_col);
        else if (id == 6) print_str(20, 31, "Cancel", c_col);
        else if (id == 65) set_char(7, 36, decode_char('1'), c_col);
        else if (id == 66) set_char(17, 36, decode_char('2'), c_col);
        else if (id == 70) print_mapped_str(4, 3, "4", c_col);
        else if (id == 71) print_mapped_str(4, 6, "5", c_col);
        else if (id == 72) print_mapped_str(4, 10, "1", c_col);
    } else {
        if (id == 4) print_str(1, right - 3, " X ", c_col);
        else if (id == 5) print_str(bottom - 4, right - 17, "  Ok  ", c_col);
        else if (id == 6) print_str(bottom - 4, right - 8, "Cancel", c_col);
        else if (id == 65) set_char(7, right - 3, decode_char('1'), c_col);
        else if (id == 66) set_char(bottom - 7, right - 3, decode_char('2'), c_col);
        else if (id == 70) print_mapped_str(4, 3, "4*", c_col);
        else if (id == 71) print_mapped_str(4, 8, "*5", c_col);
        else if (id == 72) print_mapped_str(4, 13, "1", c_col);
    }
}

void draw_file_dialog(int is_save) {
    int r, i; 
    unsigned char bg_color = get_bg_color(), hl_color = get_hl_color(), cr_color = get_cr_color(); 
    char pdisp[256], sdisp[256], idisp[256], statdisp[256]; 
    char* active_path;

    int right = SCREEN_COLS - 1;
    int bottom = SCREEN_ROWS - 1;

    if (SCREEN_COLS < 80) {
        static const char* fd40[25] = {
            "[**********************************^***]", 
            "|                                  | X |", 
            "@**********************************_***$", 
            "| [*][*] [*] [**************] [******] |", 
            "| |4||5| |1| |              | |Search| |", 
            "| {*}{*} {*} {**************} {******} |", 
            "| [********] [*********************^*] |", 
            "| | jmp to | | Title   | typ | siz |1| |", 
            "| @********$ @*********************+*$ |", 
            "| |   C:   | |                     |?| |", 
            "| |   D:   | |                     | | |", 
            "| |   E:   | |                     | | |", 
            "| |   F:   | |                     | | |", 
            "| |   G:   | |                     | | |", 
            "| |   H:   | |                     | | |", 
            "| |        | |                     | | |", 
            "| |        | |                     | | |", 
            "| |        | |                     |2| |", 
            "| {********} {*********************_*} |", 
            "|            [*******] [****] [******] |", 
            "| File Name: |       | | ok | |Cancel| |", 
            "|            {*******} {****} {******} |", 
            "@***********^**************************$", 
            "| Open File |                          |", 
            "{***********_**************************}" 
        };
        for (r = 0; r < 25; r++) print_mapped_str(r, 0, fd40[r], bg_color);
        if (is_save) print_str(23, 2, " Save As ", bg_color); 
        else print_str(23, 2, "Open File", bg_color);

        char title_buf[42];
        strcpy(title_buf, is_document_dirty ? "*" : ""); strncat(title_buf, current_filename, 40); 
        if (strlen(title_buf) > 32) { strcpy(&title_buf[29], "..."); }
        print_str(1, 2, title_buf, bg_color);

        active_path = (active_input_field == 3) ? dialog_address_text : current_browser_path;
        if (active_input_field == 3 && dialog_address_cursor > 14) { 
            strncpy(pdisp, active_path + (dialog_address_cursor - 14), 14); pdisp[14] = '\0'; 
        } else { 
            if (strlen(active_path) > 14) { strncpy(pdisp, active_path, 11); pdisp[11] = '\0'; strcat(pdisp, "..."); } 
            else { strcpy(pdisp, active_path); while(strlen(pdisp) < 14) strcat(pdisp, " "); } 
        }
        print_str(4, 14, pdisp, bg_color);

        if (strlen(dialog_search_text) == 0 && active_input_field != 2) { strcpy(sdisp, "Search"); while(strlen(sdisp)<6) strcat(sdisp," "); } 
        else { 
            if (active_input_field == 2 && dialog_search_cursor > 5) {
                strncpy(sdisp, dialog_search_text + (dialog_search_cursor - 5), 6); sdisp[6] = '\0';
            } else {
                if (strlen(dialog_search_text) > 6) { 
                    if (active_input_field == 2) { strncpy(sdisp, dialog_search_text, 6); sdisp[6] = '\0'; }
                    else { strcpy(sdisp, dialog_search_text + (strlen(dialog_search_text) - 6)); } 
                } else { strcpy(sdisp, dialog_search_text); while(strlen(sdisp) < 6) strcat(sdisp, " "); } 
            }
        }
        print_str(4, 31, sdisp, bg_color);
    } else {
        // Fill Canvas dynamically
        for(r = 0; r <= bottom; r++) for(i = 0; i <= right; i++) set_char(r, i, ' ', bg_color);
        
        // Borders and structure
        set_char(0, 0, decode_char('['), bg_color); for(i=1;i<right;i++) set_char(0,i,decode_char('*'), bg_color); set_char(0,right,decode_char(']'), bg_color);
        set_char(0,right-4,decode_char('^'), bg_color);
        print_mapped_str(1, 0, "|", bg_color);
        set_char(1, right-4, decode_char('|'), bg_color); print_str(1, right-3, " X ", bg_color); set_char(1, right, decode_char('|'), bg_color);
        
        char title_buf[150]; strcpy(title_buf, is_document_dirty ? "*" : ""); /* Upgraded for 132 column mode */
        strncat(title_buf, current_filename, 140); 
        if (strlen(title_buf) > (right - 7)) strcpy(&title_buf[right - 10], "...");
        print_str(1, 2, title_buf, bg_color);
        
        set_char(2, 0, decode_char('@'), bg_color);
        for(i=1;i<right;i++) set_char(2,i,decode_char('*'), bg_color); 
        set_char(2,right,decode_char('$'), bg_color);
        set_char(2,right-4,decode_char('_'), bg_color);

        print_mapped_str(3, 0, "| [**] [**] [*] [", bg_color); for(i=17; i<right-19; i++) set_char(3, i, decode_char('*'), bg_color); print_mapped_str(3, right-19, "] [**************] |", bg_color);
        print_mapped_str(4, 0, "| |4*| |*5| |1| |", bg_color); set_char(4, right-19, decode_char('|'), bg_color); set_char(4, right-17, decode_char('|'), bg_color); set_char(4, right-2, decode_char('|'), bg_color); set_char(4, right, decode_char('|'), bg_color);
        print_mapped_str(5, 0, "| {**} {**} {*} {", bg_color); for(i=17; i<right-19; i++) set_char(5, i, decode_char('*'), bg_color); print_mapped_str(5, right-19, "} {**************} |", bg_color);

        print_mapped_str(6, 0, "| [***********] [", bg_color); for(i=17; i<right-4; i++) set_char(6, i, decode_char('*'), bg_color); print_mapped_str(6, right-4, "^*] |", bg_color);
        
        print_mapped_str(7, 0, "| |  Jump to  | | Title", bg_color); 
        for(i=23; i<right-20; i++) set_char(7, i, ' ', bg_color); 
        print_mapped_str(6, right-20, "^********^******", bg_color);
        print_mapped_str(7, right-20, "|  Type  | Size |1| |", bg_color);
        
        /* RESTORED COLUMN SEPARATORS */
        print_mapped_str(8, 0, "| @***********$ @", bg_color); 
        for(i=17; i<right; i++) set_char(8, i, decode_char('*'), bg_color); 
        set_char(8, right-20, decode_char('+'), bg_color);
        set_char(8, right-11, decode_char('+'), bg_color);
        set_char(8, right-4, decode_char('+'), bg_color); 
        set_char(8, right-2, decode_char('$'), bg_color); 
        set_char(8, right-1, ' ', bg_color); /* FIX: Remove rogue horizontal line in top right! */
        set_char(8, right, decode_char('|'), bg_color);

        for(r=9; r<=bottom-7; r++) {
            print_mapped_str(r, 0, "| |           | |", bg_color);
            set_char(r, right-20, decode_char('|'), bg_color); 
            set_char(r, right-11, decode_char('|'), bg_color); 
            set_char(r, right-4, decode_char('|'), bg_color); 
            set_char(r, right-2, decode_char('|'), bg_color); 
            set_char(r, right, decode_char('|'), bg_color);
            if (r-9 < 8) print_str(r, 7, (r-9==0)?"C:":((r-9==1)?"D:":((r-9==2)?"E:":((r-9==3)?"F:":((r-9==4)?"G:":((r-9==5)?"H:":((r-9==6)?"I:":"J:")))))), bg_color);
        }

        print_mapped_str(bottom-6, 0, "| {***********} {", bg_color); 
        for(i=17; i<right-4; i++) set_char(bottom-6, i, decode_char('*'), bg_color);
        set_char(bottom-6, right-20, decode_char('_'), bg_color); 
        set_char(bottom-6, right-11, decode_char('_'), bg_color); 
        set_char(bottom-6, right-4, decode_char('_'), bg_color); 
        set_char(bottom-6, right-3, decode_char('*'), bg_color);
        set_char(bottom-6, right-2, decode_char('}'), bg_color); 
        set_char(bottom-6, right-1, ' ', bg_color);
        set_char(bottom-6, right, decode_char('|'), bg_color);

        set_char(bottom-5, 0, decode_char('|'), bg_color); set_char(bottom-5, right, decode_char('|'), bg_color); print_mapped_str(bottom-5, 16, "[", bg_color); for(i=17; i<right-20; i++) set_char(bottom-5, i, decode_char('*'), bg_color); print_mapped_str(bottom-5, right-20, "] [******] [******]", bg_color);
        set_char(bottom-4, 0, decode_char('|'), bg_color); set_char(bottom-4, right, decode_char('|'), bg_color); print_mapped_str(bottom-4, 4, "File Name:  |", bg_color); set_char(bottom-4, right-20, decode_char('|'), bg_color); print_mapped_str(bottom-4, right-18, "|  Ok  | |Cancel|", bg_color);
        set_char(bottom-3, 0, decode_char('|'), bg_color); set_char(bottom-3, right, decode_char('|'), bg_color); print_mapped_str(bottom-3, 16, "{", bg_color); for(i=17; i<right-20; i++) set_char(bottom-3, i, decode_char('*'), bg_color); print_mapped_str(bottom-3, right-20, "} {******} {******}", bg_color);

        set_char(bottom-2, 0, decode_char('@'), bg_color); for(i=1; i<12; i++) set_char(bottom-2, i, decode_char('*'), bg_color); set_char(bottom-2, 12, decode_char('^'), bg_color); for(i=13; i<right; i++) set_char(bottom-2, i, decode_char('*'), bg_color); set_char(bottom-2, right, decode_char('$'), bg_color);
        print_mapped_str(bottom-1, 0, "|           |", bg_color); set_char(bottom-1, right, decode_char('|'), bg_color);
        set_char(bottom, 0, decode_char('{'), bg_color); for(i=1; i<12; i++) set_char(bottom, i, decode_char('*'), bg_color); set_char(bottom, 12, decode_char('_'), bg_color); for(i=13; i<right; i++) set_char(bottom, i, decode_char('*'), bg_color); set_char(bottom, right, decode_char('}'), bg_color);

        if (is_save) print_str(bottom-1, 2, " Save As ", bg_color); else print_str(bottom-1, 2, "Open File", bg_color);

        active_path = (active_input_field == 3) ? dialog_address_text : current_browser_path;
        int addr_w = (right - 19) - 18; 
        if (active_input_field == 3 && dialog_address_cursor > addr_w - 1) { 
            strncpy(pdisp, active_path + (dialog_address_cursor - (addr_w - 1)), addr_w); pdisp[addr_w] = '\0'; 
        } else { 
            if (strlen(active_path) > addr_w) { strncpy(pdisp, active_path, addr_w - 3); pdisp[addr_w-3] = '\0'; strcat(pdisp, "..."); } 
            else { strcpy(pdisp, active_path); while(strlen(pdisp) < addr_w) strcat(pdisp, " "); } 
        }
        print_str(4, 18, pdisp, bg_color);

        if (strlen(dialog_search_text) == 0 && active_input_field != 2) { strcpy(sdisp, "Search here"); while(strlen(sdisp) < 12) strcat(sdisp, " "); } 
        else { 
            if (active_input_field == 2 && dialog_search_cursor > 11) {
                strncpy(sdisp, dialog_search_text + (dialog_search_cursor - 11), 12); sdisp[12] = '\0';
            } else {
                if (strlen(dialog_search_text) > 12) { 
                    if (active_input_field == 2) { strncpy(sdisp, dialog_search_text, 12); sdisp[12] = '\0'; }
                    else { strcpy(sdisp, dialog_search_text + (strlen(dialog_search_text) - 12)); } 
                } else { strcpy(sdisp, dialog_search_text); while(strlen(sdisp) < 12) strcat(sdisp, " "); } 
            }
        }
        print_str(4, right - 15, sdisp, bg_color);
    }
    
    // Core List Rendering & Scrollbar
    int lsize = num_browser_files; 
    int max_items = (SCREEN_COLS < 80) ? 8 : (bottom - 15); /* FULL list height */
    int track_h = (SCREEN_COLS < 80) ? 8 : (bottom - 16);   /* Track stops 1 line short for arrow */
    int sc_down_y = (SCREEN_COLS < 80) ? 17 : (bottom - 7);
    
    int tsize = lsize > max_items ? (int)((track_h * 1L * track_h) / lsize) : track_h; 
    if (tsize < 1) tsize = 1; 
    int tpos = lsize > max_items ? (int)((long)browser_scroll_offset * (track_h - tsize) / (lsize - max_items)) : 0;
    
    for (i = 0; i < track_h; i++) {
        if (lsize <= max_items || (i >= tpos && i < tpos + tsize)) {
            set_char(9 + i, (SCREEN_COLS < 80) ? 36 : right - 3, ' ', get_cursor_color());
        } else {
            set_char(9 + i, (SCREEN_COLS < 80) ? 36 : right - 3, ' ', bg_color);
        }
    }
    if (SCREEN_COLS >= 80) {
        set_char(sc_down_y, right - 3, decode_char('2'), bg_color); /* Explicitly draw down arrow! */
    }
    
    for (i = 0; i < max_items && (browser_scroll_offset + i) < lsize; i++) {
        int idx = browser_scroll_offset + i; 
        unsigned char c_col = (unsigned char)(((idx == browser_selected_index) || (active_tab_element == 7 && idx == (browser_scroll_offset))) ? hl_color : bg_color); 
        
        if (SCREEN_COLS < 80) {
            char n_buf[20], s_buf[10];
            int n_len;
            
            if (c_col == hl_color) {
                /* FIX: Paint the full row background so the highlight is contiguous */
                int fill_c;
                for (fill_c = 14; fill_c < 35; fill_c++) set_char(9 + i, fill_c, ' ', c_col);
            }
            
            n_len = (int)strlen(browser_files[idx].name);
            if (n_len > 8) { strncpy(n_buf, browser_files[idx].name, 5); n_buf[5] = '\0'; strcat(n_buf, "..."); } else { strcpy(n_buf, browser_files[idx].name); }
            while(strlen(n_buf) < 9) strcat(n_buf, " ");
            
            if (browser_files[idx].is_dir) { strcpy(s_buf, "     "); } 
            else { 
                long kb = browser_files[idx].size / 1024; if (kb == 0 && browser_files[idx].size > 0) kb = 1; 
                if (kb > 999) { long mb = kb / 1024; if (mb == 0) mb = 1; sprintf(s_buf, " %ldM", mb); } 
                else { sprintf(s_buf, " %ldK", kb); }
                while(strlen(s_buf) < 5) strcat(s_buf, " "); 
            }
            print_str(9 + i, 15, n_buf, c_col);
            print_str(9 + i, 24, browser_files[idx].is_dir ? " Fol " : " Txt ", c_col);
            print_str(9 + i, 30, s_buf, c_col);
        } else {
            char n_buf[256], t_buf[15], s_buf[15];
            int name_width = right - 39; 
            
            if (c_col == hl_color) {
                /* FIX: Paint the full row background and pipes so the highlight is perfectly contiguous */
                int fill_c;
                for (fill_c = 17; fill_c < right - 4; fill_c++) {
                    char ch_to_draw = ' ';
                    if (fill_c == right - 20 || fill_c == right - 11) ch_to_draw = decode_char('|');
                    set_char(9 + i, fill_c, ch_to_draw, c_col);
                }
            }
            
            if (strlen(browser_files[idx].name) > name_width) { 
                strncpy(n_buf, browser_files[idx].name, name_width - 3); n_buf[name_width-3] = '\0'; strcat(n_buf, "...");
            } else { 
                strcpy(n_buf, browser_files[idx].name); while(strlen(n_buf) < name_width) strcat(n_buf, " "); 
            }
            if (browser_files[idx].is_dir) { strcpy(t_buf, "Folder"); strcpy(s_buf, " "); } 
            else { 
                strcpy(t_buf, "Text");
                long kb = browser_files[idx].size / 1024; if (kb == 0 && browser_files[idx].size > 0) kb = 1; 
                if (kb > 999) { long mb = kb / 1024; if (mb == 0) mb = 1; sprintf(s_buf, "%ldMB", mb); } 
                else { sprintf(s_buf, "%ldKB", kb); }
            }
            while(strlen(t_buf) < 7) strcat(t_buf, " "); 
            while(strlen(s_buf) < 5) strcat(s_buf, " "); 
            
            /* Print separately so it NEVER overwrites the vertical borders */
            print_str(9 + i, 18, n_buf, c_col); 
            print_str(9 + i, right - 18, t_buf, c_col); 
            print_str(9 + i, right - 9, s_buf, c_col); 
        }
    }
    
    // File Name Inputs & Buttons
    if (SCREEN_COLS < 80) {
        if (active_input_field == 1 && dialog_input_cursor > 6) {
            strncpy(idisp, dialog_input_text + (dialog_input_cursor - 6), 7); idisp[7] = '\0';
        } else {
            if (strlen(dialog_input_text) > 7) {
                if (active_input_field == 1) { strncpy(idisp, dialog_input_text, 7); idisp[7] = '\0'; } 
                else { strcpy(idisp, dialog_input_text + (strlen(dialog_input_text) - 7)); }
            } else { strcpy(idisp, dialog_input_text); while(strlen(idisp) < 7) strcat(idisp, " "); }
        }
        print_str(20, 14, idisp, bg_color);
        
        if (strlen(dialog_status_msg) > 23) { strncpy(statdisp, dialog_status_msg, 23); statdisp[23] = '\0'; } 
        else { strcpy(statdisp, dialog_status_msg); while(strlen(statdisp) < 23) strcat(statdisp, " "); }
        print_str(23, 14, statdisp, bg_color);

        if ((setting_hover_effects && mouse_y == 20 && mouse_x >= 23 && mouse_x <= 28) || active_tab_element == 8) print_str(20, 24, " ok ", hl_color);
        if ((setting_hover_effects && mouse_y == 20 && mouse_x >= 30 && mouse_x <= 37) || active_tab_element == 9) print_str(20, 31, "Cancel", hl_color);
        if ((setting_hover_effects && mouse_y == 1 && mouse_x >= 36 && mouse_x <= 38) || active_tab_element == 10) print_str(1, 36, " X ", cr_color);
        if (setting_hover_effects && mouse_y == 7 && mouse_x == 36) set_char(7, 36, decode_char('1'), hl_color);
        if (setting_hover_effects && mouse_y == 17 && mouse_x == 36) set_char(17, 36, decode_char('2'), hl_color);
        if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 2 && mouse_x <= 4) || active_tab_element == 4) print_mapped_str(4, 3, "4", hl_color);
        if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 5 && mouse_x <= 7) || active_tab_element == 5) print_mapped_str(4, 6, "5", hl_color);
        if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 9 && mouse_x <= 11) || active_tab_element == 6) print_mapped_str(4, 10, "1", hl_color);
    } else {
        int in_w = right - 37; /* expanded by 2 characters to fill new space */
        if (active_input_field == 1 && dialog_input_cursor > in_w - 1) {
            strncpy(idisp, dialog_input_text + (dialog_input_cursor - (in_w - 1)), in_w); idisp[in_w] = '\0';
        } else {
            if (strlen(dialog_input_text) > in_w) { 
                if (active_input_field == 1) { strncpy(idisp, dialog_input_text, in_w); idisp[in_w] = '\0'; }
                else { strcpy(idisp, dialog_input_text + (strlen(dialog_input_text) - in_w)); } 
            } else { strcpy(idisp, dialog_input_text); while(strlen(idisp) < in_w) strcat(idisp, " "); }
        }
        print_str(bottom - 4, 17, idisp, bg_color); /* shifted 1 column left */
        
        if (strlen(dialog_status_msg) > right - 17) { strncpy(statdisp, dialog_status_msg, right - 17); statdisp[right - 17] = '\0'; } 
        else { strcpy(statdisp, dialog_status_msg); while(strlen(statdisp) < right - 17) strcat(statdisp, " "); }
        print_str(bottom - 1, 14, statdisp, bg_color);

        if ((setting_hover_effects && mouse_y == bottom - 4 && mouse_x >= right - 17 && mouse_x <= right - 12) || active_tab_element == 8) print_str(bottom - 4, right - 17, "  Ok  ", hl_color);
        if ((setting_hover_effects && mouse_y == bottom - 4 && mouse_x >= right - 8 && mouse_x <= right - 3) || active_tab_element == 9) print_str(bottom - 4, right - 8, "Cancel", hl_color);
        if ((setting_hover_effects && mouse_y == 1 && mouse_x >= right - 3 && mouse_x <= right - 1) || active_tab_element == 10) print_str(1, right - 3, " X ", cr_color);
        if (setting_hover_effects && mouse_y == 7 && mouse_x == right - 3) set_char(7, right - 3, decode_char('1'), hl_color);
        if (setting_hover_effects && mouse_y == bottom - 7 && mouse_x == right - 3) set_char(bottom - 7, right - 3, decode_char('2'), hl_color);
        if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 2 && mouse_x <= 5) || active_tab_element == 4) print_mapped_str(4, 3, "4*", hl_color);
        if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 7 && mouse_x <= 10) || active_tab_element == 5) print_mapped_str(4, 8, "*5", hl_color);
        if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 12 && mouse_x <= 14) || active_tab_element == 6) print_mapped_str(4, 13, "1", hl_color);
    }
    
    // Live Input Cursor Tracking
    if (active_input_field > 0) {
        int cx = 0, cy = 0;
        if (SCREEN_COLS < 80) {
            if (active_input_field == 1) { int dcur = dialog_input_cursor; if (dcur > 6) cx = 14 + 6; else cx = 14 + dcur; cy = 20; }
            else if (active_input_field == 2) { int dcur = dialog_search_cursor; if (dcur > 5) cx = 31 + 5; else cx = 31 + dcur; cy = 4; }
            else if (active_input_field == 3) { int dcur = dialog_address_cursor; if (dcur > 13) cx = 14 + 13; else cx = 14 + dcur; cy = 4; }
        } else {
            int addr_w = right - 37;
            int in_w = right - 37;
            if (active_input_field == 1) { int dcur = dialog_input_cursor; if (dcur > in_w - 1) cx = 17 + in_w - 1; else cx = 17 + dcur; cy = bottom - 4; }
            else if (active_input_field == 2) { int dcur = dialog_search_cursor; if (dcur > 11) cx = (right - 15) + 11; else cx = (right - 15) + dcur; cy = 4; }
            else if (active_input_field == 3) { int dcur = dialog_address_cursor; if (dcur > addr_w - 1) cx = 18 + addr_w - 1; else cx = 18 + dcur; cy = 4; }
        }
        unsigned char cur_col = get_cursor_color();
        
        /* 16-BIT CURSOR RENDERING */
        char far* video = (char far*)MK_FP(video_segment, (cy * SCREEN_COLS + cx) * 2);
        char char_under = *video;
        set_char(cy, cx, char_under, cur_col);
    }
}

void draw_menu() {
    int i; 
    unsigned char mnu_color = get_bg_color(), hl_color = get_hl_color(), cr_color = get_cr_color(); 
    
    static const char* file_menu[] = { 
        "@******_******_^", 
        "| New File     |", 
        "| Open         |", 
        "| Save         |", 
        "| Save As      |", 
        "@**************$", 
        "| Clear Page   |", 
        "@**************$", 
        "| Settings     |", 
        "@**************$", 
        "| Exit         |", 
        "@**************}" 
    };
    
    static const char* edit_menu[] = { 
        "+******_******_^", 
        "| Undo         |", 
        "| Redo         |", 
        "@**************$", 
        "| Cut          |", 
        "| Copy         |", 
        "| Paste        |", 
        "| Delete       |", 
        "@**************$", 
        "| Select All   |", 
        "| Read-Only    |", 
        "{**************}" 
    };
    
    static const char* view_menu[] = { 
        "+******_**************^", 
        "| Enable Horizontal   |", 
        "| Scroll Bar          |", 
        "@*********************$", 
        "| Enable Vertical     |", 
        "| Scroll Bar          |", 
        "@*********************$", 
        "| Enable Word Wrap    |", 
        "@*********************$", 
        "| Respect Whole Words |", 
        "{*********************}" 
    };
    
    if (active_menu == 1) {
        for (i = 0; i < 12; i++) print_mapped_str(2 + i, 0, file_menu[i], mnu_color);
        if (active_menu_hover == 0) print_str(3, 1, " New File     ", hl_color); 
        else if (active_menu_hover == 1) print_str(4, 1, " Open         ", hl_color);
        else if (active_menu_hover == 2) print_str(5, 1, " Save         ", hl_color); 
        else if (active_menu_hover == 3) print_str(6, 1, " Save As      ", hl_color);
        else if (active_menu_hover == 4) print_str(8, 1, " Clear Page   ", hl_color); 
        else if (active_menu_hover == 5) print_str(10, 1, " Settings     ", hl_color);
        else if (active_menu_hover == 6) print_str(12, 1, " Exit         ", cr_color);
        
        if (setting_hover_effects && mouse_y >= 3 && mouse_y <= 12 && mouse_x >= 1 && mouse_x <= 14) {
            if (mouse_y == 3) print_str(3, 1, " New File     ", hl_color); 
            else if (mouse_y == 4) print_str(4, 1, " Open         ", hl_color);
            else if (mouse_y == 5) print_str(5, 1, " Save         ", hl_color); 
            else if (mouse_y == 6) print_str(6, 1, " Save As      ", hl_color);
            else if (mouse_y == 8) print_str(8, 1, " Clear Page   ", hl_color); 
            else if (mouse_y == 10) print_str(10, 1, " Settings     ", hl_color);
            else if (mouse_y == 12) print_str(12, 1, " Exit         ", cr_color);
        }
    } else if (active_menu == 2) {
        /* Declare the variables at the top of the scope! */
        char ro_buf[20];
        char cb = setting_ansi_mode == 1 ? '\xFB' : 'V';
        sprintf(ro_buf, " Read-Only [%c]", setting_read_only ? cb : ' ');

        for (i = 0; i < 12; i++) print_mapped_str(2 + i, 7, edit_menu[i], mnu_color);
        if (active_menu_hover == 0) print_str(3, 8, " Undo         ", hl_color); 
        else if (active_menu_hover == 1) print_str(4, 8, " Redo         ", hl_color);
        else if (active_menu_hover == 2) print_str(6, 8, " Cut          ", hl_color); 
        else if (active_menu_hover == 3) print_str(7, 8, " Copy         ", hl_color);
        else if (active_menu_hover == 4) print_str(8, 8, " Paste        ", hl_color); 
        else if (active_menu_hover == 5) print_str(9, 8, " Delete       ", cr_color);
        else if (active_menu_hover == 6) print_str(11, 8, " Select All   ", hl_color);
        else if (active_menu_hover == 7) print_str(12, 8, ro_buf, hl_color);
        else print_str(12, 8, ro_buf, mnu_color);

        if (setting_hover_effects && mouse_y >= 3 && mouse_y <= 12 && mouse_x >= 8 && mouse_x <= 21) {
            if (mouse_y == 3) print_str(3, 8, " Undo         ", hl_color); 
            else if (mouse_y == 4) print_str(4, 8, " Redo         ", hl_color);
            else if (mouse_y == 6) print_str(6, 8, " Cut          ", hl_color); 
            else if (mouse_y == 7) print_str(7, 8, " Copy         ", hl_color);
            else if (mouse_y == 8) print_str(8, 8, " Paste        ", hl_color); 
            else if (mouse_y == 9) print_str(9, 8, " Delete       ", cr_color);
            else if (mouse_y == 11) print_str(11, 8, " Select All   ", hl_color);
            else if (mouse_y == 12) print_str(12, 8, ro_buf, hl_color);
        }
    } else if (active_menu == 3) {
        for (i = 0; i < 11; i++) print_mapped_str(2 + i, 14, view_menu[i], mnu_color);
        if (active_menu_hover == 0) { print_str(3, 15, " Enable Horizontal   ", hl_color); print_str(4, 15, " Scroll Bar          ", hl_color); }
        else if (active_menu_hover == 1) { print_str(6, 15, " Enable Vertical     ", hl_color); print_str(7, 15, " Scroll Bar          ", hl_color); }
        else if (active_menu_hover == 2) print_str(9, 15, " Enable Word Wrap    ", hl_color); 
        else if (active_menu_hover == 3) print_str(11, 15, " Respect Whole Words ", hl_color);

        if (setting_hover_effects && mouse_y >= 3 && mouse_y <= 11 && mouse_x >= 15 && mouse_x <= 35) {
            if (mouse_y == 3 || mouse_y == 4) { print_str(3, 15, " Enable Horizontal   ", hl_color); print_str(4, 15, " Scroll Bar          ", hl_color); }
            else if (mouse_y == 6 || mouse_y == 7) { print_str(6, 15, " Enable Vertical     ", hl_color); print_str(7, 15, " Scroll Bar          ", hl_color); }
            else if (mouse_y == 9) { print_str(9, 15, " Enable Word Wrap    ", hl_color); }
            else if (mouse_y == 11) { print_str(11, 15, " Respect Whole Words ", hl_color); }
        }
    }
}

void draw_exit_dialog() {
    int i, title_len; 
    unsigned char dlg_color = get_bg_color(), hl_color = get_hl_color(), cr_color = get_cr_color(); 
    char fname_buf[40];
    
    int is_40 = (SCREEN_COLS < 80);
    int box_w = is_40 ? 38 : 44;
    int start_x = (SCREEN_COLS - box_w) / 2;
    int start_y = (SCREEN_ROWS - 10) / 2;
    
    static const char* d80[10] = { 
        "[******************************************]", 
        "|             Unsaved Changes              |", 
        "@******************************************$", 
        "| Do you want to save changes to           |", 
        "|                                          |", 
        "|                                          |", 
        "|   [******]   [************]   [******]   |", 
        "|   | Save |   | Don't Save |   |Cancel|   |", 
        "|   {******}   {************}   {******}   |", 
        "{******************************************}" 
    };

    static const char* d40[10] = { 
        "[************************************]", 
        "|          Unsaved Changes           |", 
        "@************************************$", 
        "| Do you want to save changes to     |", 
        "|                                    |", 
        "|                                    |", 
        "| [******]  [************]  [******] |", 
        "| | Save |  | Don't Save |  |Cancel| |", 
        "| {******}  {************}  {******} |", 
        "{************************************}" 
    };
    
    const char** d = is_40 ? d40 : d80;
    for(i = 0; i < 10; i++) print_mapped_str(start_y + i, start_x, d[i], dlg_color);
    
    title_len = (int)strlen(current_filename);
    int max_t_len = is_40 ? 32 : 38;
    if (title_len > max_t_len - 3) { 
        strncpy(fname_buf, current_filename, max_t_len - 3); 
        fname_buf[max_t_len - 3] = '\0'; 
        strcat(fname_buf, "..."); 
    } else {
        strcpy(fname_buf, current_filename);
    }
    print_str(start_y + 4, start_x + 2, fname_buf, dlg_color);
    
    int s1 = is_40 ? 2 : 4;  int e1 = is_40 ? 9 : 11;
    int s2 = is_40 ? 12 : 15; int e2 = is_40 ? 25 : 28;
    int s3 = is_40 ? 28 : 32; int e3 = is_40 ? 35 : 39;

    if ((setting_hover_effects && mouse_y == start_y + 7 && mouse_x >= start_x + s1 && mouse_x <= start_x + e1) || active_tab_element == 1) print_str(start_y + 7, start_x + s1 + 1, " Save ", hl_color); 
    if ((setting_hover_effects && mouse_y == start_y + 7 && mouse_x >= start_x + s2 && mouse_x <= start_x + e2) || active_tab_element == 2) print_str(start_y + 7, start_x + s2 + 1, " Don't Save ", hl_color);
    if ((setting_hover_effects && mouse_y == start_y + 7 && mouse_x >= start_x + s3 && mouse_x <= start_x + e3) || active_tab_element == 3) print_str(start_y + 7, start_x + s3 + 1, "Cancel", cr_color); 
}

void draw_find_dialog() {
    unsigned char bg = get_bg_color(), hl = get_hl_color();
    int c;
    char fdisp[40], rdisp[40];
    int right = SCREEN_COLS - 1;
    int s_div = right - 2;
    int end_col = setting_v_scrollbar ? s_div : right;

    for(int r = 3; r <= 8; r++) {
        for(c = 1; c < end_col; c++) {
            set_char(r, c, ' ', bg);
        }
    }

    if (SCREEN_COLS < 80) {
        print_str(4, 2, "Find:", bg);
        print_str(7, 2, "Replace:", bg);

        print_mapped_str(3, 11, "[********]", bg);
        print_mapped_str(4, 11, "|        |", bg);
        print_mapped_str(5, 11, "{********}", bg);

        print_mapped_str(6, 11, "[********]", bg);
        print_mapped_str(7, 11, "|        |", bg);
        print_mapped_str(8, 11, "{********}", bg);

        unsigned char c_fnd = (active_tab_element==21 || (setting_hover_effects && mouse_y>=3 && mouse_y<=5 && mouse_x>=22 && mouse_x<=28)) ? hl : bg;
        print_mapped_str(3, 22, "[*****]", bg); 
        set_char(4, 22, decode_char('|'), bg); print_str(4, 23, " Nxt ", c_fnd); set_char(4, 28, decode_char('|'), bg);
        print_mapped_str(5, 22, "{*****}", bg);

        unsigned char c_nxt = (active_tab_element==22 || (setting_hover_effects && mouse_y>=3 && mouse_y<=5 && mouse_x>=30 && mouse_x<=36)) ? hl : bg;
        print_mapped_str(3, 30, "[*****]", bg); 
        set_char(4, 30, decode_char('|'), bg); print_str(4, 31, " Prv ", c_nxt); set_char(4, 36, decode_char('|'), bg);
        print_mapped_str(5, 30, "{*****}", bg);

        unsigned char c_rep = (active_tab_element==23 || (setting_hover_effects && mouse_y>=6 && mouse_y<=8 && mouse_x>=22 && mouse_x<=28)) ? hl : bg;
        print_mapped_str(6, 22, "[*****]", bg); 
        set_char(7, 22, decode_char('|'), bg); print_str(7, 23, " Rep ", c_rep); set_char(7, 28, decode_char('|'), bg);
        print_mapped_str(8, 22, "{*****}", bg);

        unsigned char c_all = (active_tab_element==24 || (setting_hover_effects && mouse_y>=6 && mouse_y<=8 && mouse_x>=30 && mouse_x<=36)) ? hl : bg;
        print_mapped_str(6, 30, "[*****]", bg); 
        set_char(7, 30, decode_char('|'), bg); print_str(7, 31, " All ", c_all); set_char(7, 36, decode_char('|'), bg);
        print_mapped_str(8, 30, "{*****}", bg);

        set_char(9, 0, decode_char('@'), bg);
        for(c = 1; c < end_col; c++) set_char(9, c, decode_char('*'), bg);
        set_char(9, end_col, decode_char(setting_v_scrollbar ? '\xB4' : '$'), bg);

        /* FIX: Sliding Camera for Find Box (40 Column) */
        if (active_input_field == 4 && find_cursor > 7) {
            strncpy(fdisp, find_text + (find_cursor - 7), 8); fdisp[8] = '\0';
        } else {
            if (strlen(find_text) > 8) { 
                if (active_input_field == 4) { strncpy(fdisp, find_text, 8); fdisp[8] = '\0'; }
                else { strcpy(fdisp, find_text + (strlen(find_text) - 8)); } 
            } else { strcpy(fdisp, find_text); while(strlen(fdisp) < 8) strcat(fdisp, " "); }
        }
        print_str(4, 12, fdisp, bg);

        /* FIX: Sliding Camera for Replace Box (40 Column) */
        if (active_input_field == 5 && replace_cursor > 7) {
            strncpy(rdisp, replace_text + (replace_cursor - 7), 8); rdisp[8] = '\0';
        } else {
            if (strlen(replace_text) > 8) { 
                if (active_input_field == 5) { strncpy(rdisp, replace_text, 8); rdisp[8] = '\0'; }
                else { strcpy(rdisp, replace_text + (strlen(replace_text) - 8)); } 
            } else { strcpy(rdisp, replace_text); while(strlen(rdisp) < 8) strcat(rdisp, " "); }
        }
        print_str(7, 12, rdisp, bg);

        /* 16-BIT CURSOR RENDERING */
        if (active_input_field == 4 || active_input_field == 5) {
            int cx = 12 + (active_input_field == 4 ? find_cursor : replace_cursor);
            if (cx > 19) cx = 19;
            int cy = active_input_field == 4 ? 4 : 7;
            unsigned char cur_col = get_cursor_color();
            
            char far* video = (char far*)MK_FP(video_segment, (cy * SCREEN_COLS + cx) * 2);
            char char_under = *video;
            set_char(cy, cx, char_under, cur_col);
        }
    } else {
        print_str(4, 2, "   Find:", bg);
        print_str(7, 2, "Replace:", bg);

        print_mapped_str(3, 11, "[***************************************]", bg);
        print_mapped_str(4, 11, "|                                       |", bg);
        print_mapped_str(5, 11, "{***************************************}", bg);

        print_mapped_str(6, 11, "[***************************************]", bg);
        print_mapped_str(7, 11, "|                                       |", bg);
        print_mapped_str(8, 11, "{***************************************}", bg);

        unsigned char c_fnd = (active_tab_element==21 || (setting_hover_effects && mouse_y>=3 && mouse_y<=5 && mouse_x>=53 && mouse_x<=63)) ? hl : bg;
        print_mapped_str(3, 53, "[*********]", bg); 
        set_char(4, 53, decode_char('|'), bg); print_str(4, 54, "  Next   ", c_fnd); set_char(4, 63, decode_char('|'), bg);
        print_mapped_str(5, 53, "{*********}", bg);

        unsigned char c_nxt = (active_tab_element==22 || (setting_hover_effects && mouse_y>=3 && mouse_y<=5 && mouse_x>=65 && mouse_x<=75)) ? hl : bg;
        print_mapped_str(3, 65, "[*********]", bg); 
        set_char(4, 65, decode_char('|'), bg); print_str(4, 66, "  Prev   ", c_nxt); set_char(4, 75, decode_char('|'), bg);
        print_mapped_str(5, 65, "{*********}", bg);

        unsigned char c_rep = (active_tab_element==23 || (setting_hover_effects && mouse_y>=6 && mouse_y<=8 && mouse_x>=53 && mouse_x<=63)) ? hl : bg;
        print_mapped_str(6, 53, "[*********]", bg); 
        set_char(7, 53, decode_char('|'), bg); print_str(7, 54, " Replace ", c_rep); set_char(7, 63, decode_char('|'), bg);
        print_mapped_str(8, 53, "{*********}", bg);

        unsigned char c_all = (active_tab_element==24 || (setting_hover_effects && mouse_y>=6 && mouse_y<=8 && mouse_x>=65 && mouse_x<=75)) ? hl : bg;
        print_mapped_str(6, 65, "[*********]", bg); 
        set_char(7, 65, decode_char('|'), bg); print_str(7, 66, "   All   ", c_all); set_char(7, 75, decode_char('|'), bg);
        print_mapped_str(8, 65, "{*********}", bg);

        set_char(9, 0, decode_char('@'), bg);
        for(c = 1; c < end_col; c++) set_char(9, c, decode_char('*'), bg);
        set_char(9, end_col, decode_char(setting_v_scrollbar ? '\xB4' : '$'), bg);

        /* FIX: Sliding Camera for Find Box (80+ Column) */
        if (active_input_field == 4 && find_cursor > 38) {
            strncpy(fdisp, find_text + (find_cursor - 38), 39); fdisp[39] = '\0';
        } else {
            if (strlen(find_text) > 39) { 
                if (active_input_field == 4) { strncpy(fdisp, find_text, 39); fdisp[39] = '\0'; }
                else { strcpy(fdisp, find_text + (strlen(find_text) - 39)); } 
            } else { strcpy(fdisp, find_text); while(strlen(fdisp) < 39) strcat(fdisp, " "); }
        }
        print_str(4, 12, fdisp, bg);

        /* FIX: Sliding Camera for Replace Box (80+ Column) */
        if (active_input_field == 5 && replace_cursor > 38) {
            strncpy(rdisp, replace_text + (replace_cursor - 38), 39); rdisp[39] = '\0';
        } else {
            if (strlen(replace_text) > 39) { 
                if (active_input_field == 5) { strncpy(rdisp, replace_text, 39); rdisp[39] = '\0'; }
                else { strcpy(rdisp, replace_text + (strlen(replace_text) - 39)); } 
            } else { strcpy(rdisp, replace_text); while(strlen(rdisp) < 39) strcat(rdisp, " "); }
        }
        print_str(7, 12, rdisp, bg);

        /* 16-BIT CURSOR RENDERING */
        if (active_input_field == 4 || active_input_field == 5) {
            int cx = 12 + (active_input_field == 4 ? find_cursor : replace_cursor);
            if (cx > 50) cx = 50; 
            int cy = active_input_field == 4 ? 4 : 7;
            unsigned char cur_col = get_cursor_color();
            
            char far* video = (char far*)MK_FP(video_segment, (cy * SCREEN_COLS + cx) * 2);
            char char_under = *video;
            set_char(cy, cx, char_under, cur_col);
        }
    }
}

void draw_editor() {
    int r, c, i, screen_cx, screen_cy, rlen;
    char under_cursor; 
    unsigned char ch_color, cur_color;
    
    unsigned char cur_cursor_color = get_cursor_color();
    unsigned char bg_color = get_bg_color(), hl_color = get_hl_color();
    unsigned char dim_color = get_dim_color();
    unsigned char base_ui_color = show_exit_dialog ? dim_color : bg_color;

    /* Core UI Anchors elevated to function scope */
    int right = SCREEN_COLS - 1;
    int bottom = SCREEN_ROWS - 1;
    int max_line_len = 0;
    char status_left[30] = "";
    char status_right[80] = "";
    
    if (!setting_disk_stream) {
        for (i = 0; i < total_lines; i++) {
            int l = (int)strlen(text_buffer[i]);
            if (l > max_line_len) max_line_len = l;
        }
    } else { max_line_len = 256; }
    
    // Dynamic Frame Math!
    int s_div = right - 2; 
    int x_div = right - 4; // <--- THE CRITICAL FIX for the 3-wide " X " box!
    
    int top_y = 3; /* Find Dialog Stripped! */
    int view_w = setting_v_scrollbar ? s_div - 1 : right - 1;
    int vis_h = SCREEN_ROWS - 3 - top_y;
    
    hide_mouse();
    if (show_settings) { draw_settings(); show_mouse(); return; }
    if (show_open_dialog || show_save_dialog) { draw_file_dialog(show_save_dialog); show_mouse(); return; }
    
    if (needs_recalc) { build_visual_map(); needs_recalc = 0; }
    long effective_vis_lines = vis_lines;
    update_vis_cursor();
    
    if (force_cursor_snap) {
        /* Line-by-line Y snapping */
        if (vis_cy < scroll_y) {
            scroll_y = vis_cy; 
        } else if (vis_cy >= scroll_y + vis_h) {
            scroll_y = vis_cy - vis_h + 1;
        }
        
        if (setting_word_wrap) { scroll_x = 0; } 
        else {
            if (cursor_x < scroll_x) scroll_x = cursor_x;
            if (cursor_x >= scroll_x + view_w) scroll_x = cursor_x - view_w + 1;
        }
        force_cursor_snap = 0;
    }
    
    if (setting_word_wrap) { scroll_x = 0; } 
    else {
        long m_scroll = (max_line_len >= view_w ? max_line_len - view_w + 1 : 0);
        if (scroll_x > m_scroll) scroll_x = m_scroll;
    }
    
    /* 2. Enforce Absolute Bounds using the 32-bit effective height! */
    long max_sc_y = effective_vis_lines - vis_h;
    if (max_sc_y < 0) max_sc_y = 0;
    if (scroll_y > max_sc_y) scroll_y = max_sc_y;
    if (scroll_y < 0) scroll_y = 0;
    
    if (needs_ui_redraw) {
        int r_idx, c_idx;

        // Clear screen
        for (r_idx = 0; r_idx < SCREEN_ROWS; r_idx++) {
            for (c_idx = 0; c_idx < SCREEN_COLS; c_idx++) {
                set_char(r_idx, c_idx, ' ', base_ui_color);
            }
        }

        // ROW 0: Top Border
        set_char(0, 0, decode_char('['), base_ui_color);
        for (c_idx = 1; c_idx < right; c_idx++) set_char(0, c_idx, decode_char('*'), base_ui_color);
        set_char(0, right, decode_char(']'), base_ui_color);
        set_char(0, 7, decode_char('^'), base_ui_color);
        set_char(0, 14, decode_char('^'), base_ui_color);
        set_char(0, 21, decode_char('^'), base_ui_color);
        set_char(0, x_div, decode_char('^'), base_ui_color);
        
        // ROW 1: Menu Bar
        set_char(1, 0, decode_char('|'), base_ui_color);
        set_char(1, 7, decode_char('|'), base_ui_color);
        set_char(1, 14, decode_char('|'), base_ui_color);
        set_char(1, 21, decode_char('|'), base_ui_color);
        set_char(1, x_div, decode_char('|'), base_ui_color);
        set_char(1, right, decode_char('|'), base_ui_color);

        // ROW 2: Header Separator
        set_char(2, 0, decode_char('@'), base_ui_color);
        for (c_idx = 1; c_idx < right; c_idx++) set_char(2, c_idx, decode_char('*'), base_ui_color);
        set_char(2, right, decode_char('$'), base_ui_color);
        
        // Menus Up-Junctions
        set_char(2, 7, decode_char('_'), base_ui_color);
        set_char(2, 14, decode_char('_'), base_ui_color);
        set_char(2, 21, decode_char('_'), base_ui_color);
        
        // X-Box Up-Junction
        set_char(2, x_div, decode_char('_'), base_ui_color); 
        
        if (setting_v_scrollbar) {
            // Scrollbar Down-Junction perfectly aligned to the right
            set_char(2, s_div, decode_char('^'), base_ui_color); 
        }

        // ROWS 3 to bottom-3: Main Text Area Borders
        for (r_idx = 3; r_idx <= bottom - 3; r_idx++) {
            set_char(r_idx, 0, decode_char('|'), base_ui_color);
            if (setting_v_scrollbar) set_char(r_idx, s_div, decode_char('|'), base_ui_color);
            set_char(r_idx, right, decode_char('|'), base_ui_color);
        }

        // ROW bottom-2: Footer Separator / H-Scroll
        set_char(bottom - 2, 0, decode_char('@'), base_ui_color);
        for (c_idx = 1; c_idx < right; c_idx++) set_char(bottom - 2, c_idx, decode_char('*'), base_ui_color);
        set_char(bottom - 2, right, decode_char('$'), base_ui_color);
        
        if (setting_v_scrollbar) set_char(bottom - 2, s_div, decode_char('+'), base_ui_color);
        if (setting_h_scrollbar) {
            /* FIX: Shifted T-junctions 1 unit right/left to correctly cap the horizontal scrollbar pipes */
            set_char(bottom - 2, 2, decode_char('^'), base_ui_color);
            set_char(bottom - 2, setting_v_scrollbar ? s_div - 2 : right - 2, decode_char('^'), base_ui_color);
        }

        // ROW bottom-1: Footer Text Area
        set_char(bottom - 1, 0, decode_char('|'), base_ui_color);
        if (setting_h_scrollbar) {
            set_char(bottom - 1, 2, decode_char('|'), base_ui_color);
            set_char(bottom - 1, setting_v_scrollbar ? s_div - 2 : right - 2, decode_char('|'), base_ui_color);
        }
        if (setting_v_scrollbar) set_char(bottom - 1, s_div, decode_char('|'), base_ui_color);
        set_char(bottom - 1, right, decode_char('|'), base_ui_color);

        // ROW bottom: Bottom Border
        set_char(bottom, 0, decode_char('{'), base_ui_color);
        for (c_idx = 1; c_idx < right; c_idx++) set_char(bottom, c_idx, decode_char('*'), base_ui_color);
        set_char(bottom, right, decode_char('}'), base_ui_color);
        
        if (setting_v_scrollbar) set_char(bottom, s_div, decode_char('_'), base_ui_color);
        if (setting_h_scrollbar) {
            set_char(bottom, 2, decode_char('_'), base_ui_color);
            set_char(bottom, setting_v_scrollbar ? s_div - 2 : right - 2, decode_char('_'), base_ui_color);
        }

        draw_main_static_ui(1); /* Force base draw */
        draw_main_element(1, 0); draw_main_element(2, 0); draw_main_element(3, 0);
        draw_main_element(4, 0); draw_main_element(60, 0);
        if (setting_v_scrollbar) { draw_main_element(61, 0); draw_main_element(62, 0); }
        if (setting_h_scrollbar) { draw_main_element(63, 0); draw_main_element(64, 0); }
        
        /* DO NOT clear needs_ui_redraw here, bottom UI elements still need to know! */
    } else {
        draw_main_static_ui(0); /* Light check for dirty status asterisk */
    }
    
    // RENDER TEXT BUFFER
    for (r = 0; r < vis_h; r++) {
        long line_idx = (long)scroll_y + r;
        if (line_idx < effective_vis_lines) {
            int log_y, v_len;
            long start_x;
            char* line_text = NULL;

            log_y = vis_map[line_idx].log_y; 
            start_x = vis_map[line_idx].log_x; 
            v_len = vis_map[line_idx].len;
            
            if (setting_disk_stream) {
                line_text = get_streamed_segment(log_y, start_x + scroll_x, setting_word_wrap ? v_len : view_w);
            } else {
                line_text = text_buffer[log_y];
            }

            for (c = 0; c < view_w; c++) {
                long char_idx = start_x + scroll_x + c;
                int is_valid_char = 0;
                char char_to_draw = ' ';
                
                if (setting_disk_stream) {
                    if (c < (line_text ? (int)strlen(line_text) : 0)) { is_valid_char = 1; char_to_draw = line_text[c]; }
                } else {
                    if (scroll_x + c < v_len && line_text != NULL) { is_valid_char = 1; char_to_draw = line_text[char_idx]; }
                }
                
                int in_sel = is_in_selection(log_y, char_idx);
                /* FIX: Extend selection background if the selection covers the END of this visual line! */
                if (!is_valid_char && has_selection && !in_sel) {
                    long sy = sel_start_y, ey = cursor_y, ex = cursor_x;
                    if (sy > ey || (sy == ey && sel_start_x > cursor_x)) { ey = sel_start_y; ex = sel_start_x; sy = cursor_y; }
                    if (log_y == ey && log_y > sy) {
                        long m_len = setting_disk_stream ? get_stream_logical_len(log_y) : (long)strlen(text_buffer[log_y]);
                        if (ex >= m_len) in_sel = 1;
                    }
                }
                
                ch_color = in_sel ? hl_color : bg_color;
                if (show_exit_dialog) ch_color = dim_color;
                
                if (is_valid_char) set_char(r + top_y, c + 1, char_to_draw, ch_color);
                else set_char(r + top_y, c + 1, ' ', ch_color);
            }
            if (setting_disk_stream && line_text) free(line_text);
        } else { 
            for (c = 0; c < view_w; c++) set_char(r + top_y, c + 1, ' ', base_ui_color); 
        }
    }

    /* Restore proper max_sc_y based on effective vis_lines! */
    max_sc_y = effective_vis_lines - vis_h;
    if (max_sc_y < 0) max_sc_y = 0;

    if (setting_v_scrollbar) {
        int track_h = bottom - 9; /* FIX: Reduced track height by 2 chunks to fit top/bottom borders! */
        static int last_v_thumb = -1, last_v_size = -1;
        long l_req_vp; int tsize = 0, thumb_pos = 0; 
        
        if (setting_disk_stream && total_stream_lines > CHUNK_LOG_SIZE) {
            l_req_vp = (total_stream_lines + vis_h - 1) / vis_h;
            if (l_req_vp < 1) l_req_vp = 1;
            tsize = (l_req_vp > 1) ? (int)((track_h * 1L + l_req_vp / 2) / l_req_vp) : track_h; 
            if (tsize < 1) tsize = 1; 
            if (tsize > track_h) tsize = track_h;
            thumb_pos = (total_stream_lines > 1) ? (int)(((long)cursor_y * (track_h - tsize) + ((total_stream_lines - 1) / 2)) / (total_stream_lines - 1)) : 0;
        } else {
            l_req_vp = (effective_vis_lines + vis_h - 1) / vis_h;
            if (l_req_vp < 1) l_req_vp = 1;
            tsize = (l_req_vp > 1) ? (int)((track_h * 1L + l_req_vp / 2) / l_req_vp) : track_h; 
            if (tsize < 1) tsize = 1; 
            if (tsize > track_h) tsize = track_h;
            thumb_pos = (max_sc_y > 0) ? (int)(((long)scroll_y * (track_h - tsize) + (max_sc_y / 2)) / max_sc_y) : 0;
        }
        
        if (needs_ui_redraw || thumb_pos != last_v_thumb || tsize != last_v_size) {
            for (i = 0; i < track_h; i++) {
                if (i >= thumb_pos && i < thumb_pos + tsize) {
                    set_char(5 + i, s_div + 1, ' ', cur_cursor_color); /* FIX: Track starts 1 chunk lower */
                } else {
                    set_char(5 + i, s_div + 1, ' ', bg_color); /* FIX: Track starts 1 chunk lower */
                }
            }
            last_v_thumb = thumb_pos; last_v_size = tsize;
        }
    }
    
    int is_hovering_hbar = (!show_exit_dialog && setting_h_scrollbar && mouse_y == bottom - 1 && mouse_x >= 1 && mouse_x <= (setting_v_scrollbar ? s_div - 1 : right - 1));
    if (setting_h_scrollbar && is_hovering_hbar) {
        int hw = (setting_v_scrollbar ? s_div - 1 : right - 1) - 4;
        int h_tsize, h_thumb_pos, max_scroll_x;
        int view_w = setting_v_scrollbar ? s_div - 2 : right - 1; /* Restore view_w! */
        max_scroll_x = (max_line_len >= view_w ? max_line_len - view_w + 1 : 0);
        h_tsize = (max_line_len > view_w) ? (int)((hw * view_w) / max_line_len) : hw;
        if (h_tsize < 3) h_tsize = 3; 
        if (h_tsize > hw) h_tsize = hw;
        h_thumb_pos = (max_scroll_x > 0) ? (int)(((long)scroll_x * (hw - h_tsize) + (max_scroll_x - 1) / 2) / max_scroll_x) : 0;
        
        static int last_h_thumb = -1, last_h_size = -1;
        if (needs_ui_redraw || h_thumb_pos != last_h_thumb || h_tsize != last_h_size) {
            for (i = 0; i < hw; i++) {
                if (i >= h_thumb_pos && i < h_thumb_pos + h_tsize) {
                    set_char(bottom - 1, 3 + i, ' ', cur_cursor_color);
                } else {
                    set_char(bottom - 1, 3 + i, ' ', bg_color);
                }
            }
            last_h_thumb = h_thumb_pos; last_h_size = h_tsize;
        }
    } else {
        long chars_count = 0; /* Restore missing variable! */
        long full_file_chars = 0; /* Restore missing variable! */
        
        if (setting_disk_stream) {
            for (i = 0; i < num_pieces; i++) chars_count += piece_table[i].length;
            full_file_chars = chars_count; 
        } else {
            for (i = 0; i < total_lines; i++) { chars_count += (long)strlen(text_buffer[i]); }
        }
        
        int scroll_percent = 100;
        if (setting_disk_stream && total_stream_lines > CHUNK_LOG_SIZE) {
            if (total_stream_lines > 0) scroll_percent = (int)((cursor_y * 100) / total_stream_lines);
        } else {
            if (max_sc_y > 0) scroll_percent = (int)((scroll_y * 100) / max_sc_y);
        }
        
        if (SCREEN_COLS < 80) {
            /* FIX: Ultra-compact status bar for 40-column mode to prevent overlap! */
            sprintf(status_left, "%d%%", scroll_percent);
            
            if (is_file_truncated && !setting_disk_stream) { 
                sprintf(status_right, "Trunc, Ln %ld, Col %ld", cursor_y + 1, cursor_x + 1); 
            } else { 
                sprintf(status_right, "Ln %ld, Col %ld", cursor_y + 1, cursor_x + 1); 
            }
        } else {
            /* Standard 80/132-column status bar with full information */
            if (setting_disk_stream) { sprintf(status_left, "%d%% [Str]", scroll_percent); } 
            else { sprintf(status_left, "%d%%", scroll_percent); }
            
            if (is_file_truncated && !setting_disk_stream) { 
                sprintf(status_right, "Chars: %ld / %ld, Ln %ld, Col %ld", chars_count, full_file_chars, cursor_y + 1, cursor_x + 1); 
            } else { 
                sprintf(status_right, "Chars: %ld, Ln %ld, Col %ld", chars_count, cursor_y + 1, cursor_x + 1); 
            }
        }
        
        rlen = (int)strlen(status_right); /* The 'int' keyword is removed so it doesn't redeclare! */
        int right_anchor = setting_v_scrollbar ? s_div - 2 : right - 2;
        
        static char last_stat_l[30] = "", last_stat_r[80] = "";
        if (needs_ui_redraw || strcmp(status_left, last_stat_l) != 0 || strcmp(status_right, last_stat_r) != 0) {
            int clear_start = setting_h_scrollbar ? 3 : 1;
            int clear_end = setting_h_scrollbar ? (setting_v_scrollbar ? s_div - 3 : right - 3) : (setting_v_scrollbar ? s_div - 1 : right - 1);
            for (i = clear_start; i <= clear_end; i++) set_char(bottom - 1, i, ' ', base_ui_color);
            print_str(bottom - 1, 3, status_left, base_ui_color);
            print_str(bottom - 1, right_anchor - rlen, status_right, base_ui_color);
            strcpy(last_stat_l, status_left); strcpy(last_stat_r, status_right);
        }
    }
    
    screen_cx = (vis_cx - scroll_x) + 1; 
    screen_cy = (vis_cy - scroll_y) + top_y;
    
    if (screen_cy >= top_y && screen_cy <= bottom - 3 && screen_cx >= 1 && screen_cx <= view_w && !active_menu && !show_exit_dialog && !show_context_menu) {
        under_cursor = ' '; 
        if (setting_disk_stream) {
            char* t_line = get_streamed_segment(cursor_y, cursor_x, 1);
            if (t_line) {
                if (strlen(t_line) > 0) under_cursor = t_line[0];
                free(t_line);
            }
        } else {
            if (cursor_x < (int)strlen(text_buffer[cursor_y])) under_cursor = text_buffer[cursor_y][cursor_x];
        }
        
        cur_color = cur_cursor_color; 
        if (show_exit_dialog) cur_color = dim_color;
        set_char(screen_cy, screen_cx, under_cursor, cur_color);
    }
    
    if (show_context_menu) draw_context_menu();
    else if (show_exit_dialog) draw_exit_dialog(); 
    else if (active_menu > 0) draw_menu();
    
    needs_ui_redraw = 0; /* Safely cleared at the very end of the rendering cycle! */
    show_mouse();
}

void execute_find_replace(int action) {
    int i;
    int f_len = (int)strlen(find_text);
    int r_len = (int)strlen(replace_text);
    int m_lines = setting_disk_stream ? total_stream_lines : total_lines;
    
    if (f_len == 0) return;

    if (action == 1) { /* Find / Next */
        int sy = cursor_y;
        int sx = cursor_x;
        
        if (has_selection && sel_start_y == cursor_y) {
            int min_x = sel_start_x < cursor_x ? sel_start_x : cursor_x;
            char* cur_line = setting_disk_stream ? get_streamed_segment(cursor_y, 0, get_stream_logical_len(cursor_y)) : text_buffer[cursor_y];
            if (cur_line && min_x < (int)strlen(cur_line) && strncmp(&cur_line[min_x], find_text, f_len) == 0) {
                sx = min_x + 1; 
            }
            if (setting_disk_stream && cur_line) free(cur_line);
        }

        /* 1. Search from current cursor to end of document */
        for (i = sy; i < m_lines; i++) {
            char* line = setting_disk_stream ? get_streamed_segment(i, 0, get_stream_logical_len(i)) : text_buffer[i];
            if (!line) continue;
            char* match = NULL;
            
            if (i == sy && sx < (int)strlen(line)) {
                match = strstr(line + sx, find_text);
            } else if (i > sy) {
                match = strstr(line, find_text);
            }

            if (match != NULL) {
                int pos = (int)(match - line);
                cursor_y = i; cursor_x = pos + f_len;
                sel_start_y = i; sel_start_x = pos;
                has_selection = 1; force_cursor_snap = 1; needs_ui_redraw = 1;
                if (setting_disk_stream) free(line);
                return;
            }
            if (setting_disk_stream) free(line);
        }
        
        /* 2. Wrap around and search from top to cursor */
        for (i = 0; i <= sy && i < m_lines; i++) {
            char* line = setting_disk_stream ? get_streamed_segment(i, 0, get_stream_logical_len(i)) : text_buffer[i];
            if (!line) continue;
            char* match = strstr(line, find_text);
            
            if (match != NULL && (i < sy || (i == sy && (int)(match - line) < sx))) {
                int pos = (int)(match - line);
                cursor_y = i; cursor_x = pos + f_len;
                sel_start_y = i; sel_start_x = pos;
                has_selection = 1; force_cursor_snap = 1; needs_ui_redraw = 1;
                if (setting_disk_stream) free(line);
                return;
            }
            if (setting_disk_stream) free(line);
        }
        
        has_selection = 0; needs_ui_redraw = 1;
    }
    else if (action == 4) { /* Prev (Search Backwards) */
        int sy = cursor_y;
        int sx = cursor_x;

        if (has_selection && sel_start_y == cursor_y) {
            int min_x = sel_start_x < cursor_x ? sel_start_x : cursor_x;
            char* cur_line = setting_disk_stream ? get_streamed_segment(cursor_y, 0, get_stream_logical_len(cursor_y)) : text_buffer[cursor_y];
            if (cur_line && min_x < (int)strlen(cur_line) && strncmp(&cur_line[min_x], find_text, f_len) == 0) {
                sx = min_x - 1; 
            } else {
                sx--;
            }
            if (setting_disk_stream && cur_line) free(cur_line);
        } else {
            sx--; 
        }

        /* 1. Search backwards from current cursor to top of document */
        for (i = sy; i >= 0; i--) {
            char* line = setting_disk_stream ? get_streamed_segment(i, 0, get_stream_logical_len(i)) : text_buffer[i];
            if (!line) continue;
            char* last_match = NULL;
            char* current_match = line;
            
            while ((current_match = strstr(current_match, find_text)) != NULL) {
                int pos = (int)(current_match - line);
                if (i < sy || (i == sy && pos <= sx)) {
                    last_match = current_match;
                }
                current_match++; 
            }

            if (last_match != NULL) {
                int pos = (int)(last_match - line);
                cursor_y = i; cursor_x = pos + f_len;
                sel_start_y = i; sel_start_x = pos;
                has_selection = 1; force_cursor_snap = 1; needs_ui_redraw = 1;
                if (setting_disk_stream) free(line);
                return;
            }
            if (i > 0) sx = setting_disk_stream ? (int)get_stream_logical_len(i-1) : (int)strlen(text_buffer[i-1]); 
            if (setting_disk_stream) free(line);
        }
        
        /* 2. Wrap around and search backwards from bottom to cursor */
        for (i = m_lines - 1; i >= sy; i--) {
            char* line = setting_disk_stream ? get_streamed_segment(i, 0, get_stream_logical_len(i)) : text_buffer[i];
            if (!line) continue;
            char* last_match = NULL;
            char* current_match = line;
            
            while ((current_match = strstr(current_match, find_text)) != NULL) {
                int pos = (int)(current_match - line);
                if (i > sy || (i == sy && pos >= cursor_x)) {
                    last_match = current_match;
                }
                current_match++;
            }

            if (last_match != NULL) {
                int pos = (int)(last_match - line);
                cursor_y = i; cursor_x = pos + f_len;
                sel_start_y = i; sel_start_x = pos;
                has_selection = 1; force_cursor_snap = 1; needs_ui_redraw = 1;
                if (setting_disk_stream) free(line);
                return;
            }
            if (setting_disk_stream) free(line);
        }
        
        has_selection = 0; needs_ui_redraw = 1;
    }
    else if (action == 2) { /* Replace */
        if (setting_read_only) return; /* GUARD: Read-Only */
        if (has_selection && sel_start_y == cursor_y) {
            int min_x = sel_start_x < cursor_x ? sel_start_x : cursor_x;
            char* cur_line = setting_disk_stream ? get_streamed_segment(cursor_y, 0, get_stream_logical_len(cursor_y)) : text_buffer[cursor_y];
            if (cur_line && min_x < (int)strlen(cur_line) && strncmp(&cur_line[min_x], find_text, f_len) == 0) {
                if (setting_disk_stream) free(cur_line);
                save_state(); last_was_typing = 0;
                delete_selection();
                for (i = 0; i < r_len; i++) insert_char(replace_text[i]);
                execute_find_replace(1); 
                return;
            }
            if (setting_disk_stream && cur_line) free(cur_line);
        }
        execute_find_replace(1); 
    }
    else if (action == 3) { /* Replace All */
        int count = 0;
        if (setting_read_only || setting_disk_stream) return; /* GUARD: Disabled in Stream Mode to protect RAM bounds! */
        save_state(); last_was_typing = 0;
        for (i = 0; i < total_lines; i++) {
            char* search_start = text_buffer[i]; 
            char* match;
            while ((match = strstr(search_start, find_text)) != NULL) {
                int pos = (int)(match - text_buffer[i]);
                int line_len = (int)strlen(text_buffer[i]);
                
                if (r_len > f_len) {
                    text_buffer[i] = (char*)realloc(text_buffer[i], line_len + (r_len - f_len) + 1);
                    match = text_buffer[i] + pos;
                }
                
                memmove(match + r_len, match + f_len, line_len - pos - f_len + 1);
                memcpy(match, replace_text, r_len);
                
                search_start = match + r_len; 
                count++;
            }
        }
        has_selection = 0;
        if (count > 0) {
            is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1; needs_ui_redraw = 1;
        }
    }
}

int save_document(const char* filepath) {
    if (!setting_disk_stream) {
        /* FIX: Atomic Save Protocol for Fast RAM Buffer! Protects against power loss/crashes during save. */
        char temp_path[256];
        int len, i;
        FILE* f;
        
        strcpy(temp_path, filepath);
        len = (int)strlen(temp_path);
        while (len > 0 && temp_path[len-1] != '\\' && temp_path[len-1] != '/') len--;
        temp_path[len] = '\0';
        strcat(temp_path, "~NPTMP.TMP");
        
        f = fopen(temp_path, "w");
        if (!f) return 0;
        
        for (i = 0; i < total_lines; i++) {
            fputs(text_buffer[i], f);
            fputs("\n", f);
        }
        fclose(f);
        
        /* The Hot-Swap */
        remove(filepath);
        rename(temp_path, filepath);
        
        is_document_dirty = 0;
        return 1;
    } else {
        /* Safe Disk Stream Save Pipeline */
        char temp_path[256];
        int p_idx, len;
        FILE* temp_f;
        
        /* Ensure the TMP file is created in the exact same directory to prevent cross-drive rename errors */
        strcpy(temp_path, filepath);
        len = strlen(temp_path);
        while (len > 0 && temp_path[len-1] != '\\' && temp_path[len-1] != '/') len--;
        temp_path[len] = '\0';
        strcat(temp_path, "~NPTMP.TMP");
        
        temp_f = fopen(temp_path, "wb");
        if (!temp_f) return 0;
        
        for (p_idx = 0; p_idx < num_pieces; p_idx++) {
            Piece* p = &piece_table[p_idx];
            if (p->source == SOURCE_DISK && stream_file_handle) {
                fseek(stream_file_handle, p->offset, SEEK_SET);
                long bytes = p->length;
                while (bytes > 0) {
                    int ch = fgetc(stream_file_handle);
                    if (ch == EOF) break;
                    fputc(ch, temp_f);
                    bytes--;
                }
            } else if (p->source == SOURCE_RAM) {
                long r_idx = p->offset;
                long bytes = p->length;
                while (bytes > 0 && r_idx < add_buffer_size) {
                    char ch = add_buffer[r_idx++];
                    if (ch == '\n') fputc('\r', temp_f); /* Safely inject DOS carriage returns for freshly typed lines! */
                    fputc(ch, temp_f);
                    bytes--;
                }
            }
        }
        fclose(temp_f);
        
        /* The Hot-Swap */
        if (stream_file_handle) { fclose(stream_file_handle); stream_file_handle = NULL; }
        remove(filepath);
        rename(temp_path, filepath);
        
        /* Flatten the Piece Table onto the new file so the user can seamlessly keep typing */
        stream_file_handle = fopen(filepath, "rb");
        if (stream_file_handle) {
            num_pieces = 1;
            piece_table[0].source = SOURCE_DISK;
            piece_table[0].offset = 0;
            fseek(stream_file_handle, 0, SEEK_END);
            piece_table[0].length = ftell(stream_file_handle);
            rewind(stream_file_handle);
            add_buffer_size = 0;
        }
        
        clear_entire_history(); /* Snapshots point to old disk byte-offsets, so they must be wiped on save */
        is_document_dirty = 0;
        return 1;
    }
}

void clean_exit() {
    int r, c;
    
    /* FIX: Final Exit Cleanup! Ensure no ghost .TMP files are left behind when the app closes! */
    if (strcmp(current_filename, "Untitled.txt") != 0) {
        char temp_path[256];
        strcpy(temp_path, current_browser_path);
        strcat(temp_path, "~NPTMP.TMP");
        remove(temp_path);
    }
    hide_mouse();
    show_hardware_cursor();
    /* Dynamically wipe the screen based on the current video mode */
    for (r = 0; r < SCREEN_ROWS; r++) { 
        for (c = 0; c < SCREEN_COLS; c++) {
            set_char(r, c, ' ', make_color(0, 7)); 
        }
    } 
    exit(0);
}

void execute_menu_action(int menu_id, int action_y) {
    if (menu_id == 1) {
        if (action_y == 3) { 
            if (setting_disk_stream) {
                init_empty_piece_table();
            } else {
                total_lines = 1; text_buffer[0][0] = '\0'; 
            }
            setting_read_only = 0; /* New files are always editable */
            cursor_x = 0; cursor_y = 0; is_document_dirty = 0; 
            scroll_y = 0; scroll_x = 0; needs_recalc = 1; 
            clear_entire_history();
            strcpy(current_filename, "Untitled.txt");
            is_file_truncated = 0;
            needs_ui_redraw = 1; force_cursor_snap = 1; 
        }
        else if (action_y == 4) { 
            show_open_dialog = 1; active_tab_element = 1; active_input_field = 1;
            strcpy(dialog_status_msg, "Ready to select file..."); 
            load_directory(current_browser_path, 1); 
        }
        else if (action_y == 5) { 
            if (strcmp(current_filename, "Untitled.txt") == 0) { 
                show_save_dialog = 1; active_tab_element = 1; active_input_field = 1;
                strcpy(dialog_status_msg, "Ready to save file..."); 
                load_directory(current_browser_path, 1); 
            } else { 
                char filepath[256]; 
                strcpy(filepath, current_browser_path); 
                strcat(filepath, current_filename); 
                save_document(filepath); 
            } 
        }
        else if (action_y == 6) { 
            show_save_dialog = 1; active_tab_element = 1; active_input_field = 1;
            strcpy(dialog_input_text, current_filename); 
            dialog_input_cursor = (int)strlen(dialog_input_text); 
            strcpy(dialog_status_msg, "Ready to save file..."); 
            load_directory(current_browser_path, 1); 
        }
        else if (action_y == 8) {
            if (setting_read_only) return;
            if (setting_disk_stream) {
                if (!has_selection) {
                    init_empty_piece_table();
                    cursor_x = 0; cursor_y = 0; is_document_dirty = 1;
                    scroll_y = 0; scroll_x = 0; needs_recalc = 1; 
                    needs_ui_redraw = 1; force_cursor_snap = 1;
                }
            } else {
                if (has_selection) { save_state(); last_was_typing = 0; delete_selection(); }
                else {
                    save_state(); last_was_typing = 0;
                    total_lines = 1; text_buffer[0][0] = '\0'; 
                    cursor_x = 0; cursor_y = 0; is_document_dirty = 1; 
                    scroll_y = 0; scroll_x = 0; needs_recalc = 1; 
                    needs_ui_redraw = 1; force_cursor_snap = 1;
                }
            }
        }
        else if (action_y == 10) { 
            show_settings = 1;
            needs_ui_redraw = 1;
        }
        else if (action_y == 12) { 
            if (is_document_dirty) { 
                show_exit_dialog = 1; active_tab_element = 1; 
            } 
            else clean_exit(); 
        }
    } else if (menu_id == 2) {
        if (action_y == 3) perform_undo();
        else if (action_y == 4) perform_redo();
        else if (action_y == 6) { if(has_selection){ save_state(); last_was_typing=0; copy_selection(); delete_selection(); } } 
        else if (action_y == 7) copy_selection(); 
        else if (action_y == 8) paste_clipboard(); 
        else if (action_y == 9) { if(has_selection){ save_state(); last_was_typing=0; delete_selection(); } }
        else if (action_y == 11) { 
            sel_start_x = 0; sel_start_y = 0; 
            if (setting_disk_stream) { cursor_y = total_stream_lines - 1; cursor_x = get_stream_logical_len(cursor_y); }
            else { cursor_y = total_lines - 1; cursor_x = (int)strlen(text_buffer[cursor_y]); }
            has_selection = 1; force_cursor_snap = 1; 
        }
        else if (action_y == 12) { 
            setting_read_only = !setting_read_only;
            needs_ui_redraw = 1;
        }
    } else if (menu_id == 3) {
        if (action_y == 3 || action_y == 4) { 
            setting_h_scrollbar = !setting_h_scrollbar; 
            setting_word_wrap = !setting_h_scrollbar; 
        }
        else if (action_y == 6 || action_y == 7) { 
            setting_v_scrollbar = !setting_v_scrollbar; 
        }
        else if (action_y == 9) { 
            setting_word_wrap = !setting_word_wrap; 
            setting_h_scrollbar = !setting_word_wrap; 
        } 
        else if (action_y == 11) { 
            setting_respect_words = !setting_respect_words; 
        }
        needs_recalc = 1; force_cursor_snap = 1;
    }
    active_menu = 0; active_menu_hover = -1; needs_ui_redraw = 1;
}

void handle_file_ok_click() {
    char filepath[256]; 
    FILE* f; 
    strcpy(filepath, current_browser_path); 
    strcat(filepath, dialog_input_text);
    
    if (show_save_dialog) {
        if (save_document(filepath)) { 
            strcpy(current_filename, dialog_input_text); 
            show_open_dialog = 0; 
            show_save_dialog = 0; 
            active_tab_element = 0;
            needs_ui_redraw = 1; 
            draw_editor(); 
        } else { 
            strcpy(dialog_status_msg, "Error: Could not save file!"); 
            draw_editor(); 
        }
    } else if (show_open_dialog) {
        if (setting_disk_stream) {
            /* NEW ENGINE: Route straight to the Piece Table! */
            init_piece_table(filepath);
            if (stream_file_handle) {
                strcpy(current_filename, dialog_input_text); 
                is_document_dirty = 0; 
                show_open_dialog = 0; show_save_dialog = 0; 
                active_tab_element = 0;
                setting_read_only = (total_stream_lines > 6643) ? 1 : 0;
                clear_entire_history();
                needs_ui_redraw = 1; needs_recalc = 1; force_cursor_snap = 1; 
                draw_editor();
            } else {
                strcpy(dialog_status_msg, "Error: Stream Failed!"); 
                draw_editor();
            }
        } else {
            /* OLD ENGINE: Fast RAM Buffer */
            f = fopen(filepath, "rb");
            if (f) {
                /* Instantly grab the true file size off the hard drive! */
                fseek(f, 0, SEEK_END);
                full_file_chars = ftell(f);
                rewind(f);
                
                char line_chunk[512]; 
                int i; 
                for(i = 0; i < total_lines; i++) free(text_buffer[i]);
                total_lines = 0;
                
                char* current_line = (char*)malloc(1); 
                current_line[0] = '\0'; 
                int cur_len = 0;
                long loaded_chars = 0;
                is_file_truncated = 0;
                
                while (fgets(line_chunk, sizeof(line_chunk), f)) {
                    int chunk_len = (int)strlen(line_chunk); 
                    int has_newline = 0;
                    
                    if (chunk_len > 0 && line_chunk[chunk_len - 1] == '\n') has_newline = 1;
                    
                    /* EXACT LIMIT ENFORCEMENT */
                    if (loaded_chars + chunk_len > 6643) {
                        chunk_len = 6643 - loaded_chars;
                        line_chunk[chunk_len] = '\0';
                        is_file_truncated = 1;
                        has_newline = 0; 
                    }
                    int raw_chunk = chunk_len;
                    
                    while (chunk_len > 0 && (line_chunk[chunk_len-1] == '\n' || line_chunk[chunk_len-1] == '\r')) { 
                        line_chunk[chunk_len-1] = '\0'; chunk_len--; 
                    }
                    
                    if (cur_len + chunk_len > 32000) has_newline = 1;
                    
                    char* temp_line = (char*)realloc(current_line, (size_t)(cur_len + chunk_len + 1)); 
                    if (!temp_line) { strcpy(dialog_status_msg, "Out Of Memory! File truncated."); break; }
                    current_line = temp_line;
                    
                    strcpy(current_line + cur_len, line_chunk); 
                    cur_len += chunk_len;
                    loaded_chars += raw_chunk;
                    
                    if (has_newline) { 
                        if (total_lines >= buffer_capacity) { 
                            long new_cap = buffer_capacity * 2;
                            if (new_cap * sizeof(char*) > 64000L) { strcpy(dialog_status_msg, "64KB Limit! File truncated."); break; }
                            char** temp_buf = (char**)realloc(text_buffer, (size_t)(new_cap * sizeof(char*))); 
                            if (!temp_buf) { strcpy(dialog_status_msg, "Out Of Memory!"); break; }
                            buffer_capacity = (int)new_cap; text_buffer = temp_buf;
                        } 
                        text_buffer[total_lines++] = current_line; 
                        current_line = (char*)malloc(1); 
                        if (!current_line) { strcpy(dialog_status_msg, "Out Of Memory!"); break; }
                        current_line[0] = '\0'; cur_len = 0; 
                    }
                    if (is_file_truncated) break;
                }
                /* Allocate space for both the last line AND the warning message */
                if (total_lines >= buffer_capacity) { 
                    char** temp_buf = (char**)realloc(text_buffer, (total_lines + 2) * sizeof(char*)); 
                    if (temp_buf) text_buffer = temp_buf;
                } 
                if (current_line) text_buffer[total_lines++] = current_line;
                
                /* Inject the warning directly into the canvas! */
                if (is_file_truncated) {
                    char* warning_msg = (char*)malloc(100);
                    strcpy(warning_msg, "[file greater than 6643 characters use disk streaming mode to load all of it]");
                    text_buffer[total_lines++] = warning_msg;
                }
                
                fclose(f); 
                strcpy(current_filename, dialog_input_text); 
                is_document_dirty = 0; cursor_x = 0; cursor_y = 0; scroll_y = 0; scroll_x = 0; 
                
                /* Always close the dialog cleanly */
                show_open_dialog = 0; show_save_dialog = 0; active_tab_element = 0;
                
                setting_read_only = 0; /* RAM mode always starts fully editable */
                clear_entire_history();
                needs_ui_redraw = 1; needs_recalc = 1; force_cursor_snap = 1; 
                draw_editor();
            } else { 
                strcpy(dialog_status_msg, "Error: File not found!"); 
                draw_editor(); 
            }
        }
    }
}

void detect_screen() {
    /* FIX: Direct BIOS Data Area peeks! Bypasses Turbo C++ parser panics! */
    unsigned char far* p_mode = (unsigned char far*)MK_FP(0x0040, 0x0049);
    unsigned int far* p_cols  = (unsigned int far*)MK_FP(0x0040, 0x004A);
    unsigned char far* p_rows = (unsigned char far*)MK_FP(0x0040, 0x0084);
    unsigned int far* p_font  = (unsigned int far*)MK_FP(0x0040, 0x0085);
    
    video_mode = *p_mode;
    SCREEN_COLS = *p_cols;
    SCREEN_ROWS = (*p_rows) + 1;
    font_height = *p_font;
    
    /* Hardware MDA Memory Segment Check! */
    if (video_mode == 7) video_segment = 0xB000; 
    else video_segment = 0xB800; 
    
    if (SCREEN_ROWS < 10) SCREEN_ROWS = 25;
    if (font_height == 0) font_height = 16;
}

void init_piece_table(const char* filepath) {
    long current_byte = 0;
    int ch;
    
    if (stream_file_handle) { fclose(stream_file_handle); stream_file_handle = NULL; }
    
    /* 1. Setup the Disk Stream */
    stream_file_handle = fopen(filepath, "rb");
    if (!stream_file_handle) return;
    
    /* 2. Setup the Add Buffer (Start with 4KB of typing space) */
    if (add_buffer) free(add_buffer);
    add_buffer_capacity = 4096;
    add_buffer_size = 0;
    add_buffer = (char*)malloc(add_buffer_capacity);
    
    /* 3. Setup the First Piece (The whole file) */
    if (piece_table) free(piece_table);
    piece_capacity = 100;
    num_pieces = 1;
    piece_table = (Piece*)malloc(piece_capacity * sizeof(Piece));
    
    piece_table[0].source = SOURCE_DISK;
    piece_table[0].offset = 0;
    fseek(stream_file_handle, 0, SEEK_END);
    piece_table[0].length = ftell(stream_file_handle);
    rewind(stream_file_handle);
    
    /* 4. Build the Line Index for Instant Scrolling */
    if (stream_lines) free(stream_lines);
    stream_line_capacity = 1000;
    total_stream_lines = 0;
    stream_lines = (LineIndex*)malloc(stream_line_capacity * sizeof(LineIndex));
    
    /* Line 0 always starts at byte 0 */
    stream_lines[total_stream_lines++].global_offset = 0;
    
    long line_length = 0;
    while ((ch = fgetc(stream_file_handle)) != EOF) {
        current_byte++;
        line_length++;
        /* Artificial Chunking: Break exactly at 4096 chars OR real newline */
        if (ch == '\n' || line_length >= 4096) {
            if (total_stream_lines >= stream_line_capacity) {
                long new_cap = stream_line_capacity * 2;
                if (new_cap * sizeof(LineIndex) <= 64000L) { /* 64KB Guard */
                    LineIndex* temp = (LineIndex*)realloc(stream_lines, (size_t)(new_cap * sizeof(LineIndex)));
                    if (temp) { stream_line_capacity = (int)new_cap; stream_lines = temp; }
                    else { is_file_truncated = 1; break; } /* FIX: Stop if out of heap! */
                } else {
                    is_file_truncated = 1; break; /* FIX: Stop if hitting 64KB segment limit! */
                }
            }
            stream_lines[total_stream_lines++].global_offset = current_byte;
            line_length = 0;
        }
    }
    
    /* Setup Editor State */
    cursor_x = 0; cursor_y = 0; scroll_x = 0; scroll_y = 0;
    is_document_dirty = 0; needs_recalc = 1; force_cursor_snap = 1;
    is_file_truncated = 0;
    current_chunk_start_log = 0;
}

char get_stream_char(long global_offset) {
    int p_idx = 0;
    long piece_start = 0;
    while (p_idx < num_pieces) {
        if (global_offset >= piece_start && global_offset < piece_start + piece_table[p_idx].length) {
            long local_offset = global_offset - piece_start;
            if (piece_table[p_idx].source == SOURCE_DISK) {
                if (stream_file_handle) {
                    fseek(stream_file_handle, piece_table[p_idx].offset + local_offset, SEEK_SET);
                    return (char)fgetc(stream_file_handle);
                }
            } else if (piece_table[p_idx].source == SOURCE_RAM) {
                return add_buffer[piece_table[p_idx].offset + local_offset];
            }
        }
        piece_start += piece_table[p_idx].length;
        p_idx++;
    }
    return '\0';
}

void delete_from_stream(long target_global) {
    char deleted_char;
    int p_idx = 0, i;
    long piece_start = 0;
    long local_offset;
    Piece* p;
    
    if (target_global < 0) return;
    
    deleted_char = get_stream_char(target_global);
    if (deleted_char == '\0') return;
    
    while (p_idx < num_pieces) {
        if (target_global >= piece_start && target_global < piece_start + piece_table[p_idx].length) break;
        piece_start += piece_table[p_idx].length;
        p_idx++;
    }
    if (p_idx >= num_pieces) return;
    
    local_offset = target_global - piece_start;
    p = &piece_table[p_idx];
    
    /* 1. Shrink or Split the Piece! */
    if (local_offset == 0) {
        p->offset++; p->length--;
    } else if (local_offset == p->length - 1) {
        p->length--;
    } else {
        /* Middle-Split */
        if (num_pieces + 1 >= piece_capacity) {
            long new_cap = piece_capacity * 2;
            Piece* temp = (Piece*)realloc(piece_table, (size_t)(new_cap * sizeof(Piece)));
            if (!temp) return;
            piece_capacity = (int)new_cap;
            piece_table = temp;
            p = &piece_table[p_idx];
        }
        memmove(&piece_table[p_idx + 2], &piece_table[p_idx + 1], (size_t)((num_pieces - p_idx - 1) * sizeof(Piece)));
        piece_table[p_idx + 1].source = p->source;
        piece_table[p_idx + 1].offset = p->offset + local_offset + 1;
        piece_table[p_idx + 1].length = p->length - local_offset - 1;
        p->length = local_offset;
        num_pieces++;
    }
    
    /* 2. Garbage Collect 0-length pieces */
    for (i = 0; i < num_pieces; i++) {
        if (piece_table[i].length <= 0) {
            memmove(&piece_table[i], &piece_table[i + 1], (size_t)((num_pieces - i - 1) * sizeof(Piece)));
            num_pieces--; i--;
        }
    }
    
    /* 3. Shift the Line Scrolling Index (Pointer Math Optimization) */
    {
        LineIndex* ptr = stream_lines;
        LineIndex* end = stream_lines + total_stream_lines;
        while (ptr < end) {
            if (ptr->global_offset > target_global) ptr->global_offset--;
            ptr++;
        }
    }
    
    /* 4. Merge lines if we deleted a newline! */
    if (deleted_char == '\n') {
        for (i = 1; i < total_stream_lines; i++) {
            if (stream_lines[i].global_offset == target_global) {
                memmove(&stream_lines[i], &stream_lines[i + 1], (size_t)((total_stream_lines - i - 1) * sizeof(LineIndex)));
                total_stream_lines--;
                break;
            }
        }
    }
}

void insert_into_stream(char c) {
    long target_global = stream_lines[cursor_y].global_offset + cursor_x;
    int p_idx = 0, i;
    long piece_start = 0;

    /* 1. Find the piece the cursor is inside */
    while (p_idx < num_pieces) {
        if (target_global >= piece_start && target_global <= piece_start + piece_table[p_idx].length) break;
        piece_start += piece_table[p_idx].length;
        p_idx++;
    }
    
    /* Safely clamp to the end of the file if needed */
    if (p_idx >= num_pieces) {
        p_idx = num_pieces - 1;
        piece_start -= piece_table[p_idx].length;
        if (target_global > piece_start + piece_table[p_idx].length) 
            target_global = piece_start + piece_table[p_idx].length;
    }

    Piece* p = &piece_table[p_idx];
    long local_offset = target_global - piece_start;

    /* 2. Drop the character into the fast RAM bucket */
    if (add_buffer_size >= add_buffer_capacity) {
        long new_cap = add_buffer_capacity * 2;
        char* temp = (char*)realloc(add_buffer, (size_t)new_cap);
        if (!temp) return; /* OOM Guard */
        add_buffer_capacity = new_cap;
        add_buffer = temp;
    }
    long new_ram_offset = add_buffer_size;
    add_buffer[add_buffer_size++] = c;

    /* 3. The Split & Sandwich */
    if (p->source == SOURCE_RAM && local_offset == p->length && p->offset + p->length == new_ram_offset) {
        /* Optimization: Just extend the current RAM piece! */
        p->length++;
    } else {
        if (num_pieces + 2 >= piece_capacity) {
            long new_p_cap = piece_capacity * 2;
            Piece* temp = (Piece*)realloc(piece_table, (size_t)(new_p_cap * sizeof(Piece)));
            if (!temp) return; 
            piece_capacity = new_p_cap;
            piece_table = temp;
            p = &piece_table[p_idx]; 
        }
        
        if (local_offset == 0) {
            /* Insert Before */
            memmove(&piece_table[p_idx + 1], &piece_table[p_idx], (size_t)((num_pieces - p_idx) * sizeof(Piece)));
            piece_table[p_idx].source = SOURCE_RAM; piece_table[p_idx].offset = new_ram_offset; piece_table[p_idx].length = 1;
            num_pieces++;
        } else if (local_offset == p->length) {
            /* Insert After */
            memmove(&piece_table[p_idx + 2], &piece_table[p_idx + 1], (size_t)((num_pieces - p_idx - 1) * sizeof(Piece)));
            piece_table[p_idx + 1].source = SOURCE_RAM; piece_table[p_idx + 1].offset = new_ram_offset; piece_table[p_idx + 1].length = 1;
            num_pieces++;
        } else {
            /* The 3-Way Split! */
            memmove(&piece_table[p_idx + 2], &piece_table[p_idx], (size_t)((num_pieces - p_idx) * sizeof(Piece)));
            piece_table[p_idx + 2].source = p->source; 
            piece_table[p_idx + 2].offset = p->offset + local_offset; 
            piece_table[p_idx + 2].length = p->length - local_offset;
            
            piece_table[p_idx + 1].source = SOURCE_RAM; 
            piece_table[p_idx + 1].offset = new_ram_offset; 
            piece_table[p_idx + 1].length = 1;
            
            piece_table[p_idx].length = local_offset; 
            num_pieces += 2;
        }
    }

    /* 4. Update the Line Scrolling Index (Pointer Math Optimization) */
    {
        LineIndex* ptr = stream_lines + cursor_y + 1;
        LineIndex* end = stream_lines + total_stream_lines;
        while (ptr < end) {
            ptr->global_offset++;
            ptr++;
        }
    }

    if (c == '\n') {
        if (total_stream_lines >= stream_line_capacity) {
            long new_l_cap = stream_line_capacity * 2;
            if (new_l_cap * sizeof(LineIndex) <= 64000L) {
                LineIndex* temp = (LineIndex*)realloc(stream_lines, (size_t)(new_l_cap * sizeof(LineIndex)));
                if (temp) { stream_line_capacity = new_l_cap; stream_lines = temp; }
                else return; /* FIX: Abort typing if out of memory! */
            } else return; /* FIX: Abort typing if at 64KB limit! */
        }
        memmove(&stream_lines[cursor_y + 2], &stream_lines[cursor_y + 1], (size_t)((total_stream_lines - cursor_y - 1) * sizeof(LineIndex)));
        stream_lines[cursor_y + 1].global_offset = target_global + 1;
        total_stream_lines++;
        cursor_y++; cursor_x = 0;
    } else {
        cursor_x++;
    }
    
    is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
}

char* get_streamed_segment(int line_idx, long start_col, int max_chars) {
    long global_offset, piece_start_global = 0, local_offset;
    int p_idx = 0, len = 0;
    char* line_buf;
    long line_start_global;
    
    if (line_idx < 0 || line_idx >= total_stream_lines) return NULL;
    
    line_start_global = stream_lines[line_idx].global_offset;
    global_offset = line_start_global + start_col;
    
    /* 1. Find which piece this exact column starts in */
    while (p_idx < num_pieces) {
        if (global_offset >= piece_start_global && global_offset < piece_start_global + piece_table[p_idx].length) break;
        piece_start_global += piece_table[p_idx].length;
        p_idx++;
    }
    
    line_buf = (char*)malloc(max_chars + 1);
    if (!line_buf) return NULL;
    
    if (p_idx >= num_pieces) {
        line_buf[0] = '\0';
        return line_buf;
    }
    
    local_offset = global_offset - piece_start_global;
    
    /* 2. Read EXACTLY max_chars across pieces until newline or EOF */
    while (p_idx < num_pieces && len < max_chars) {
        Piece* p = &piece_table[p_idx];
        long bytes_to_read = p->length - local_offset;
        
        if (p->source == SOURCE_DISK) {
            if (stream_file_handle) {
                fseek(stream_file_handle, p->offset + local_offset, SEEK_SET);
                while (bytes_to_read > 0 && len < max_chars) {
                    int ch = fgetc(stream_file_handle);
                    if (ch == EOF || ch == '\n') goto done_read;
                    if (ch != '\r') line_buf[len++] = (char)ch;
                    bytes_to_read--;
                }
            }
        } else if (p->source == SOURCE_RAM) {
            long r_idx = p->offset + local_offset;
            while (bytes_to_read > 0 && r_idx < add_buffer_size && len < max_chars) {
                char ch = add_buffer[r_idx++];
                if (ch == '\n') goto done_read;
                if (ch != '\r') line_buf[len++] = ch;
                bytes_to_read--;
            }
        }
        p_idx++;
        local_offset = 0; 
    }
    
done_read:
    line_buf[len] = '\0';
    return line_buf;
}

/* Blank Table Initializer for Untitled.txt */
void init_empty_piece_table() {
    if (stream_file_handle) { fclose(stream_file_handle); stream_file_handle = NULL; }
    if (add_buffer) free(add_buffer);
    add_buffer_capacity = 4096;
    add_buffer_size = 0;
    add_buffer = (char*)malloc(add_buffer_capacity);
    
    if (piece_table) free(piece_table);
    piece_capacity = 100;
    num_pieces = 1;
    piece_table = (Piece*)malloc(piece_capacity * sizeof(Piece));
    piece_table[0].source = SOURCE_RAM;
    piece_table[0].offset = 0;
    piece_table[0].length = 0;
    
    if (stream_lines) free(stream_lines);
    stream_line_capacity = 1000;
    total_stream_lines = 1;
    stream_lines = (LineIndex*)malloc(stream_line_capacity * sizeof(LineIndex));
    stream_lines[0].global_offset = 0;
}

void handle_mouse_clicks(int last_mouse_left) {
    /* 1. Declare the missing local variables */
    int right_col = SCREEN_COLS - 1;
    int bottom_row = SCREEN_ROWS - 1;
    int top_y = show_find_dialog ? 10 : 3;
    int view_w = setting_v_scrollbar ? (SCREEN_COLS - 3) : (SCREEN_COLS - 1);
    int target_y;
    long log_y;
    long target_x;
    
    /* 2. Declare static variables for timing and double-clicks */
    static unsigned int last_scroll_tick = 0; // Fixes 'last_scroll_tick' error
    static unsigned int last_canvas_tick = 0;
    static unsigned int last_click_ticks = 0; 
    static int last_click_cx = -1, last_click_cy = -1;

    /* 3. Use 16-bit tick counting */
    unsigned int current_ticks = (unsigned int)GET_TICKS();

    /* --- UNCOMMENTED MOUSE LOGIC STARTS HERE --- */
    if (active_menu > 0) {
        char old_menu = active_menu;
        if (mouse_y == 1 && mouse_x >= 1 && mouse_x <= 6) active_menu = 1; 
        else if (mouse_y == 1 && mouse_x >= 8 && mouse_x <= 13) active_menu = 2; 
        else if (mouse_y == 1 && mouse_x >= 15 && mouse_x <= 20) active_menu = 3;
        else if (active_menu == 1 && mouse_y >= 3 && mouse_y <= 12 && mouse_x >= 1 && mouse_x <= 15) {
            execute_menu_action(1, mouse_y);
        }
        else if (active_menu == 2 && mouse_y >= 3 && mouse_y <= 12 && mouse_x >= 7 && mouse_x <= 22) {
            execute_menu_action(2, mouse_y);
        }
        else if (active_menu == 3 && mouse_y >= 3 && mouse_y <= 11 && mouse_x >= 14 && mouse_x <= 36) {
            execute_menu_action(3, mouse_y);
        } else { 
            active_menu = 0; active_menu_hover = -1; needs_ui_redraw = 1; 
        }
        if (old_menu != active_menu) needs_ui_redraw = 1; 
        draw_editor(); while(mouse_left) update_mouse(); return;
    }
    if (mouse_y == 1 && !is_dragging) {
        if (mouse_x >= 1 && mouse_x <= 6) { active_menu = 1; active_menu_hover = -1; }
        else if (mouse_x >= 8 && mouse_x <= 13) { active_menu = 2; active_menu_hover = -1; }
        else if (mouse_x >= 15 && mouse_x <= 20) { active_menu = 3; active_menu_hover = -1; }
        else if (mouse_x >= right_col - 3 && mouse_x <= right_col - 1) { 
            if (is_document_dirty) { show_exit_dialog = 1; active_tab_element = 1; } else clean_exit(); 
        }
        if (active_menu > 0 || show_exit_dialog) { 
            needs_ui_redraw = 1; draw_editor(); while(mouse_left) update_mouse(); return; 
        }
    }
    
    /* Lite Settings Screen Clicks */
    if (show_settings) {
        int right = SCREEN_COLS - 1;
        if (mouse_y >= 3 && mouse_y <= 5 && mouse_x >= right - 13 && mouse_x <= right - 4) {
            show_settings = 0;
            needs_ui_redraw = 1;
            draw_editor();
        }
        while(mouse_left) update_mouse(); return;
    }

    if (mouse_y == bottom_row - 1 && mouse_x == right_col - 1) { 
        show_settings = 1; 
        active_tab_element = 0;
        needs_ui_redraw = 1;
        draw_editor();
        while(mouse_left) update_mouse(); return;
    }

    /* RESTORED DIALOG MOUSE LOGIC */
    if (show_context_menu && !last_mouse_left) {
        show_context_menu = 0; 
        needs_ui_redraw = 1; 
        draw_editor();
        if (mouse_x >= ctx_x && mouse_x <= ctx_x + 15 && mouse_y >= ctx_y && mouse_y <= ctx_y + 11) {
            if (mouse_y == ctx_y + 1) perform_undo();
            else if (mouse_y == ctx_y + 2) perform_redo();
            else if (mouse_y == ctx_y + 4) { 
                if (has_selection) { save_state(); last_was_typing = 0; copy_selection(); delete_selection(); }
            }
            else if (mouse_y == ctx_y + 5) copy_selection();
            else if (mouse_y == ctx_y + 6) paste_clipboard();
            else if (mouse_y == ctx_y + 7) {
                if (has_selection) { save_state(); last_was_typing = 0; delete_selection(); }
            }
            else if (mouse_y == ctx_y + 9) { 
                sel_start_x = 0; sel_start_y = 0; 
                if (setting_disk_stream) { cursor_y = total_stream_lines - 1; cursor_x = get_stream_logical_len(cursor_y); }
                else { cursor_y = total_lines - 1; cursor_x = (int)strlen(text_buffer[cursor_y]); }
                has_selection = 1; force_cursor_snap = 1; last_was_typing = 0;
            }
        }
        do { update_mouse(); } while (mouse_left); return;
    }

    if (show_open_dialog || show_save_dialog) {
        int right = SCREEN_COLS - 1;
        int bottom = SCREEN_ROWS - 1;
        int max_items = (SCREEN_COLS < 80) ? 8 : (bottom - 15);
        int track_h = (SCREEN_COLS < 80) ? 8 : (bottom - 16);
        int sc_down_y = (SCREEN_COLS < 80) ? 17 : (bottom - 7);
        int sc_x = (SCREEN_COLS < 80) ? 36 : (right - 3);
        
        if (SCREEN_COLS < 80) {
            if (mouse_y == 20 && mouse_x >= 23 && mouse_x <= 28) handle_file_ok_click();
            else if (mouse_y == 20 && mouse_x >= 30 && mouse_x <= 37) { show_open_dialog = 0; show_save_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } 
            else if (mouse_y == 1 && mouse_x >= 36 && mouse_x <= 38) { show_open_dialog = 0; show_save_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } 
            else if (mouse_y >= 9 && mouse_y <= 16 && mouse_x >= 2 && mouse_x <= 11) { 
                char drive_path[4]; drive_path[0] = (char)('C' + (mouse_y - 9)); drive_path[1] = ':'; drive_path[2] = '\\'; drive_path[3] = '\0'; load_directory(drive_path, 1);
            }
            else if (mouse_y == 4) {
                if (mouse_x >= 2 && mouse_x <= 4 && path_history_index > 0) { path_history_index--; load_directory(path_history[path_history_index], 0); } 
                else if (mouse_x >= 5 && mouse_x <= 7 && path_history_index < path_history_count - 1) { path_history_index++; load_directory(path_history[path_history_index], 0); } 
                else if (mouse_x >= 9 && mouse_x <= 11) {
                    char new_path[256]; int len = (int)strlen(current_browser_path);
                    if (len > 3) { len -= 2; while (len > 0 && current_browser_path[len] != '\\') len--; strncpy(new_path, current_browser_path, len + 1); new_path[len + 1] = '\0'; load_directory(new_path, 1); }
                } 
                else if (mouse_x >= 13 && mouse_x <= 28) { active_input_field = 3; strcpy(dialog_address_text, current_browser_path); dialog_address_cursor = (int)strlen(dialog_address_text); strcpy(dialog_status_msg, "Editing address..."); } 
                else if (mouse_x >= 30 && mouse_x <= 37) { active_input_field = 2; dialog_search_cursor = mouse_x - 31; if (dialog_search_cursor > (int)strlen(dialog_search_text)) dialog_search_cursor = (int)strlen(dialog_search_text); strcpy(dialog_status_msg, "Searching..."); }
            }
            else if (mouse_y == 20 && mouse_x >= 13 && mouse_x <= 21) {
                active_input_field = 1; dialog_input_cursor = mouse_x - 14; if (dialog_input_cursor > (int)strlen(dialog_input_text)) dialog_input_cursor = (int)strlen(dialog_input_text); strcpy(dialog_status_msg, show_save_dialog ? "Ready to save file..." : "Ready to select file...");
            }
            else if (mouse_y >= 9 && mouse_y <= 16 && mouse_x >= 14 && mouse_x <= 34) { 
                int idx = browser_scroll_offset + (mouse_y - 9); 
                if (idx < num_browser_files) { 
                    browser_selected_index = idx; 
                    if (browser_files[idx].is_dir) { 
                        char new_path[256]; 
                        if (strcmp(browser_files[idx].name, "..") == 0) { int len = (int)strlen(current_browser_path); if (len > 3) { len -= 2; while (len > 0 && current_browser_path[len] != '\\') len--; current_browser_path[len + 1] = '\0'; } strcpy(new_path, current_browser_path); } 
                        else { strcpy(new_path, current_browser_path); strcat(new_path, browser_files[idx].name); } 
                        load_directory(new_path, 1); 
                    } else { 
                        strcpy(dialog_input_text, browser_files[idx].name); dialog_input_cursor = (int)strlen(dialog_input_text); active_input_field = 1; strcpy(dialog_status_msg, show_save_dialog ? "Ready to save file..." : "Ready to select file..."); 
                    } 
                } 
            } else { active_input_field = 0; }
        } else {
            if (mouse_y == bottom - 4 && mouse_x >= right - 17 && mouse_x <= right - 12) handle_file_ok_click();
            else if (mouse_y == bottom - 4 && mouse_x >= right - 8 && mouse_x <= right - 3) { show_open_dialog = 0; show_save_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } 
            else if (mouse_y == 1 && mouse_x >= right - 3 && mouse_x <= right - 1) { show_open_dialog = 0; show_save_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } 
            else if (mouse_y >= 9 && mouse_y <= bottom - 7 && mouse_x >= 2 && mouse_x <= 12) { 
                int d_idx = mouse_y - 9;
                if (d_idx < 10) { char drive_path[4]; drive_path[0] = (char)('C' + d_idx); drive_path[1] = ':'; drive_path[2] = '\\'; drive_path[3] = '\0'; load_directory(drive_path, 1); }
            }
            else if (mouse_y == 4) {
                if (mouse_x >= 2 && mouse_x <= 5 && path_history_index > 0) { path_history_index--; load_directory(path_history[path_history_index], 0); } 
                else if (mouse_x >= 7 && mouse_x <= 10 && path_history_index < path_history_count - 1) { path_history_index++; load_directory(path_history[path_history_index], 0); } 
                else if (mouse_x >= 12 && mouse_x <= 14) {
                    char new_path[256]; int len = (int)strlen(current_browser_path);
                    if (len > 3) { len -= 2; while (len > 0 && current_browser_path[len] != '\\') len--; strncpy(new_path, current_browser_path, len + 1); new_path[len + 1] = '\0'; load_directory(new_path, 1); }
                } 
                else if (mouse_x >= 18 && mouse_x <= right - 21) { active_input_field = 3; strcpy(dialog_address_text, current_browser_path); dialog_address_cursor = (int)strlen(dialog_address_text); strcpy(dialog_status_msg, "Editing address..."); } 
                else if (mouse_x >= right - 16 && mouse_x <= right - 3) { active_input_field = 2; dialog_search_cursor = mouse_x - (right - 16); if (dialog_search_cursor > (int)strlen(dialog_search_text)) dialog_search_cursor = (int)strlen(dialog_search_text); strcpy(dialog_status_msg, "Searching..."); }
            }
            else if (mouse_y == bottom - 4 && mouse_x >= 17 && mouse_x <= right - 20) {
                active_input_field = 1; dialog_input_cursor = mouse_x - 17; if (dialog_input_cursor > (int)strlen(dialog_input_text)) dialog_input_cursor = (int)strlen(dialog_input_text); strcpy(dialog_status_msg, show_save_dialog ? "Ready to save file..." : "Ready to select file...");
            }
            else if (mouse_y >= 9 && mouse_y <= bottom - 7 && mouse_x >= 18 && mouse_x <= right - 4) { 
                int idx = browser_scroll_offset + (mouse_y - 9); 
                if (idx < num_browser_files) { 
                    browser_selected_index = idx; 
                    if (browser_files[idx].is_dir) { 
                        char new_path[256]; 
                        if (strcmp(browser_files[idx].name, "..") == 0) { int len = (int)strlen(current_browser_path); if (len > 3) { len -= 2; while (len > 0 && current_browser_path[len] != '\\') len--; current_browser_path[len + 1] = '\0'; } strcpy(new_path, current_browser_path); } 
                        else { strcpy(new_path, current_browser_path); strcat(new_path, browser_files[idx].name); } 
                        load_directory(new_path, 1); 
                    } else { 
                        strcpy(dialog_input_text, browser_files[idx].name); dialog_input_cursor = (int)strlen(dialog_input_text); active_input_field = 1; strcpy(dialog_status_msg, show_save_dialog ? "Ready to save file..." : "Ready to select file..."); 
                    } 
                } 
            } else { active_input_field = 0; }
        }
        
        /* Universal Scrollbar Logic */
        if (mouse_y == 7 && mouse_x == sc_x) { if (browser_scroll_offset > 0) browser_scroll_offset--; }
        else if (mouse_y == sc_down_y && mouse_x == sc_x) { if (browser_scroll_offset < num_browser_files - max_items) browser_scroll_offset++; }
        else if (mouse_y >= 9 && mouse_y <= sc_down_y - 1 && mouse_x == sc_x) {
            int lsize = num_browser_files;
            if (lsize > max_items) {
                int tsize = (int)((track_h * 1L * track_h) / lsize);
                if (tsize < 1) tsize = 1;
                int denom = track_h - tsize;
                if (denom < 1) denom = 1;
                
                while (mouse_left) {
                    int clamped_y = mouse_y; 
                    if (clamped_y < 9) clamped_y = 9; 
                    if (clamped_y > 9 + denom) clamped_y = 9 + denom;
                    
                    int new_offset = (int)(((long)(clamped_y - 9) * (lsize - max_items) + (denom/2)) / denom);
                    if (new_offset < 0) new_offset = 0; 
                    if (new_offset > lsize - max_items) new_offset = lsize - max_items;
                    
                    if (new_offset != browser_scroll_offset) { browser_scroll_offset = new_offset; needs_ui_redraw = 1; draw_editor(); }
                    update_mouse();
                }
            }
        }
        needs_ui_redraw = 1; draw_editor();
        do { update_mouse(); } while (mouse_left); return;
    }

    if (show_exit_dialog) {
        int is_40 = (SCREEN_COLS < 80);
        int box_w = is_40 ? 38 : 44;
        int start_x = (SCREEN_COLS - box_w) / 2;
        int start_y = (SCREEN_ROWS - 10) / 2;
        int s1 = is_40 ? 2 : 4;  int e1 = is_40 ? 9 : 11;
        int s2 = is_40 ? 12 : 15; int e2 = is_40 ? 25 : 28;
        int s3 = is_40 ? 28 : 32; int e3 = is_40 ? 35 : 39;

        if (mouse_y == start_y + 7) { 
            if (mouse_x >= start_x + s1 && mouse_x <= start_x + e1) { /* Save */
                if (strcmp(current_filename, "Untitled.txt") == 0) { 
                    show_exit_dialog = 0; show_save_dialog = 1; active_tab_element = 1; active_input_field = 1;
                    strcpy(dialog_status_msg, "Ready to save file..."); 
                    load_directory(current_browser_path, 1);
                    needs_ui_redraw = 1; draw_editor();
                } else { 
                    char filepath[256]; 
                    strcpy(filepath, current_browser_path); strcat(filepath, current_filename); 
                    save_document(filepath);
                    running = 0;
                } 
            } 
            else if (mouse_x >= start_x + s2 && mouse_x <= start_x + e2) running = 0; /* Don't Save */
            else if (mouse_x >= start_x + s3 && mouse_x <= start_x + e3) { show_exit_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } /* Cancel */
        }
        do { update_mouse(); } while (mouse_left); return;
    }

    if (show_find_dialog) {
        if (SCREEN_COLS < 80) {
            if (mouse_y >= 3 && mouse_y <= 5 && mouse_x >= 22 && mouse_x <= 28) execute_find_replace(1);
            else if (mouse_y >= 3 && mouse_y <= 5 && mouse_x >= 30 && mouse_x <= 36) execute_find_replace(4);
            else if (mouse_y >= 6 && mouse_y <= 8 && mouse_x >= 22 && mouse_x <= 28) execute_find_replace(2);
            else if (mouse_y >= 6 && mouse_y <= 8 && mouse_x >= 30 && mouse_x <= 36) execute_find_replace(3);
            else if (mouse_y == 4 && mouse_x >= 12 && mouse_x <= 19) active_input_field = 4;
            else if (mouse_y == 7 && mouse_x >= 12 && mouse_x <= 19) active_input_field = 5;
        } else {
            if (mouse_y >= 3 && mouse_y <= 5 && mouse_x >= 53 && mouse_x <= 63) execute_find_replace(1);
            else if (mouse_y >= 3 && mouse_y <= 5 && mouse_x >= 65 && mouse_x <= 75) execute_find_replace(4);
            else if (mouse_y >= 6 && mouse_y <= 8 && mouse_x >= 53 && mouse_x <= 63) execute_find_replace(2);
            else if (mouse_y >= 6 && mouse_y <= 8 && mouse_x >= 65 && mouse_x <= 75) execute_find_replace(3);
            else if (mouse_y == 4 && mouse_x >= 12 && mouse_x <= 50) active_input_field = 4;
            else if (mouse_y == 7 && mouse_x >= 12 && mouse_x <= 50) active_input_field = 5;
        }
        do { update_mouse(); } while (mouse_left); return;
    }

    /* TEXT SELECTION & DOUBLE CLICK LOGIC */
    if (mouse_y >= top_y && mouse_y <= bottom_row - 3 && mouse_x >= 1 && mouse_x <= view_w) {
        active_input_field = 0; 
        target_y = scroll_y + (mouse_y - top_y);

        long log_y;
        long target_x;
        int valid_click = 0;

        if (target_y < vis_lines) {
            log_y = vis_map[target_y].log_y;
            target_x = vis_map[target_y].log_x + scroll_x + (mouse_x - 1);
            if (target_x > vis_map[target_y].log_x + vis_map[target_y].len) {
                target_x = vis_map[target_y].log_x + vis_map[target_y].len;
            }
            valid_click = 1;
        }
        
        if (valid_click) {
            long prev_cx = cursor_x;
            long prev_cy = cursor_y;
            int trigger_redraw = 0;

            if (mouse_left && !last_mouse_left) {
                if (current_ticks - last_click_ticks < 9 && log_y == last_click_cy && target_x == last_click_cx) {
                    long sx = target_x, ex = target_x; 
                    if (!setting_disk_stream) { 
                        char* line = text_buffer[log_y]; 
                        int len = (int)strlen(line);
                        while (sx > 0 && (isalnum(line[sx-1]) || line[sx-1] == '_')) sx--;
                        while (ex < len && (isalnum(line[ex]) || line[ex] == '_')) ex++;
                    }
                    sel_start_x = sx; sel_start_y = log_y; 
                    cursor_x = ex; cursor_y = log_y;
                    has_selection = 1; is_dragging = 1; force_cursor_snap = 1; last_was_typing = 0;
                    last_click_ticks = 0; 
                } else {
                    last_click_ticks = current_ticks; 
                    last_click_cy = log_y; 
                    last_click_cx = target_x;
                    is_dragging = 1; 
                    sel_start_x = target_x; 
                    sel_start_y = log_y; 
                    has_selection = 0;
                    cursor_y = log_y; cursor_x = target_x;
                }
                trigger_redraw = 1;
                last_canvas_tick = current_ticks;
            } else if (mouse_left && last_mouse_left && is_dragging) {
                if (current_ticks - last_canvas_tick >= 1) {
                    if (target_x != sel_start_x || log_y != sel_start_y) has_selection = 1;
                    cursor_y = log_y; cursor_x = target_x;
                    trigger_redraw = 1;
                    last_canvas_tick = current_ticks;
                }
            }
            
            long m_len = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]); 
            if (cursor_x > m_len) cursor_x = m_len;
            
            if (trigger_redraw && (cursor_x != prev_cx || cursor_y != prev_cy || (mouse_left && !last_mouse_left))) {
                if (is_dragging) force_cursor_snap = 1;
                draw_editor();
            }
        }
    }
    /* SCROLLBAR INTERACTION LOGIC */
    if (setting_v_scrollbar && mouse_x == SCREEN_COLS - 2) {
        
        /* 1. Calculate the TRUE mathematical height of the document */
        long effective_vis_lines;
        int max_c = setting_v_scrollbar ? (SCREEN_COLS - 4) : (SCREEN_COLS - 2);
        if (setting_disk_stream) {
            effective_vis_lines = setting_word_wrap ? get_stream_total_vis_lines(max_c) : total_stream_lines;
        } else {
            effective_vis_lines = vis_lines;
        }

        if (mouse_y == top_y) { 
            if (!last_mouse_left || (current_ticks - last_scroll_tick >= 2)) {
                if (setting_disk_stream && total_stream_lines > CHUNK_LOG_SIZE) { cursor_y -= 1; if (cursor_y < 0) cursor_y = 0; force_cursor_snap = 1; }
                else if (scroll_y > 0) scroll_y--; 
                needs_ui_redraw = 1; 
                last_scroll_tick = current_ticks;
            }
        } 
        else if (mouse_y == bottom_row - 3) { 
            if (!last_mouse_left || (current_ticks - last_scroll_tick >= 2)) {
                if (setting_disk_stream && total_stream_lines > CHUNK_LOG_SIZE) { cursor_y += 1; if (cursor_y >= total_stream_lines) cursor_y = total_stream_lines - 1; force_cursor_snap = 1; }
                else {
                    int vis_h = SCREEN_ROWS - 3 - top_y;
                    long max_sc_y = effective_vis_lines - vis_h;
                    if (max_sc_y < 0) max_sc_y = 0;
                    if (scroll_y < max_sc_y) scroll_y++;
                }
                needs_ui_redraw = 1; 
                last_scroll_tick = current_ticks;
            }
        } 
        else if (mouse_y >= 5 && mouse_y <= bottom_row - 5) { /* FIX: Scroll track is now 1 row shorter on both ends! */
            int vis_h = SCREEN_ROWS - 3 - top_y;
            int track_h = bottom_row - 9; /* FIX: Reduced track physical height! */
            
            if (setting_disk_stream && total_stream_lines > CHUNK_LOG_SIZE) {
                /* Chunked Scrollbar Dragging (Navigates entire file) */
                int tsize = (total_stream_lines > CHUNK_LOG_SIZE) ? (int)((CHUNK_LOG_SIZE * track_h) / total_stream_lines) : track_h;
                if (tsize < 1) tsize = 1;
                if (tsize > track_h) tsize = track_h;
                int denom = track_h - tsize;
                if (denom < 1) denom = 1;
                
                while (mouse_left) {
                    int offset_y = mouse_y - (tsize / 2);
                    if (offset_y < 5) offset_y = 5;
                    if (offset_y > 5 + denom) offset_y = 5 + denom;
                    
                    long new_log_y = (total_stream_lines > 0) ? (long)(((long)(offset_y - 5) * total_stream_lines) / denom) : 0;
                    if (new_log_y >= total_stream_lines) new_log_y = total_stream_lines - 1;
                    
                    if (cursor_y != new_log_y) {
                        cursor_y = new_log_y;
                        cursor_x = 0;
                        force_cursor_snap = 1;
                        needs_ui_redraw = 1;
                        draw_editor();
                    }
                    update_mouse();
                }
            } else {
                long max_sc_y = effective_vis_lines - vis_h;
                if (max_sc_y < 0) max_sc_y = 0;
                long req_vp = (effective_vis_lines + vis_h - 1) / vis_h; 
                if (req_vp == 0) req_vp = 1;
                
                int tsize = (req_vp > 1) ? (int)((track_h * 1L + req_vp / 2) / req_vp) : track_h; 
                if (tsize < 1) tsize = 1; 
                if (tsize > track_h) tsize = track_h;
                int denom = track_h - tsize;
                if (denom < 1) denom = 1;

                while (mouse_left) {
                    int offset_y = mouse_y - (tsize / 2);
                    if (offset_y < 5) offset_y = 5;
                    if (offset_y > 5 + denom) offset_y = 5 + denom;
                    
                    long new_scroll_y = (max_sc_y > 0) ? (long)(((long)(offset_y - 5) * max_sc_y) / denom) : 0;
                    if (new_scroll_y > max_sc_y) new_scroll_y = max_sc_y;

                    if (scroll_y != new_scroll_y) {
                        scroll_y = new_scroll_y;
                        needs_ui_redraw = 1;
                        draw_editor();
                    }
                    update_mouse();
                }
            }
        }
        draw_editor();
    }
    else if (setting_h_scrollbar && mouse_y == SCREEN_ROWS - 2 && !setting_word_wrap) {
        int right_arrow_x = setting_v_scrollbar ? SCREEN_COLS - 4 : SCREEN_COLS - 2;
        long m_scroll = (max_line_len >= view_w ? max_line_len - view_w + 1 : 0);
        
        if (mouse_x == 1) { 
            if (!last_mouse_left || (current_ticks - last_scroll_tick >= 2)) {
                scroll_x -= 5; 
                if (scroll_x < 0) scroll_x = 0; 
                needs_ui_redraw = 1; 
                last_scroll_tick = current_ticks;
            }
        }
        else if (mouse_x == right_arrow_x) { 
            if (!last_mouse_left || (current_ticks - last_scroll_tick >= 2)) {
                scroll_x += 5; 
                if (scroll_x > m_scroll) scroll_x = m_scroll; 
                needs_ui_redraw = 1; 
                last_scroll_tick = current_ticks;
            }
        }
        else if (mouse_x >= 3 && mouse_x <= right_arrow_x - 2) {
            int track_w = right_arrow_x - 4;
            int h_tsize = (m_scroll > 0) ? (int)((track_w * view_w) / max_line_len) : track_w;
            if (h_tsize < 3) h_tsize = 3; 
            if (h_tsize > track_w) h_tsize = track_w;
            int denom = track_w - h_tsize;
            if (denom < 1) denom = 1;

            while (mouse_left) {
                int offset_x = mouse_x - (h_tsize / 2);
                if (offset_x < 3) offset_x = 3;
                if (offset_x > 3 + denom) offset_x = 3 + denom;
                
                int new_scroll_x = (m_scroll > 0) ? (int)(((long)(offset_x - 3) * m_scroll) / denom) : 0;
                if (new_scroll_x < 0) new_scroll_x = 0;
                if (new_scroll_x > m_scroll) new_scroll_x = m_scroll;
                
                if (scroll_x != new_scroll_x) {
                    scroll_x = new_scroll_x;
                    needs_ui_redraw = 1;
                    draw_editor();
                }
                update_mouse();
            }
        }
        draw_editor(); /* FIX: Force immediate redraw when holding horizontal scroll arrows! */
    }
}

int main(int argc, char* argv[]) {
    int last_mouse_x = -1, last_mouse_y = -1, key; 
    int r, c;
    int last_mouse_left = 0, last_mouse_right = 0;
    char cwd_buf[256];
    
    int alt_state = 0;
    int last_alt_state = 0;
    int keys_pressed_during_alt = 0;
    
    // --- FIX: Declare dynamic edges here so the whole function can see them! ---
    int right_col;
    int bottom_row;
    int i; /* for heap initialization */

    /* EVICT MASSIVE ARRAYS FROM 64KB DGROUP TO THE HEAP! */
    all_browser_files = (FileItem*)malloc(100 * sizeof(FileItem));
    browser_files = (FileItem*)malloc(100 * sizeof(FileItem));
    
    path_history = (char**)malloc(20 * sizeof(char*));
    for (i = 0; i < 20; i++) {
        path_history[i] = (char*)malloc(256);
    }

    /* --- NEW EVICTIONS --- */
    /* Find & Replace RAM buffers stripped */
    current_filename = (char*)malloc(256); strcpy(current_filename, "Untitled.txt");
    exe_dir = (char*)malloc(256); exe_dir[0] = '\0';
    dialog_input_text = (char*)malloc(256); dialog_input_text[0] = '\0';
    dialog_search_text = (char*)malloc(256); dialog_search_text[0] = '\0';
    dialog_address_text = (char*)malloc(256); strcpy(dialog_address_text, "C:\\");
    current_browser_path = (char*)malloc(256); strcpy(current_browser_path, "C:\\");
    dialog_status_msg = (char*)malloc(256); strcpy(dialog_status_msg, "Ready...");
    
    undo_stack = (EditorSnapshot*)malloc(MAX_UNDO_LEVELS * sizeof(EditorSnapshot));
    redo_stack = (EditorSnapshot*)malloc(MAX_UNDO_LEVELS * sizeof(EditorSnapshot));

    detect_screen();
    
    right_col = SCREEN_COLS - 1;
    bottom_row = SCREEN_ROWS - 1;
    
    if (argv[0] != NULL) {
        /* FIX: Bulletproof manual extraction to bypass missing strrchr prototype */
        char* last_slash = NULL;
        char* t_ptr = argv[0];
        while (*t_ptr) {
            if (*t_ptr == '\\' || *t_ptr == '/') last_slash = t_ptr;
            t_ptr++;
        }
        if (last_slash) {
            strncpy(exe_dir, argv[0], last_slash - argv[0] + 1);
            exe_dir[last_slash - argv[0] + 1] = '\0';
        }
    }
    
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) {
        strcpy(current_browser_path, cwd_buf);
        strcpy(dialog_address_text, current_browser_path);
        dialog_address_cursor = (int)strlen(dialog_address_text);
    }
    
    load_config();

    /* Force UI into monochrome if MDA display is detected! */
    if (video_mode == 7) {
        setting_color = 0;
        setting_bg_color = 0;
        setting_fg_color = 7;
    }

    // --- NEW CURSOR ACTIVATION LOGIC ---
    use_software_cursor = !(SCREEN_COLS <= 80 && SCREEN_ROWS == 25);
    
    if (font_height <= 8) {
        load_cursor_glyph88(); 
    } else {
        load_cursor_glyph(); 
    }
    // -----------------------------------

    init_buffer();
    hide_hardware_cursor();
    
    if (!init_mouse()) {
        //printf("Notice: DOS Mouse driver not loaded.\n");
    }

    set_mouse_range(SCREEN_COLS, SCREEN_ROWS);
    
    /* Attempt to load file passed as argument */
    if (argc > 1) {
        /* FIX: CLI Path Initialization! Bulletproof manual extraction */
        char* global_last_slash = NULL;
        char* t_ptr2 = argv[1];
        while (*t_ptr2) {
            if (*t_ptr2 == '\\' || *t_ptr2 == '/') global_last_slash = t_ptr2;
            t_ptr2++;
        }
        
        if (global_last_slash) {
            int dir_len = (int)(global_last_slash - argv[1]) + 1;
            strncpy(current_browser_path, argv[1], dir_len);
            current_browser_path[dir_len] = '\0';
            strcpy(dialog_address_text, current_browser_path);
            dialog_address_cursor = (int)strlen(dialog_address_text);
        }

        if (setting_disk_stream) {
            /* NEW ENGINE: Route straight to the Piece Table! */
            init_piece_table(argv[1]);
            if (stream_file_handle) {
                setting_read_only = (total_stream_lines > 6643) ? 1 : 0;
                if (global_last_slash) strcpy(current_filename, global_last_slash + 1);
                else strcpy(current_filename, argv[1]);
            }
        } else {
            /* OLD ENGINE: Fast RAM Buffer */
            FILE* f = fopen(argv[1], "rb"); 
            if (f) {
                /* Instantly grab the true file size off the hard drive! */
                fseek(f, 0, SEEK_END);
                full_file_chars = ftell(f);
                rewind(f);
                
                char line_chunk[512]; 
                for(i = 0; i < total_lines; i++) free(text_buffer[i]);
                total_lines = 0;
                
                char* current_line = (char*)malloc(1); 
                current_line[0] = '\0'; int cur_len = 0;
                long loaded_chars = 0;
                is_file_truncated = 0;
                
                while (fgets(line_chunk, sizeof(line_chunk), f)) {
                    int chunk_len = (int)strlen(line_chunk); int has_newline = 0;
                    if (chunk_len > 0 && line_chunk[chunk_len - 1] == '\n') has_newline = 1;
                    
                    if (loaded_chars + chunk_len > 6643) {
                        chunk_len = 6643 - loaded_chars;
                        line_chunk[chunk_len] = '\0';
                        is_file_truncated = 1;
                        has_newline = 0;
                    }
                    int raw_chunk = chunk_len;
                    
                    while (chunk_len > 0 && (line_chunk[chunk_len-1] == '\n' || line_chunk[chunk_len-1] == '\r')) { 
                        line_chunk[chunk_len-1] = '\0'; chunk_len--; 
                    }
                    if (cur_len + chunk_len > 32000) has_newline = 1;
                    char* temp_line = (char*)realloc(current_line, (size_t)(cur_len + chunk_len + 1)); 
                    if (!temp_line) break; 
                    current_line = temp_line;
                    strcpy(current_line + cur_len, line_chunk); cur_len += chunk_len;
                    loaded_chars += raw_chunk;
                    
                    if (has_newline) { 
                        if (total_lines >= buffer_capacity) { 
                            long new_cap = buffer_capacity * 2;
                            if (new_cap * sizeof(char*) > 64000L) break; 
                            char** temp_buf = (char**)realloc(text_buffer, (size_t)(new_cap * sizeof(char*))); 
                            if (!temp_buf) break; 
                            buffer_capacity = (int)new_cap; text_buffer = temp_buf;
                        } 
                        text_buffer[total_lines++] = current_line; 
                        current_line = (char*)malloc(1); 
                        if (!current_line) break;
                        current_line[0] = '\0'; cur_len = 0; 
                    }
                    if (is_file_truncated) break;
                }
                /* Allocate space for both the last line AND the warning message */
                if (total_lines >= buffer_capacity) { 
                    char** temp_buf = (char**)realloc(text_buffer, (total_lines + 2) * sizeof(char*)); 
                    if (temp_buf) text_buffer = temp_buf;
                } 
                if (current_line) text_buffer[total_lines++] = current_line;
                
                /* Inject the warning directly into the canvas! */
                if (is_file_truncated) {
                    char* warning_msg = (char*)malloc(100);
                    strcpy(warning_msg, "[file greater than 6643 characters use disk streaming mode to load all of it]");
                    text_buffer[total_lines++] = warning_msg;
                }
                
                fclose(f);
                setting_read_only = 0; /* RAM mode always starts fully editable */
                
                char* last_slash = strrchr(argv[1], '\\');
                if (!last_slash) last_slash = strrchr(argv[1], '/');
                if (last_slash) strcpy(current_filename, last_slash + 1);
                else strcpy(current_filename, argv[1]);
            } else {
                char* last_slash = strrchr(argv[1], '\\');
                if (!last_slash) last_slash = strrchr(argv[1], '/');
                if (last_slash) strcpy(current_filename, last_slash + 1);
                else strcpy(current_filename, argv[1]);
            }
        }
    } else {
        /* new: if launched without a file, setup an empty stream! */
        if (setting_disk_stream) init_empty_piece_table();
    }
    
    draw_editor();

    while (running) {
        update_mouse();
        
        /* --- Alt Key State Tracking --- */
        alt_state = is_alt_pressed();
        if (alt_state && !last_alt_state) {
            keys_pressed_during_alt = 0;
        }
        if (!alt_state && last_alt_state) {
            /* Standalone Alt tap logic */
            if (!keys_pressed_during_alt && !show_open_dialog && !show_save_dialog && !show_exit_dialog && !show_context_menu) {
                if (active_menu == 0) { active_menu = 1; active_menu_hover = 0; }
                else { active_menu = 0; active_menu_hover = -1; }
                needs_ui_redraw = 1;
                draw_editor();
            }
        }
        last_alt_state = alt_state;
        
        if (mouse_right && !last_mouse_right) {
            if (show_open_dialog || show_save_dialog) {
                /* Right click clears file selection so arrow keys return to scrolling */
                if (browser_selected_index != -1) {
                    browser_selected_index = -1;
                    needs_ui_redraw = 1;
                    draw_editor();
                }
            }
            else if (!show_exit_dialog) {
                show_context_menu = 1; 
                active_menu = 0; 
                is_dragging = 0;
                ctx_x = mouse_x; 
                ctx_y = mouse_y;
                if (ctx_x > 64) ctx_x = 64; 
                if (ctx_y > 13) ctx_y = 13;
                needs_ui_redraw = 1; 
                draw_editor();
            }
        }
        
        if (mouse_x != last_mouse_x || mouse_y != last_mouse_y) {
            int hid = get_hover_id(mouse_x, mouse_y);
            /* FIX: Make hover detection completely dynamic for any screen resolution! */
            int current_hover_hbar = (!show_exit_dialog && setting_h_scrollbar && mouse_y == bottom_row - 1 && mouse_x >= 1 && mouse_x <= (setting_v_scrollbar ? right_col - 3 : right_col - 1));
            static int last_hover_hbar = 0;
            
            if (hid != last_hover_id || is_dragging || current_hover_hbar != last_hover_hbar) {
                int hbar_changed = (current_hover_hbar != last_hover_hbar);
                int previous_hid = last_hover_id; /* Save old state to un-hover! */
                last_hover_id = hid;
                last_hover_hbar = current_hover_hbar;
                
                if (active_menu > 0 && mouse_y == 1 && !is_dragging && !show_open_dialog && !show_save_dialog && !show_exit_dialog && !show_context_menu) {
                    int old_menu = active_menu;
                    if (mouse_x >= 1 && mouse_x <= 6) active_menu = 1; 
                    else if (mouse_x >= 8 && mouse_x <= 13) active_menu = 2; 
                    else if (mouse_x >= 15 && mouse_x <= 20) active_menu = 3;
                    
                    if (active_menu != old_menu) { 
                        active_menu_hover = -1;
                        needs_ui_redraw = 1; 
                        draw_editor(); 
                        last_mouse_x = mouse_x; 
                        last_mouse_y = mouse_y; 
                        continue; 
                    }
                }
                
                if (is_dragging || hbar_changed) { 
                    /* FIX: Force UI redraw so status bar overwrites track when mouse leaves! */
                    if (hbar_changed) needs_ui_redraw = 1; 
                    draw_editor(); 
                } else {
                    hide_mouse();
                    if (show_open_dialog || show_save_dialog) {
                        /* SELECTIVE REDRAW: Only update the buttons that actually changed! */
                        if (setting_hover_effects) {
                            if (previous_hid == 4 || previous_hid == 5 || previous_hid == 6 || (previous_hid >= 65 && previous_hid <= 72)) draw_file_dialog_element(previous_hid, 0);
                            if (hid == 4 || hid == 5 || hid == 6 || (hid >= 65 && hid <= 72)) draw_file_dialog_element(hid, 1);
                        }
                    }
                    else if (show_context_menu) { draw_context_menu(); }
                    else if (show_exit_dialog) { 
                        draw_exit_dialog(); 
                    }
                    else if (show_settings) { draw_settings(); }
                    else if (active_menu > 0) { draw_menu(); }
                    else {
                        /* SELECTIVE REDRAW: Main Top Menu & Scroll Arrows */
                        if (setting_hover_effects) {
                            if ((previous_hid >= 1 && previous_hid <= 4) || (previous_hid >= 60 && previous_hid <= 64)) draw_main_element(previous_hid, 0);
                            if ((hid >= 1 && hid <= 4) || (hid >= 60 && hid <= 64)) draw_main_element(hid, 1);
                        }
                    }
                    show_mouse();
                }
            }
            last_mouse_x = mouse_x; 
            last_mouse_y = mouse_y;
        }
        
        if (kbhit()) {
            int is_extended = 0;
            key = getch();
            if (alt_state) keys_pressed_during_alt = 1;

            if (key == 0) {
                is_extended = 1;
                key = getch();
                if (alt_state) keys_pressed_during_alt = 1;
            }

            if (show_settings) { 
                if (!is_extended && (key == 27 || key == 13)) { show_settings = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } 
                continue; 
            }
            if (show_open_dialog || show_save_dialog) { 
                if (!is_extended && key == 27) { show_open_dialog = 0; show_save_dialog = 0; active_tab_element = 0; active_input_field = 0; needs_ui_redraw = 1; draw_editor(); } 
                else if (!is_extended && key == 9) { /* Tab */
                    int shift = is_shift_pressed();
                    if (!shift) {
                        active_tab_element++;
                        if (active_tab_element > 10) active_tab_element = 1;
                    } else {
                        active_tab_element--;
                        if (active_tab_element < 1) active_tab_element = 10;
                    }
                    if (active_tab_element == 1) active_input_field = 1;
                    else if (active_tab_element == 2) active_input_field = 2;
                    else if (active_tab_element == 3) active_input_field = 3;
                    else active_input_field = 0;
                    
                    if (active_tab_element == 7) browser_selected_index = browser_scroll_offset;
                    else browser_selected_index = -1;
                    
                    needs_ui_redraw = 1; draw_editor();
                }
                else if (is_extended) {
                    if (active_input_field > 0) {
                        int* cur = (active_input_field == 1) ? &dialog_input_cursor : ((active_input_field == 2) ? &dialog_search_cursor : &dialog_address_cursor);
                        char* txt = (active_input_field == 1) ? dialog_input_text : ((active_input_field == 2) ? dialog_search_text : dialog_address_text);
                        if (key == 75 && *cur > 0) (*cur)--; /* Left Arrow */
                        else if (key == 77 && *cur < (int)strlen(txt)) (*cur)++; /* Right Arrow */
                        else if (key == 83 && *cur < (int)strlen(txt)) { /* Delete Key */
                            int len = (int)strlen(txt);
                            memmove(&txt[*cur], &txt[*cur + 1], len - *cur);
                            if (active_input_field == 2) filter_browser_files();
                        }
                    } 
                    
                    if (key == 72 || key == 80) { /* Up or Down Arrow */
                        int max_items = (SCREEN_COLS < 80) ? 8 : (SCREEN_ROWS - 16);
                        
                        /* FIX: Global File Browser Arrow Navigation! Also dynamically updates the File Name box! */
                        if (num_browser_files > 0) {
                            if (browser_selected_index < 0) browser_selected_index = 0;
                            else if (key == 72 && browser_selected_index > 0) browser_selected_index--;
                            else if (key == 80 && browser_selected_index < num_browser_files - 1) browser_selected_index++;
                            
                            if (browser_selected_index < browser_scroll_offset) browser_scroll_offset = browser_selected_index;
                            else if (browser_selected_index >= browser_scroll_offset + max_items) browser_scroll_offset = browser_selected_index - max_items + 1;
                            
                            /* Auto-fill the text box with the new selection */
                            if (!browser_files[browser_selected_index].is_dir) {
                                strcpy(dialog_input_text, browser_files[browser_selected_index].name);
                                dialog_input_cursor = (int)strlen(dialog_input_text);
                                active_input_field = 1;
                            }
                        }
                    }
                } else {
                    if (active_input_field > 0) {
                        int* cur = (active_input_field == 1) ? &dialog_input_cursor : ((active_input_field == 2) ? &dialog_search_cursor : &dialog_address_cursor);
                        char* txt = (active_input_field == 1) ? dialog_input_text : ((active_input_field == 2) ? dialog_search_text : dialog_address_text);
                        int max_len = (active_input_field == 3) ? 255 : ((active_input_field == 2) ? 128 : 255);

                        if (key == 8 && *cur > 0) { /* Backspace */
                            int len = (int)strlen(txt);
                            memmove(&txt[*cur - 1], &txt[*cur], len - *cur + 1);
                            (*cur)--;
                            if (active_input_field == 2) filter_browser_files();
                        } else if (key == 13) { /* Enter */
                            if (active_input_field == 1) {
                                handle_file_ok_click();
                            } else if (active_input_field == 3) {
                                struct ffblk test_dir;
                                char test_path[256];
                                strcpy(test_path, dialog_address_text);
                                if (test_path[strlen(test_path)-1] != '\\') strcat(test_path, "\\");
                                strcat(test_path, "*.*");
                                if (findfirst(test_path, &test_dir, FA_DIREC) == 0) {
                                    load_directory(dialog_address_text, 1);
                                    strcpy(dialog_status_msg, show_save_dialog ? "Ready to save file..." : "Ready to select file...");
                                } else {
                                    strcpy(dialog_status_msg, "Path does not exist");
                                }
                            }
                        } else if (key >= 32 && key <= 126 && (int)strlen(txt) < max_len - 1) {
                            int len = (int)strlen(txt);
                            memmove(&txt[*cur + 1], &txt[*cur], len - *cur + 1);
                            txt[*cur] = (char)key;
                            (*cur)++;
                            if (active_input_field == 2) filter_browser_files();
                        }
                    } else if (key == 13) { /* Enter on Tabbed Item */
                        if (active_tab_element == 4 && path_history_index > 0) {
                            path_history_index--; load_directory(path_history[path_history_index], 0);
                        } else if (active_tab_element == 5 && path_history_index < path_history_count - 1) {
                            path_history_index++; load_directory(path_history[path_history_index], 0);
                        } else if (active_tab_element == 6) {
                            char new_path[256]; int len = (int)strlen(current_browser_path);
                            if (len > 3) {
                                len -= 2; while (len > 0 && current_browser_path[len] != '\\') len--;
                                strncpy(new_path, current_browser_path, len + 1); new_path[len + 1] = '\0';
                                load_directory(new_path, 1);
                            }
                        } else if (active_tab_element == 7 && browser_selected_index >= 0) {
                            if (browser_files[browser_selected_index].is_dir) {
                                char new_path[256];
                                if (strcmp(browser_files[browser_selected_index].name, "..") == 0) {
                                    int len = (int)strlen(current_browser_path);
                                    if (len > 3) { len -= 2; while (len > 0 && current_browser_path[len] != '\\') len--; current_browser_path[len + 1] = '\0'; }
                                    strcpy(new_path, current_browser_path);
                                } else {
                                    strcpy(new_path, current_browser_path); strcat(new_path, browser_files[browser_selected_index].name);
                                }
                                load_directory(new_path, 1);
                            } else {
                                strcpy(dialog_input_text, browser_files[browser_selected_index].name);
                                dialog_input_cursor = (int)strlen(dialog_input_text);
                                active_input_field = 1; active_tab_element = 1;
                            }
                        } else if (active_tab_element == 8) handle_file_ok_click();
                        else if (active_tab_element == 9 || active_tab_element == 10) { show_open_dialog = 0; show_save_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; }
                    }
                }
                needs_ui_redraw = 1; draw_editor();
                continue; 
            }
            if (show_exit_dialog) { 
                if (!is_extended && key == 27) { show_exit_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } 
                else if (!is_extended && key == 9) { /* Tab */
                    int shift = is_shift_pressed();
                    if (!shift) {
                        active_tab_element++;
                        if (active_tab_element > 3) active_tab_element = 1;
                    } else {
                        active_tab_element--;
                        if (active_tab_element < 1) active_tab_element = 3;
                    }
                    needs_ui_redraw = 1; draw_editor();
                }
                else if (!is_extended && key == 13 && active_tab_element > 0) { /* Enter Key */
                    if (active_tab_element == 1) { /* Save */
                        if (strcmp(current_filename, "Untitled.txt") == 0) { 
                            show_exit_dialog = 0; show_save_dialog = 1; active_tab_element = 1; active_input_field = 1;
                            strcpy(dialog_status_msg, "Ready to save file..."); 
                            load_directory(current_browser_path, 1); 
                        } else { 
                            char filepath[256]; 
                            strcpy(filepath, current_browser_path); strcat(filepath, current_filename); 
                            save_document(filepath);
                            clean_exit();
                        } 
                    }
                    else if (active_tab_element == 2) clean_exit(); /* Don't Save */
                    else if (active_tab_element == 3) { show_exit_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } /* Cancel */
                }
                continue; 
            }
            
            /* --- Find & Replace Keyboard Logic --- */
            if (show_find_dialog && active_input_field >= 4) {
                if (!is_extended && (key == 27 || key == 6)) { /* Escape or Ctrl+F (Toggle Off) */
                    show_find_dialog = 0; active_input_field = 0; needs_ui_redraw = 1; draw_editor(); 
                }
                else if (!is_extended && key == 9) { /* Tab Key flips between Find and Replace */
                    active_input_field = (active_input_field == 4) ? 5 : 4;
                    needs_ui_redraw = 1; draw_editor();
                }
                else if (!is_extended && key == 13) { /* Enter Key triggers the primary action */
                    if (active_input_field == 4) execute_find_replace(1);
                    else if (active_input_field == 5) execute_find_replace(2);
                }
                else if (is_extended) {
                    int* cur = (active_input_field == 4) ? &find_cursor : &replace_cursor;
                    char* txt = (active_input_field == 4) ? find_text : replace_text;
                    
                    if (key == 75 && *cur > 0) (*cur)--; /* Left Arrow */
                    else if (key == 77 && *cur < (int)strlen(txt)) (*cur)++; /* Right Arrow */
                    else if (key == 83 && *cur < (int)strlen(txt)) { /* Delete Key */
                        int len = (int)strlen(txt);
                        memmove(&txt[*cur], &txt[*cur + 1], len - *cur);
                    }
                    needs_ui_redraw = 1; draw_editor();
                } 
                else {
                    int* cur = (active_input_field == 4) ? &find_cursor : &replace_cursor;
                    char* txt = (active_input_field == 4) ? find_text : replace_text;
                    int max_len = 127;
                    
                    if (key == 8 && *cur > 0) { /* Backspace */
                        int len = (int)strlen(txt);
                        memmove(&txt[*cur - 1], &txt[*cur], len - *cur + 1);
                        (*cur)--;
                    } 
                    else if (key >= 32 && key <= 126 && (int)strlen(txt) < max_len - 1) {
                        int len = (int)strlen(txt);
                        memmove(&txt[*cur + 1], &txt[*cur], len - *cur + 1);
                        txt[*cur] = (char)key;
                        (*cur)++;
                    }
                    needs_ui_redraw = 1; draw_editor();
                }
                continue; 
            }
            
            /* Global Ctrl and Alt Shortcuts */
            if (!show_open_dialog && !show_save_dialog && !show_exit_dialog) {
                if (!is_extended) {
                    if (key == 1) { execute_menu_action(2, 11); draw_editor(); continue; } /* Ctrl+A (Select All) */
                    if (key == 3) { execute_menu_action(2, 7); draw_editor(); continue; }  /* Ctrl+C (Copy) */
                    if (key == 14) { execute_menu_action(1, 3); draw_editor(); continue; } /* Ctrl+N (New) */
                    if (key == 15) { execute_menu_action(1, 4); draw_editor(); continue; } /* Ctrl+O (Open) */
                    if (key == 19) { execute_menu_action(1, 5); draw_editor(); continue; } /* Ctrl+S (Save) */
                    if (key == 22) { execute_menu_action(2, 8); draw_editor(); continue; } /* Ctrl+V (Paste) */
                    if (key == 24) { execute_menu_action(2, 6); draw_editor(); continue; } /* Ctrl+X (Cut) */
                    if (key == 25) { execute_menu_action(2, 4); draw_editor(); continue; } /* Ctrl+Y (Redo) */
                    if (key == 26) { execute_menu_action(2, 3); draw_editor(); continue; } /* Ctrl+Z (Undo) */
                } else if (is_extended) {
                    if (key == 33) { active_menu = 1; active_menu_hover = 0; show_context_menu = 0; needs_ui_redraw = 1; draw_editor(); continue; } /* Alt+F */
                    if (key == 18) { active_menu = 2; active_menu_hover = 0; show_context_menu = 0; needs_ui_redraw = 1; draw_editor(); continue; } /* Alt+E */
                    if (key == 47) { active_menu = 3; active_menu_hover = 0; show_context_menu = 0; needs_ui_redraw = 1; draw_editor(); continue; } /* Alt+V */
                }
            }
            
            /* Arrow Key Menu Navigation */
            if (active_menu > 0 || show_context_menu) { 
                if (!is_extended && key == 27) { active_menu = 0; active_menu_hover = -1; show_context_menu = 0; needs_ui_redraw = 1; draw_editor(); } 
                else if (!is_extended && key == 13 && active_menu_hover >= 0 && !show_context_menu) { /* Enter Key */
                    int action_y = 0;
                    if (active_menu == 1) {
                        if (active_menu_hover == 0) action_y = 3;
                        else if (active_menu_hover == 1) action_y = 4;
                        else if (active_menu_hover == 2) action_y = 5;
                        else if (active_menu_hover == 3) action_y = 6;
                        else if (active_menu_hover == 4) action_y = 8;
                        else if (active_menu_hover == 5) action_y = 10;
                        else if (active_menu_hover == 6) action_y = 12;
                    } else if (active_menu == 2) {
                        if (active_menu_hover == 0) action_y = 3;
                        else if (active_menu_hover == 1) action_y = 4;
                        else if (active_menu_hover == 2) action_y = 6;
                        else if (active_menu_hover == 3) action_y = 7;
                        else if (active_menu_hover == 4) action_y = 8;
                        else if (active_menu_hover == 5) action_y = 9;
                        else if (active_menu_hover == 6) action_y = 11;
                        else if (active_menu_hover == 7) action_y = 12;
                    } else if (active_menu == 3) {
                        if (active_menu_hover == 0) action_y = 3;
                        else if (active_menu_hover == 1) action_y = 6;
                        else if (active_menu_hover == 2) action_y = 9;
                        else if (active_menu_hover == 3) action_y = 11;
                    }
                    if (action_y > 0) {
                        execute_menu_action(active_menu, action_y);
                        draw_editor();
                    }
                }
                else if (is_extended && active_menu > 0 && !show_context_menu) {
                    if (key == 75) { /* Left */
                        active_menu--; 
                        if (active_menu < 1) active_menu = 3; 
                        active_menu_hover = 0; 
                        needs_ui_redraw = 1; draw_editor(); 
                    } 
                    else if (key == 77) { /* Right */
                        active_menu++; 
                        if (active_menu > 3) active_menu = 1; 
                        active_menu_hover = 0; 
                        needs_ui_redraw = 1; draw_editor(); 
                    }
                    else if (key == 72) { /* Up */
                        if (active_menu_hover > 0) active_menu_hover--;
                        else active_menu_hover = (active_menu == 1 ? 6 : (active_menu == 2 ? 7 : 3));
                        needs_ui_redraw = 1; draw_editor();
                    }
                    else if (key == 80) { /* Down */
                        int max_items = (active_menu == 1 ? 6 : (active_menu == 2 ? 7 : 3));
                        if (active_menu_hover < max_items) active_menu_hover++;
                        else active_menu_hover = 0;
                        needs_ui_redraw = 1; draw_editor();
                    }
                }
                continue; 
            }
            
            if (is_extended) {
                /* SHIFT KEY SELECTION MECHANIC */
                if (key == 72 || key == 80 || key == 75 || key == 77 || key == 71 || key == 79 || key == 119 || key == 117) {
                    int shift = is_shift_pressed();
                    if (shift && !has_selection) { 
                        sel_start_x = cursor_x; 
                        sel_start_y = cursor_y; 
                        has_selection = 1; 
                    } else if (!shift) { 
                        has_selection = 0; 
                    }
                    last_was_typing = 0;
                    force_cursor_snap = 1;
                }
                
                if (key == 72) { /* UP ARROW */
                    if (setting_word_wrap && vis_cy > 0) {
                        cursor_y = vis_map[vis_cy - 1].log_y;
                        cursor_x = vis_map[vis_cy - 1].log_x + (vis_cx < vis_map[vis_cy - 1].len ? vis_cx : vis_map[vis_cy - 1].len);
                    } else if (!setting_word_wrap && cursor_y > 0) { 
                        cursor_y--; 
                        long llen = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]); 
                        if (cursor_x > llen) cursor_x = llen; 
                    }
                    force_cursor_snap = 1; needs_ui_redraw = 1;
                }
                else if (key == 80) { /* DOWN ARROW */
                    if (setting_word_wrap && vis_cy < vis_lines - 1) {
                        cursor_y = vis_map[vis_cy + 1].log_y;
                        cursor_x = vis_map[vis_cy + 1].log_x + (vis_cx < vis_map[vis_cy + 1].len ? vis_cx : vis_map[vis_cy + 1].len);
                    } else if (!setting_word_wrap) {
                        int m_lines = setting_disk_stream ? total_stream_lines : total_lines;
                        if (cursor_y < m_lines - 1) { 
                            cursor_y++; 
                            long llen = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]); 
                            if (cursor_x > llen) cursor_x = llen; 
                        }
                    }
                    force_cursor_snap = 1; needs_ui_redraw = 1;
                }
                else if (key == 75) { /* LEFT ARROW */
                    if (cursor_x > 0) cursor_x--; 
                    else if (cursor_y > 0) { 
                        cursor_y--; 
                        cursor_x = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]); 
                    } 
                } else if (key == 77) { /* RIGHT ARROW */
                    long c_len = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]);
                    if (cursor_x < c_len) cursor_x++; 
                    else if (cursor_y < (setting_disk_stream ? total_stream_lines : total_lines) - 1) { 
                        cursor_y++; cursor_x = 0; 
                    } 
                } else if (key == 73) { /* PAGE UP */
                    int top_y = show_find_dialog ? 10 : 3;
                    int vis_h = SCREEN_ROWS - 3 - top_y;
                    if (setting_word_wrap) {
                        long target_v = vis_cy - vis_h;
                        if (target_v < 0) target_v = 0;
                        if (target_v < vis_lines) {
                            cursor_y = vis_map[target_v].log_y;
                            cursor_x = vis_map[target_v].log_x + (vis_cx < vis_map[target_v].len ? vis_cx : vis_map[target_v].len);
                        }
                    } else {
                        cursor_y -= vis_h;
                        if (cursor_y < 0) cursor_y = 0;
                        long llen = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]); 
                        if (cursor_x > llen) cursor_x = llen; 
                    }
                    last_was_typing = 0; needs_recalc = 1; force_cursor_snap = 1; needs_ui_redraw = 1;
                } else if (key == 81) { /* PAGE DOWN */
                    int top_y = show_find_dialog ? 10 : 3;
                    int vis_h = SCREEN_ROWS - 3 - top_y;
                    if (setting_word_wrap) {
                        long target_v = vis_cy + vis_h;
                        if (target_v >= vis_lines) target_v = vis_lines - 1;
                        if (target_v < 0) target_v = 0;
                        if (target_v < vis_lines) {
                            cursor_y = vis_map[target_v].log_y;
                            cursor_x = vis_map[target_v].log_x + (vis_cx < vis_map[target_v].len ? vis_cx : vis_map[target_v].len);
                        }
                    } else {
                        long effective_lines = setting_disk_stream ? total_stream_lines : total_lines;
                        cursor_y += vis_h;
                        if (cursor_y >= effective_lines) cursor_y = effective_lines - 1;
                        long llen = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]); 
                        if (cursor_x > llen) cursor_x = llen; 
                    }
                    last_was_typing = 0; needs_recalc = 1; force_cursor_snap = 1; needs_ui_redraw = 1;
                } else if (key == 71) { /* HOME */
                    if (setting_word_wrap) cursor_x = vis_map[vis_cy].log_x;
                    else cursor_x = 0;
                    force_cursor_snap = 1; needs_ui_redraw = 1; 
                } else if (key == 79) { /* END */
                    if (setting_word_wrap) cursor_x = vis_map[vis_cy].log_x + vis_map[vis_cy].len;
                    else {
                        long llen = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]);
                        cursor_x = llen;
                    }
                    force_cursor_snap = 1; needs_ui_redraw = 1; 
                } else if (key == 119) { 
                    int view_w = setting_v_scrollbar ? 76 : 78;
                    scroll_x -= 15; 
                    if (scroll_x < 0) scroll_x = 0; 
                    if (cursor_x > scroll_x + view_w - 1) cursor_x = scroll_x + view_w - 1; 
                    force_cursor_snap = 1; 
                } else if (key == 117) { 
                    scroll_x += 15; 
                    if (cursor_x < scroll_x) cursor_x = scroll_x; 
                    force_cursor_snap = 1; 
                } else if (key == 83) { /* Delete Key */
                    if (has_selection) {
                        save_state(); last_was_typing = 0;
                        delete_selection();
                    } else {
                        save_state(); last_was_typing = 0;
                        
                        if (setting_disk_stream) {
                            long target_global = stream_lines[cursor_y].global_offset + cursor_x;
                            long total_chars = 0;
                            int p_idx; for (p_idx = 0; p_idx < num_pieces; p_idx++) total_chars += piece_table[p_idx].length;
                            
                            if (target_global < total_chars) {
                                delete_from_stream(target_global);
                                is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
                            }
                        } else if (cursor_x < (int)strlen(text_buffer[cursor_y])) {
                            char* line = text_buffer[cursor_y];
                            int len = (int)strlen(line);
                            memmove(&line[cursor_x], &line[cursor_x + 1], len - cursor_x);
                            is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
                        } else if (cursor_y < total_lines - 1) {
                            char* line = text_buffer[cursor_y];
                            char* next_line = text_buffer[cursor_y + 1];
                            int len1 = (int)strlen(line);
                            int len2 = (int)strlen(next_line);
                            int i;
                            text_buffer[cursor_y] = (char*)realloc(line, (size_t)(len1 + len2 + 1));
                            strcat(text_buffer[cursor_y], next_line);
                            free(next_line);
                            for (i = cursor_y + 1; i < total_lines - 1; i++) text_buffer[i] = text_buffer[i + 1];
                            total_lines--;
                            is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
                        }
                    }
                }
                
                long m_len = setting_disk_stream ? get_stream_logical_len(cursor_y) : (long)strlen(text_buffer[cursor_y]); 
                if (cursor_x > m_len) cursor_x = m_len;
            } else {
                if (key == 27) { 
                    if (is_document_dirty) { show_exit_dialog = 1; active_tab_element = 1; } else running = 0; 
                } else if (key == 13) { 
                    save_state(); last_was_typing = 0;
                    if (has_selection) delete_selection(); 
                    insert_newline(); 
                } else if (key == 8) { 
                    save_state(); last_was_typing = 0;
                    if (has_selection) delete_selection(); else backspace(); 
                } else if (key >= 32 && key <= 126) { 
                    if (!last_was_typing || key == ' ') save_state();
                    last_was_typing = 1;
                    if (has_selection) { delete_selection(); last_was_typing = 0; }
                    insert_char((char)key); 
                }
            }
            draw_editor();
        }
        
        
        if (mouse_left) {
            handle_mouse_clicks(last_mouse_left);
        } else is_dragging = 0;
        
        last_mouse_left = mouse_left;
        last_mouse_right = mouse_right;
    }
    
    clear_entire_history();
    for (r = 0; r < total_lines; r++) free(text_buffer[r]);
    free(text_buffer);
    free(vis_map);
    if (internal_clipboard) free(internal_clipboard);

    hide_mouse();
    show_hardware_cursor();
    
    /* Cleanly restore DOS screen before quitting! */
    for (r = 0; r < SCREEN_ROWS; r++) { 
        for (c = 0; c < SCREEN_COLS; c++) {
            set_char(r, c, ' ', make_color(0, 7)); 
        }
    }
    
    return 0;
}