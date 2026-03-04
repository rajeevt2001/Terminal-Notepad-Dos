#include <dos.h>
#include <dir.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --- Macros & Constants --- */
#define bool int
#define MOUSE_INT 0x33
#define INIT_MOUSE 0x00
#define SHOW_MOUSE 0x01
#define HIDE_MOUSE 0x02
#define GET_MOUSE_STATUS 0x03
#define VIDEO_ADDRESS 0xB800

#define MAX_COLS 80
#define MAX_ROWS 25
#define INITIAL_LINES 100

#define CURSOR_CHAR 0x0D

/* --- Global Editor State --- */
char** text_buffer;
int total_lines = 0;
int buffer_capacity = INITIAL_LINES;

int cursor_x = 0; /* Logical X (0-based) */
int cursor_y = 0; /* Logical Y (0-based) */
int scroll_x = 0; /* Horizontal Scroll Offset */
int scroll_y = 0; /* Vertical Scroll Offset */

int mouse_x = 0;
int mouse_y = 0;
int mouse_left = 0;
int mouse_right = 0;

/* --- Advanced Features State --- */
int active_menu = 0;
int active_menu_hover = -1; /* Tracks keyboard selection index in menus */
int active_tab_element = 0; /* Tracks keyboard Tab navigation in dialogs */
int show_context_menu = 0;
int ctx_x = 0, ctx_y = 0;

int show_exit_dialog = 0;
int is_document_dirty = 0;
int last_was_typing = 0; /* For smart Undo grouping */
char current_filename[256] = "Untitled.txt";
char exe_dir[256] = ""; /* Stores the directory where the .exe resides */

int has_selection = 0;
int is_dragging = 0;
int sel_start_x = 0;
int sel_start_y = 0;

char* internal_clipboard = NULL;

/* --- Settings State --- */
int show_settings = 0;
int current_settings_tab = 1;
int setting_hover_effects = 1; /* 0 = Off, 1 = On */
int setting_ansi_mode = 1;
int setting_color = 1;
int setting_bg_color = 0;
int setting_fg_color = 7;
int setting_sel_color = 1;
int setting_crit_color = 4;
int use_mouse = 1;

/* --- Dynamic Layout Engine (Word Wrap) --- */
int setting_word_wrap = 1;
int setting_respect_words = 1;
int setting_h_scrollbar = 0;
int setting_v_scrollbar = 1;
int needs_recalc = 1;
int needs_ui_redraw = 1;
int last_hover_id = 0;
int force_cursor_snap = 1; /* Decoupled Camera Engine */

typedef struct {
    int log_y;
    int log_x;
    int len;
} VisualLine;

VisualLine* vis_map = NULL;
int vis_lines = 0;
int vis_cap = 0;
int vis_cx = 0, vis_cy = 0;
int max_line_len = 0; /* Longest line length for Horizontal Scroll */

/* --- File Browser State --- */
typedef struct {
    char name[128];
    int is_dir;
    long size;
} FileItem;

FileItem all_browser_files[100];
int num_all_browser_files = 0;
FileItem browser_files[100];
int num_browser_files = 0;
int browser_scroll_offset = 0;
int browser_selected_index = -1;

char path_history[20][256];
int path_history_count = 0;
int path_history_index = -1;

int show_open_dialog = 0;
int show_save_dialog = 0;
char dialog_input_text[256] = "";
char dialog_search_text[256] = "";
char dialog_address_text[256] = "C:\\";
char current_browser_path[256] = "C:\\";
char dialog_status_msg[256] = "Ready...";
int active_input_field = 0;
int dialog_input_cursor = 0;
int dialog_search_cursor = 0;
int dialog_address_cursor = 3;

/* --- Undo / Redo Engine (10 Levels Max) --- */
#define MAX_UNDO_LEVELS 10

typedef struct {
    char** lines;
    int num_lines;
    int cur_x;
    int cur_y;
} EditorSnapshot;

EditorSnapshot undo_stack[MAX_UNDO_LEVELS];
EditorSnapshot redo_stack[MAX_UNDO_LEVELS];
int undo_count = 0;
int redo_count = 0;

/* --- Custom VGA Font Pointer --- */
unsigned char mouse_arrow[16] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xFF, 0xF8, 0xEC, 0xCC, 0x86, 0x02, 0x00, 0x00
};

/* --- Prototypes for TC --- */
void draw_editor(void);
void build_visual_map(void);
void draw_settings(void);
void draw_file_dialog(int is_save);
void draw_main_hovers(void);
void draw_context_menu(void);
void execute_menu_action(int menu_id, int action_y);
void handle_file_ok_click(void);
void load_directory(const char* path, int update_history);
void filter_browser_files(void);

void load_cursor_glyph() {
    struct REGPACK reg;
    reg.r_ax = 0x1100;
    reg.r_bx = 0x1000;
    reg.r_cx = 1;
    reg.r_dx = CURSOR_CHAR;
    reg.r_es = FP_SEG(mouse_arrow);
    reg.r_bp = FP_OFF(mouse_arrow);
    intr(0x10, &reg);
}

/* --- BIOS Hardware Polling --- */
int is_shift_pressed() {
    unsigned char far* kbd_flags = (unsigned char far*)MK_FP(0x0040, 0x0017);
    return (*kbd_flags) & 0x03; /* 0x01 = Right Shift, 0x02 = Left Shift */
}

int is_alt_pressed() {
    unsigned char far* kbd_flags = (unsigned char far*)MK_FP(0x0040, 0x0017);
    return (*kbd_flags) & 0x08; /* 0x08 = Alt Key */
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

/* --- Configuration System --- */
void load_config() {
    char cfg_path[256];
    FILE* f;
    
    strcpy(cfg_path, exe_dir);
    strcat(cfg_path, "config.txt");
    
    f = fopen(cfg_path, "r");
    if (f) {
        char line[128];
        int val;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "setting_hover_effects=%d", &val) == 1) setting_hover_effects = val;
            else if (sscanf(line, "setting_ansi_mode=%d", &val) == 1) setting_ansi_mode = val;
            else if (sscanf(line, "setting_color=%d", &val) == 1) setting_color = val;
            else if (sscanf(line, "setting_bg_color=%d", &val) == 1) setting_bg_color = val;
            else if (sscanf(line, "setting_fg_color=%d", &val) == 1) setting_fg_color = val;
            else if (sscanf(line, "setting_sel_color=%d", &val) == 1) setting_sel_color = val;
            else if (sscanf(line, "setting_crit_color=%d", &val) == 1) setting_crit_color = val;
            else if (sscanf(line, "word_warp=%d", &val) == 1) setting_word_wrap = val;
            else if (sscanf(line, "respect_whole_words=%d", &val) == 1) setting_respect_words = val;
            else if (sscanf(line, "h_scrollBar=%d", &val) == 1) setting_h_scrollbar = val;
            else if (sscanf(line, "v_scrollBar=%d", &val) == 1) setting_v_scrollbar = val;
        }
        fclose(f);
    }
}

void save_config() {
    char cfg_path[256];
    FILE* f;
    
    strcpy(cfg_path, exe_dir);
    strcat(cfg_path, "config.txt");
    
    f = fopen(cfg_path, "w");
    if (f) {
        fprintf(f, "word_warp=%d\n", setting_word_wrap);
        fprintf(f, "v_scrollBar=%d\n", setting_v_scrollbar);
        fprintf(f, "h_scrollBar=%d\n", setting_h_scrollbar);
        fprintf(f, "respect_whole_words=%d\n", setting_respect_words);
        fprintf(f, "setting_hover_effects=%d\n", setting_hover_effects);
        fprintf(f, "setting_color=%d\n", setting_color);
        fprintf(f, "setting_ansi_mode=%d\n", setting_ansi_mode);
        fprintf(f, "setting_bg_color=%d\n", setting_bg_color);
        fprintf(f, "setting_fg_color=%d\n", setting_fg_color);
        fprintf(f, "setting_sel_color=%d\n", setting_sel_color);
        fprintf(f, "setting_crit_color=%d\n", setting_crit_color);
        fclose(f);
    }
}

/* --- Time Machine (Undo/Redo) Core --- */
void free_snapshot(EditorSnapshot* snap) {
    int i;
    for (i = 0; i < snap->num_lines; i++) {
        free(snap->lines[i]);
    }
    free(snap->lines);
    snap->num_lines = 0;
}

EditorSnapshot create_snapshot() {
    EditorSnapshot snap;
    int i;
    snap.cur_x = cursor_x;
    snap.cur_y = cursor_y;
    snap.num_lines = total_lines;
    snap.lines = (char**)malloc(total_lines * sizeof(char*));
    for (i = 0; i < total_lines; i++) {
        snap.lines[i] = (char*)malloc(strlen(text_buffer[i]) + 1);
        strcpy(snap.lines[i], text_buffer[i]);
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

/* --- The 80x25 UI Layout Template --- */
const char* ui_layout[25] = {
    "[******^******^******^*****************************************************^***]",
    "| File | Edit | View |                                                     | X |",
    "@******_******_******_*****************************************************_*^*$",
    "|                                                                            |1|",
    "|                                                                            @*$",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            | |",
    "|                                                                            @*$",
    "|                                                                            |2|",
    "@****************************************************************************+*$",
    "|                                                                            |3|",
    "{****************************************************************************_*}"
};

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
    unsigned char fg = 7;
    if (bg == 7) fg = 0; /* Enforce Contrast */
    return make_color(bg, fg);
}

unsigned char get_cr_color() {
    if (!setting_color) {
        return (setting_bg_color == 7) ? make_color(0, 7) : make_color(7, 0);
    }
    unsigned char bg = (unsigned char)setting_crit_color;
    unsigned char fg = 7;
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
    unsigned char far* vram = (unsigned char far*)MK_FP(VIDEO_ADDRESS, 0x0000);
    int offset = (r * 80 + c) * 2;
    vram[offset] = ch; 
    vram[offset + 1] = color;
}

void print_str(int r, int c, const char* str, unsigned char color) {
    int i = 0; 
    while (str[i] != '\0' && (c + i) < 80) { 
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
            case '|': return 179; 
            case '*': return 196; 
            case '[': return 218; 
            case ']': return 191;
            case '{': return 192; 
            case '}': return 217; 
            case '^': return 194; 
            case '_': return 193;
            case '@': return 195; 
            case '$': return 180; 
            case '+': return 197; 
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
    while (str[i] != '\0' && (c + i) < 80) { 
        set_char(r, c + i, decode_char(str[i]), color); 
        i++; 
    }
}

/* --- Mouse Driver --- */
void show_mouse() { 
    union REGS regs; 
    regs.x.ax = SHOW_MOUSE; 
    int86(MOUSE_INT, &regs, &regs); 
}

void hide_mouse() { 
    union REGS regs; 
    regs.x.ax = HIDE_MOUSE; 
    int86(MOUSE_INT, &regs, &regs); 
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
    return 0;
}

void update_mouse() {
    union REGS regs; 
    regs.x.ax = GET_MOUSE_STATUS; 
    int86(MOUSE_INT, &regs, &regs);
    mouse_x = regs.x.cx / 8; 
    mouse_y = regs.x.dx / 8; 
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
int is_in_selection(int y, int x) {
    int sy = sel_start_y, sx = sel_start_x, ey = cursor_y, ex = cursor_x, temp;
    if (!has_selection) return 0;
    if (sy > ey || (sy == ey && sx > ex)) { 
        temp = sy; sy = ey; ey = temp; 
        temp = sx; sx = ex; ex = temp; 
    }
    if (y < sy || y > ey) return 0;
    if (sy == ey) return (x >= sx && x < ex);
    if (y == sy) return x >= sx;
    if (y == ey) return x < ex;
    return 1;
}

void delete_selection() {
    int sy = sel_start_y, sx = sel_start_x, ey = cursor_y, ex = cursor_x;
    int temp, i, lines_removed; char *first, *last, *line;
    if (!has_selection) return;
    if (sy > ey || (sy == ey && sx > ex)) { 
        temp = sy; sy = ey; ey = temp; 
        temp = sx; sx = ex; ex = temp; 
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
        lines_removed = ey - sy;
        for(i = ey + 1; i < total_lines; i++) text_buffer[i - lines_removed] = text_buffer[i];
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
        temp = sx; sx = ex; ex = temp; 
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
    char* line = text_buffer[cursor_y]; int len = (int)strlen(line);
    line = (char*)realloc(line, len + 2);
    memmove(&line[cursor_x + 1], &line[cursor_x], len - cursor_x + 1); 
    line[cursor_x] = c; text_buffer[cursor_y] = line;
    cursor_x++; is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
}

void backspace() {
    char* line = text_buffer[cursor_y];
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
    if (total_lines >= buffer_capacity) { 
        buffer_capacity *= 2; 
        text_buffer = (char**)realloc(text_buffer, buffer_capacity * sizeof(char*)); 
    }
    for (i = total_lines; i > cursor_y + 1; i--) text_buffer[i] = text_buffer[i - 1];
    
    current_line = text_buffer[cursor_y]; 
    new_line = (char*)malloc(strlen(&current_line[cursor_x]) + 1);
    strcpy(new_line, &current_line[cursor_x]); 
    current_line[cursor_x] = '\0';
    text_buffer[cursor_y] = (char*)realloc(current_line, (size_t)(cursor_x + 1));
    text_buffer[cursor_y + 1] = new_line; 
    total_lines++; cursor_y++; cursor_x = 0; 
    is_document_dirty = 1; needs_recalc = 1; force_cursor_snap = 1;
}

void paste_clipboard() {
    int i, len;
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

/* --- Dynamic Word Wrap Engine --- */
void build_visual_map() {
    int i, j, cp, sp, len, max_c = setting_v_scrollbar ? 76 : 78; 
    vis_lines = 0; max_line_len = 0;
    
    for (i = 0; i < total_lines; i++) {
        len = (int)strlen(text_buffer[i]);
        if (len > max_line_len) max_line_len = len;
        
        if (!setting_word_wrap || len <= max_c) {
            if (vis_lines >= vis_cap) { 
                vis_cap *= 2; 
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
                            if (text_buffer[i][j] == ' ') { ls = j - cp; break; } 
                        }
                        if (ls > 0) sp = ls;
                    }
                }
                if (vis_lines >= vis_cap) { 
                    vis_cap *= 2; 
                    vis_map = (VisualLine*)realloc(vis_map, vis_cap * sizeof(VisualLine)); 
                }
                vis_map[vis_lines].log_y = i; vis_map[vis_lines].log_x = cp; 
                vis_map[vis_lines].len = sp; vis_lines++;
                cp += sp; 
                if (setting_respect_words && cp < len && text_buffer[i][cp] == ' ') cp++;
            }
        }
    }
}

void update_vis_cursor() {
    int i; vis_cx = 0; vis_cy = 0;
    if (!setting_word_wrap) { vis_cy = cursor_y; vis_cx = cursor_x; }
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
        if (my == 3) { 
            if (mx >= 25 && mx <= 33) return 40; 
            if (mx >= 35 && mx <= 44) return 41; 
            if (mx >= 46 && mx <= 52) return 42; 
            if (mx >= 72 && mx <= 78) return 43; 
        }
        if (current_settings_tab == 1) {
            if (my == 7 && mx >= 4 && mx <= 20) return 44;
            if (my == 8 && mx >= 4 && mx <= 20) return 45;
            if (my == 11 && mx >= 2 && mx <= 20) return 47;
            if (my == 14 && mx >= 4 && mx <= 30) return 48;
            if (my == 15 && mx >= 4 && mx <= 30) return 49;
            if (my >= 18 && my <= 21) {
                if (!setting_color && my >= 20) {}
                else {
                    int base = 100 + (my - 18) * 10;
                    if (mx >= 27 && mx <= 35) return base + 0;
                    if (mx >= 38 && mx <= 46) return base + 1;
                    if (setting_color) {
                        if (mx >= 49 && mx <= 56) return base + 2;
                        if (mx >= 59 && mx <= 67) return base + 3;
                        if (mx >= 70 && mx <= 76) return base + 4;
                    }
                }
            }
        }
        return 0;
    }
    if (show_open_dialog || show_save_dialog) {
        if (my == 1 && mx >= 76 && mx <= 78) return 4;
        if (my == 20 && mx >= 61 && mx <= 68) return 5;
        if (my == 20 && mx >= 70 && mx <= 77) return 6;
        if (my == 7 && mx == 76) return 65;
        if (my == 17 && mx == 76) return 66;
        if (my == 4) {
            if (mx >= 2 && mx <= 5) return 70;
            if (mx >= 7 && mx <= 10) return 71;
            if (mx >= 12 && mx <= 14) return 72;
        }
        return 0;
    }
    if (show_exit_dialog) {
        if (my == 14) { 
            if (mx >= 22 && mx <= 29) return 80; 
            if (mx >= 33 && mx <= 46) return 81; 
            if (mx >= 50 && mx <= 57) return 82; 
        }
        return 0;
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
        if (mx >= 76 && mx <= 78) return 4; 
    }
    if (my == 23 && mx == 78) return 60;
    if (setting_v_scrollbar && my == 3 && mx == 78) return 61;
    if (setting_v_scrollbar && my == 21 && mx == 78) return 62;
    if (setting_h_scrollbar && my == 23 && mx == 1) return 63;
    if (setting_h_scrollbar && my == 23 && mx == (setting_v_scrollbar ? 76 : 78)) return 64;
    if (setting_h_scrollbar && my == 23 && mx >= 3 && mx <= (setting_v_scrollbar ? 74 : 76)) return 200;
    return 0;
}

void print_theme_line(int row, const char* label, int val, int color_enabled, unsigned char bg_color, unsigned char hl_color, int mx, int my, int hover_fx) {
    char buf[80]; 
    char cb = setting_ansi_mode == 1 ? '\xFB' : 'V'; 
    char rb = setting_ansi_mode == 1 ? '\x07' : '*';
    int tab_base = 100 + (row - 18) * 10;
    
    sprintf(buf, "  %s - ", label); 
    print_str(row, 2, buf, bg_color);
    
    /* The keyboard tab logic (active_tab_element == tab_base + X) is now completely independent of hover_fx */
    unsigned char c_blk = ((hover_fx && my == row && mx >= 27 && mx <= 35) || (active_tab_element == tab_base + 0)) ? hl_color : bg_color;
    sprintf(buf, "Black (%c)", val == 0 ? rb : ' ');
    print_str(row, 27, buf, c_blk);
    print_str(row, 36, ", ", bg_color);
    
    unsigned char c_wht = ((hover_fx && my == row && mx >= 38 && mx <= 46) || (active_tab_element == tab_base + 1)) ? hl_color : bg_color;
    sprintf(buf, "White (%c)", val == 7 ? rb : ' ');
    print_str(row, 38, buf, c_wht);
    
    if (color_enabled) { 
        print_str(row, 47, ", ", bg_color);
        unsigned char c_blu = ((hover_fx && my == row && mx >= 49 && mx <= 56) || (active_tab_element == tab_base + 2)) ? hl_color : bg_color;
        sprintf(buf, "Blue [%c]", (val != 0 && val != 7 && (val & 1)) ? cb : ' '); 
        print_str(row, 49, buf, c_blu);
        
        print_str(row, 57, ", ", bg_color);
        unsigned char c_grn = ((hover_fx && my == row && mx >= 59 && mx <= 67) || (active_tab_element == tab_base + 3)) ? hl_color : bg_color;
        sprintf(buf, "Green [%c]", (val != 0 && val != 7 && (val & 2)) ? cb : ' '); 
        print_str(row, 59, buf, c_grn);
        
        print_str(row, 68, ", ", bg_color);
        unsigned char c_red = ((hover_fx && my == row && mx >= 70 && mx <= 76) || (active_tab_element == tab_base + 4)) ? hl_color : bg_color;
        sprintf(buf, "Red [%c]", (val != 0 && val != 7 && (val & 4)) ? cb : ' '); 
        print_str(row, 70, buf, c_red);
    }
}

void draw_main_hovers() {
    unsigned char bg_color = get_bg_color(), hl_color = get_hl_color(), cr_color = get_cr_color();
    unsigned char dim_color = get_dim_color();
    unsigned char base_ui_color = show_exit_dialog ? dim_color : bg_color;
    char title_buf[60]; int title_len;
    
    print_mapped_str(1, 0, "| File | Edit | View |", base_ui_color);
    
    strcpy(title_buf, is_document_dirty ? "*" : ""); 
    strncat(title_buf, current_filename, 48); 
    title_len = (int)strlen(title_buf);
    
    if (title_len > 51) {
        strcpy(&title_buf[48], "..."); 
    } else { 
        while (title_len < 51) {
            title_buf[title_len++] = ' '; 
        }
        title_buf[51] = '\0'; 
    }
    print_str(1, 23, title_buf, base_ui_color);
    
    /* Ensure default states are drawn first so hovers don't get "stuck" */
    print_str(1, 76, " X ", base_ui_color);
    set_char(23, 78, decode_char('3'), base_ui_color);
    if (setting_v_scrollbar) {
        set_char(3, 78, decode_char('1'), base_ui_color);
        set_char(21, 78, decode_char('2'), base_ui_color);
    }
    if (setting_h_scrollbar) {
        set_char(23, 1, decode_char('4'), base_ui_color);
        set_char(23, setting_v_scrollbar ? 76 : 78, decode_char('5'), base_ui_color);
    }
    
    if (!show_settings && !show_open_dialog && !show_save_dialog && !show_exit_dialog) {
        if (active_menu == 1) print_str(1, 1, " File ", hl_color); 
        else if (active_menu == 2) print_str(1, 8, " Edit ", hl_color); 
        else if (active_menu == 3) print_str(1, 15, " View ", hl_color); 
        
        if (setting_hover_effects) {
            if (mouse_y == 1) {
                if (mouse_x >= 1 && mouse_x <= 6 && active_menu != 1) print_str(1, 1, " File ", hl_color); 
                else if (mouse_x >= 8 && mouse_x <= 13 && active_menu != 2) print_str(1, 8, " Edit ", hl_color);
                else if (mouse_x >= 15 && mouse_x <= 20 && active_menu != 3) print_str(1, 15, " View ", hl_color); 
                else if (mouse_x >= 76 && mouse_x <= 78) print_str(1, 76, " X ", cr_color);
            } else if (!active_menu) { 
                if (mouse_y == 23 && mouse_x == 78) set_char(23, 78, decode_char('3'), hl_color); 
                if (setting_v_scrollbar && mouse_y == 3 && mouse_x == 78) set_char(3, 78, decode_char('1'), hl_color);
                if (setting_v_scrollbar && mouse_y == 21 && mouse_x == 78) set_char(21, 78, decode_char('2'), hl_color);
                if (setting_h_scrollbar && mouse_y == 23 && mouse_x == 1) set_char(23, 1, decode_char('4'), hl_color);
                if (setting_h_scrollbar && mouse_y == 23 && mouse_x == (setting_v_scrollbar ? 76 : 78)) set_char(23, setting_v_scrollbar ? 76 : 78, decode_char('5'), hl_color);
            }
        }
    }
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
        "|              |",
        "{**************}"
    };
    for (i = 0; i < 12; i++) {
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

void draw_settings() {
    int y; 
    unsigned char bg = get_bg_color(), hl = get_hl_color(), cr = get_cr_color(); 
    char buf[80]; 
    char cb = setting_ansi_mode == 1 ? '\xFB' : 'V'; 
    char rb = setting_ansi_mode == 1 ? '\x07' : '*';
    
    for (y = 2; y <= 24; y++) {
        print_str(y, 0, "                                                                                ", bg);
    }
    
    print_mapped_str(0, 0, "[******^******^******^*****************************************************^***]", bg); 
    print_mapped_str(1, 0, "| File | Edit | View |                                                     | X |", bg); 
    print_mapped_str(2, 0, "@******_***^**_******_**^*********^**********^*******^*****************^***_***$", bg); 
    
    print_mapped_str(3, 0, "| Settings |            | Display | Graphics | Other |                 |\x1B Back |", bg);
    
    if (current_settings_tab == 0) {
        print_mapped_str(4, 0, "@**********_************}         {**********_*******_*****************_*******$", bg); 
        print_str(6, 2, "Mode: 80x25 (More settings available later)", bg);
    } else if (current_settings_tab == 1) {
        int disp_bg = setting_color ? setting_bg_color : ((setting_bg_color == 7) ? 7 : 0);
        int disp_fg = setting_color ? setting_fg_color : ((disp_bg == 7) ? 0 : 7);
        
        print_mapped_str(4, 0, "@**********_************_*********}          {*******_*****************_*******$", bg);
        print_str(6, 2, "Hover Effects:", bg); 
        
        unsigned char he_off_c = ((setting_hover_effects && mouse_y == 7 && mouse_x >= 4 && mouse_x <= 20) || (active_tab_element == 44)) ? hl : bg;
        unsigned char he_on_c  = ((setting_hover_effects && mouse_y == 8 && mouse_x >= 4 && mouse_x <= 20) || (active_tab_element == 45)) ? hl : bg;
        sprintf(buf, "(%c) Off", setting_hover_effects == 0 ? rb : ' '); print_str(7, 4, buf, he_off_c); 
        sprintf(buf, "(%c) On ", setting_hover_effects == 1 ? rb : ' '); print_str(8, 4, buf, he_on_c); 
        
        unsigned char ec_c = ((setting_hover_effects && mouse_y == 11 && mouse_x >= 2 && mouse_x <= 20) || (active_tab_element == 47)) ? hl : bg;
        sprintf(buf, "Enable Color [%c]", setting_color ? cb : ' '); print_str(11, 2, buf, ec_c);
        
        print_str(13, 2, "Ansi vs Unicode compatibility Mode:", bg); 
        
        unsigned char ansi0_c = ((setting_hover_effects && mouse_y == 14 && mouse_x >= 4 && mouse_x <= 30) || (active_tab_element == 48)) ? hl : bg;
        unsigned char ansi1_c = ((setting_hover_effects && mouse_y == 15 && mouse_x >= 4 && mouse_x <= 30) || (active_tab_element == 49)) ? hl : bg;
        sprintf(buf, "(%c) Standard Ansi Mode", setting_ansi_mode == 0 ? rb : ' '); print_str(14, 4, buf, ansi0_c); 
        sprintf(buf, "(%c) Extended Ascii Mode", setting_ansi_mode == 1 ? rb : ' '); print_str(15, 4, buf, ansi1_c);
        
        print_str(17, 2, "Theme Settings:", bg); 
        print_theme_line(18, "Background Color(BG)", disp_bg, setting_color, bg, hl, mouse_x, mouse_y, setting_hover_effects); 
        print_theme_line(19, "Foreground Color(FG)", disp_fg, setting_color, bg, hl, mouse_x, mouse_y, setting_hover_effects);
        if (setting_color) { 
            print_theme_line(20, "Selection  Color(BG)", setting_sel_color, 1, bg, hl, mouse_x, mouse_y, setting_hover_effects); 
            print_theme_line(21, "Critical   Color(BG)", setting_crit_color, 1, bg, hl, mouse_x, mouse_y, setting_hover_effects); 
        }
    } else {
        print_mapped_str(4, 0, "@**********_************_*********_**********}       {*****************_*******$", bg);
        print_str(6, 2, "System:", bg); 
        sprintf(buf, "Mouse Support: %s [%c] (Auto-detected)", use_mouse ? "ON " : "OFF", use_mouse ? cb : ' '); print_str(8, 2, buf, bg);
    }
    
    for (y = 5; y <= 23; y++) { 
        set_char(y, 0, decode_char('|'), bg); 
        set_char(y, 79, decode_char('|'), bg); 
    } 
    print_mapped_str(24, 0, "{******************************************************************************}", bg);
    
    if (setting_hover_effects && mouse_y == 1 && mouse_x >= 76 && mouse_x <= 78) {
        print_str(1, 76, " X ", cr);
    }
    
    if ((setting_hover_effects && mouse_y == 3 && mouse_x >= 25 && mouse_x <= 33) || active_tab_element == 40) print_str(3, 25, " Display ", hl); 
    if ((setting_hover_effects && mouse_y == 3 && mouse_x >= 35 && mouse_x <= 44) || active_tab_element == 41) print_str(3, 35, " Graphics ", hl); 
    if ((setting_hover_effects && mouse_y == 3 && mouse_x >= 46 && mouse_x <= 52) || active_tab_element == 42) print_str(3, 46, " Other ", hl); 
    if ((setting_hover_effects && mouse_y == 3 && mouse_x >= 71 && mouse_x <= 77) || active_tab_element == 43) print_str(3, 72, setting_ansi_mode ? "\x1B Back " : "< Back ", cr); 
}

void draw_file_dialog(int is_save) {
    int r, i; 
    unsigned char bg_color = get_bg_color(), hl_color = get_hl_color(), cr_color = get_cr_color(); 
    char pdisp[45], sdisp[20], idisp[45], statdisp[70]; 
    int lsize, th, tsize, tpos; 
    char* active_path;
    const char* fd[25] = {
        "[******^******^******^*****************************************************^***]", 
        "| File | Edit | View |                                                     | X |", 
        "@******_******_******_*****************************************************_***$", 
        "| [**] [**] [*] [*******************************************] [**************] |", 
        "| |4*| |*5| |1| |                                           | |              | |", 
        "| {**} {**} {*} {*******************************************} {**************} |", 
        "| [***********] [**********************************************************^*] |", 
        "| |  jump to  | | Title                                    |  type  | size |1| |", 
        "| @***********$ @**********************************************************+*$ |", 
        "| |  C:       | |                                                          |?| |", 
        "| |  D:       | |                                                          | | |", 
        "| |  E:       | |                                                          | | |", 
        "| |  F:       | |                                                          | | |", 
        "| |  G:       | |                                                          | | |", 
        "| |  H:       | |                                                          | | |", 
        "| |           | |                                                          | | |", 
        "| |           | |                                                          | | |", 
        "| |           | |                                                          |2| |", 
        "| {***********} {**********************************************************_*} |", 
        "|               [******************************************] [******] [******] |", 
        "|   File Name:  |                                          | |  ok  | |cancel| |", 
        "|               {******************************************} {******} {******} |", 
        "@***********^****************************************************************^*$", 
        "|           |                                                                |3|", 
        "{***********_****************************************************************_*}"
    };
    for (r = 0; r < 25; r++) print_mapped_str(r, 0, fd[r], bg_color);
    if (is_save) print_str(23, 2, " Save As ", bg_color); 
    else print_str(23, 2, "Open File", bg_color);
    
    active_path = (active_input_field == 3) ? dialog_address_text : current_browser_path;
    if (active_input_field == 3 && dialog_address_cursor > 40) { 
        strncpy(pdisp, active_path + (dialog_address_cursor - 40), 41); 
        pdisp[41] = '\0'; 
    } else { 
        if (strlen(active_path) > 41) { 
            strncpy(pdisp, active_path, 38); 
            pdisp[38] = '\0'; 
            strcat(pdisp, "..."); 
        } else { 
            strcpy(pdisp, active_path); 
            while(strlen(pdisp) < 41) strcat(pdisp, " "); 
        } 
    }
    print_str(4, 18, pdisp, bg_color);
    
    if (strlen(dialog_search_text) == 0 && active_input_field != 2) { 
        strcpy(sdisp, " search here"); 
        while(strlen(sdisp) < 12) strcat(sdisp, " "); 
    } else { 
        if (strlen(dialog_search_text) > 12) { 
            strcpy(sdisp, dialog_search_text + (strlen(dialog_search_text) - 12)); 
        } else { 
            strcpy(sdisp, dialog_search_text); 
            while(strlen(sdisp) < 12) strcat(sdisp, " "); 
        } 
    }
    print_str(4, 64, sdisp, bg_color);
    
    lsize = num_browser_files; th = 8; 
    tsize = lsize > 9 ? (int)((8L * 9) / lsize) : th; 
    if (tsize < 1) tsize = 1; 
    tpos = lsize > 9 ? (int)((long)browser_scroll_offset * (8 - tsize) / (lsize - 9)) : 0;
    
    for (i = 0; i < th; i++) {
        set_char(9 + i, 76, (lsize <= 9 || (i >= tpos && i < tpos + tsize)) ? decode_char('?') : ' ', bg_color);
    }
    
    for (i = 0; i < 9 && (browser_scroll_offset + i) < lsize; i++) {
        int idx = browser_scroll_offset + i; 
        unsigned char c_col = (unsigned char)(((idx == browser_selected_index) || (active_tab_element == 7 && idx == (browser_scroll_offset))) ? hl_color : bg_color); 
        char n_buf[45], s_buf[15], row_buf[80];
        if (strlen(browser_files[idx].name) > 40) { 
            strncpy(n_buf, browser_files[idx].name, 37); 
            n_buf[37] = '\0'; 
            strcat(n_buf, "..."); 
            strcat(n_buf, " "); 
        } else { 
            strcpy(n_buf, browser_files[idx].name); 
            while(strlen(n_buf) < 41) strcat(n_buf, " "); 
        }
        if (browser_files[idx].is_dir) { 
            strcpy(s_buf, "     "); 
        } else { 
            long kb = browser_files[idx].size / 1024; 
            if (kb == 0 && browser_files[idx].size > 0) kb = 1; 
            sprintf(s_buf, "%ldKB", kb); 
            while(strlen(s_buf) < 5) strcat(s_buf, " "); 
        }
        sprintf(row_buf, "%s  %s   %s", n_buf, browser_files[idx].is_dir ? "Folder" : "Text  ", s_buf); 
        print_str(9 + i, 18, row_buf, c_col);
    }
    
    if (strlen(dialog_input_text) > 40) { 
        strcpy(idisp, dialog_input_text + (strlen(dialog_input_text) - 40)); 
    } else { 
        strcpy(idisp, dialog_input_text); 
        while(strlen(idisp) < 40) strcat(idisp, " "); 
    }
    print_str(20, 18, idisp, bg_color);
    
    if (strlen(dialog_status_msg) > 62) { 
        strncpy(statdisp, dialog_status_msg, 62); 
        statdisp[62] = '\0'; 
    } else { 
        strcpy(statdisp, dialog_status_msg); 
        while(strlen(statdisp) < 62) strcat(statdisp, " "); 
    }
    print_str(23, 14, statdisp, bg_color);

    if ((setting_hover_effects && mouse_y == 20 && mouse_x >= 61 && mouse_x <= 68) || active_tab_element == 8) print_str(20, 62, "  ok  ", hl_color);
    if ((setting_hover_effects && mouse_y == 20 && mouse_x >= 70 && mouse_x <= 77) || active_tab_element == 9) print_str(20, 71, "cancel", hl_color);
    if ((setting_hover_effects && mouse_y == 1 && mouse_x >= 76 && mouse_x <= 78) || active_tab_element == 10) print_str(1, 76, " X ", cr_color);
    if (setting_hover_effects && mouse_y == 7 && mouse_x == 76) set_char(7, 76, decode_char('1'), hl_color);
    if (setting_hover_effects && mouse_y == 17 && mouse_x == 76) set_char(17, 76, decode_char('2'), hl_color);
    if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 2 && mouse_x <= 5) || active_tab_element == 4) print_mapped_str(4, 2, "|4*|", hl_color);
    if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 7 && mouse_x <= 10) || active_tab_element == 5) print_mapped_str(4, 7, "|*5|", hl_color);
    if ((setting_hover_effects && mouse_y == 4 && mouse_x >= 12 && mouse_x <= 14) || active_tab_element == 6) print_mapped_str(4, 12, "|1|", hl_color);
    
    if (active_input_field > 0) {
        int cx = 0, cy = 0;
        unsigned char cur_col, far* vram;
        int offset;
        char char_under;
        
        if (active_input_field == 1) { cx = 18 + dialog_input_cursor; if (cx > 57) cx = 57; cy = 20; }
        else if (active_input_field == 2) { cx = 64 + dialog_search_cursor; if (cx > 75) cx = 75; cy = 4; }
        else if (active_input_field == 3) { 
            int dcur = dialog_address_cursor;
            if (dcur > 40) cx = 18 + 40; else cx = 18 + dcur;
            cy = 4; 
        }
        
        cur_col = get_cursor_color();
        vram = (unsigned char far*)MK_FP(VIDEO_ADDRESS, 0x0000);
        offset = (cy * 80 + cx) * 2;
        char_under = vram[offset];
        set_char(cy, cx, char_under, cur_col);
    }
}

void draw_menu() {
    int i; 
    unsigned char mnu_color = get_bg_color(), hl_color = get_hl_color(), cr_color = get_cr_color(); 
    
    const char* file_menu[] = { 
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
    
    const char* edit_menu[] = { 
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
        "|              |", 
        "{**************}" 
    };
    
    const char* view_menu[] = { 
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
        for (i = 0; i < 12; i++) print_mapped_str(2 + i, 7, edit_menu[i], mnu_color);
        if (active_menu_hover == 0) print_str(3, 8, " Undo         ", hl_color); 
        else if (active_menu_hover == 1) print_str(4, 8, " Redo         ", hl_color);
        else if (active_menu_hover == 2) print_str(6, 8, " Cut          ", hl_color); 
        else if (active_menu_hover == 3) print_str(7, 8, " Copy         ", hl_color);
        else if (active_menu_hover == 4) print_str(8, 8, " Paste        ", hl_color); 
        else if (active_menu_hover == 5) print_str(9, 8, " Delete       ", cr_color);
        else if (active_menu_hover == 6) print_str(11, 8, " Select All   ", hl_color);

        if (setting_hover_effects && mouse_y >= 3 && mouse_y <= 12 && mouse_x >= 8 && mouse_x <= 21) {
            if (mouse_y == 3) print_str(3, 8, " Undo         ", hl_color); 
            else if (mouse_y == 4) print_str(4, 8, " Redo         ", hl_color);
            else if (mouse_y == 6) print_str(6, 8, " Cut          ", hl_color); 
            else if (mouse_y == 7) print_str(7, 8, " Copy         ", hl_color);
            else if (mouse_y == 8) print_str(8, 8, " Paste        ", hl_color); 
            else if (mouse_y == 9) print_str(9, 8, " Delete       ", cr_color);
            else if (mouse_y == 11) print_str(11, 8, " Select All   ", hl_color);
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
    /* The dialog itself pops out using the vibrant normal theme colors! */
    unsigned char dlg_color = get_bg_color(), hl_color = get_hl_color(), cr_color = get_cr_color(); 
    char fname_buf[40];
    
    const char* d[10] = { 
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
    
    for(i = 0; i < 10; i++) print_mapped_str(7 + i, 18, d[i], dlg_color);
    
    title_len = (int)strlen(current_filename);
    if (title_len > 38) { 
        strncpy(fname_buf, current_filename, 35); 
        fname_buf[35] = '\0'; 
        strcat(fname_buf, "..."); 
    } else {
        strcpy(fname_buf, current_filename);
    }
    print_str(11, 20, fname_buf, dlg_color);
    
    if ((setting_hover_effects && mouse_y == 14 && mouse_x >= 22 && mouse_x <= 29) || active_tab_element == 1) print_str(14, 23, " Save ", hl_color); 
    if ((setting_hover_effects && mouse_y == 14 && mouse_x >= 33 && mouse_x <= 46) || active_tab_element == 2) print_str(14, 34, " Don't Save ", hl_color);
    if ((setting_hover_effects && mouse_y == 14 && mouse_x >= 50 && mouse_x <= 57) || active_tab_element == 3) print_str(14, 51, "Cancel", cr_color); 
}

void draw_editor() {
    int r, c, i, line_idx, screen_cx, screen_cy, thumb_pos = 0, tsize = 0, req_vp, cur_vp, rlen, target_vp;
    char under_cursor, ch, status_left[30], status_right[60]; 
    long chars_count = 0;
    unsigned char ch_color, cur_color;
    
    /* Active Theme Variables */
    unsigned char bg_color = get_bg_color();
    unsigned char hl_color = get_hl_color();
    unsigned char cur_cursor_color = get_cursor_color();
    unsigned char dim_color = get_dim_color();
    unsigned char scroll_color = make_color(7,7);
    
    /* "Lights Out" Logic: Dim everything in the background if the Exit Dialog is open */
    unsigned char base_ui_color = show_exit_dialog ? dim_color : bg_color;
    
    int view_w = setting_v_scrollbar ? 76 : 78;
    
    hide_mouse();
    if (show_settings) { draw_settings(); show_mouse(); return; }
    if (show_open_dialog || show_save_dialog) { draw_file_dialog(show_save_dialog); show_mouse(); return; }
    
    if (needs_recalc) { build_visual_map(); needs_recalc = 0; }
    
    update_vis_cursor();
    req_vp = (vis_lines + 18) / 19; if (req_vp == 0) req_vp = 1;
    
    if (force_cursor_snap) {
        target_vp = (vis_cy / 19) + 1;
        scroll_y = (target_vp - 1) * 19;
        
        if (setting_word_wrap) {
            scroll_x = 0;
        } else {
            if (cursor_x < scroll_x) scroll_x = cursor_x;
            if (cursor_x >= scroll_x + view_w) scroll_x = cursor_x - view_w + 1;
        }
        force_cursor_snap = 0;
    }
    
    if (setting_word_wrap) {
        scroll_x = 0;
    } else {
        int m_scroll = (max_line_len > view_w ? max_line_len - view_w : 0);
        if (scroll_x > m_scroll) scroll_x = m_scroll;
    }
    
    cur_vp = (scroll_y / 19) + 1;
    if (cur_vp > req_vp) { cur_vp = req_vp; scroll_y = (cur_vp - 1) * 19; }
    if (scroll_y < 0) { scroll_y = 0; cur_vp = 1; }
    
    if (needs_ui_redraw) {
        const char* r2_v = "@******_******_******_*****************************************************_*^*$";
        const char* r2_nv= "@******_******_******_*****************************************************_***$";
        
        const char* r22_v_h = "@*^************************************************************************^*+*$";
        const char* r22_v_nh= "@****************************************************************************+*$";
        const char* r22_nv_h= "@*^************************************************************************^*^*$";
        const char* r22_nv_nh="@****************************************************************************^*$";
        
        const char* r23_v_h = "|4|                                                                        |5|3|";
        const char* r23_v_nh= "|                                                                            |3|";
        const char* r23_nv_h= "|4|                                                                        |5|3|";
        const char* r23_nv_nh="|                                                                            |3|";
        
        const char* r24_v_h = "{*_************************************************************************_*_*}";
        const char* r24_v_nh= "{****************************************************************************_*}";
        const char* r24_nv_h= "{*_************************************************************************_*_*}";
        const char* r24_nv_nh="{****************************************************************************_*}";
        
        for (r = 0; r < 25; r++) { 
            const char* row_str = ui_layout[r];
            if (r == 2) row_str = setting_v_scrollbar ? r2_v : r2_nv;
            else if (r == 22) row_str = setting_v_scrollbar ? (setting_h_scrollbar ? r22_v_h : r22_v_nh) : (setting_h_scrollbar ? r22_nv_h : r22_nv_nh);
            else if (r == 23) row_str = setting_v_scrollbar ? (setting_h_scrollbar ? r23_v_h : r23_v_nh) : (setting_h_scrollbar ? r23_nv_h : r23_nv_nh);
            else if (r == 24) row_str = setting_v_scrollbar ? (setting_h_scrollbar ? r24_v_h : r24_v_nh) : (setting_h_scrollbar ? r24_nv_h : r24_nv_nh);
            
            for (c = 0; c < 80; c++) { 
                ch = row_str[c]; 
                if (ch == '\0') break; 
                
                /* Protect the right window border against T-Junction leaks from ui_layout */
                if (!setting_v_scrollbar && r >= 3 && r <= 21 && c == 79) ch = '|';
                
                set_char(r, c, decode_char(ch), base_ui_color); 
            } 
        } 

        char title_buf[60]; int title_len;
        strcpy(title_buf, is_document_dirty ? "*" : ""); 
        strncat(title_buf, current_filename, 48); 
        title_len = (int)strlen(title_buf);
        if (title_len > 51) {
            strcpy(&title_buf[48], "..."); 
        } else { 
            while (title_len < 51) title_buf[title_len++] = ' '; 
            title_buf[51] = '\0'; 
        }
        print_str(1, 23, title_buf, base_ui_color);
        
        needs_ui_redraw = 0;
    }
    
    for (r = 0; r < 19; r++) {
        line_idx = scroll_y + r;
        if (line_idx < vis_lines) {
            int log_y = vis_map[line_idx].log_y; 
            int start_x = vis_map[line_idx].log_x; 
            int v_len = vis_map[line_idx].len;
            for (c = 0; c < view_w; c++) {
                int char_idx = start_x + scroll_x + c;
                if (scroll_x + c < v_len) {
                    ch_color = is_in_selection(log_y, char_idx) ? hl_color : bg_color;
                    if (show_exit_dialog) ch_color = dim_color;
                    set_char(r + 3, c + 1, text_buffer[log_y][char_idx], ch_color);
                } else {
                    ch_color = is_in_selection(log_y, char_idx) ? hl_color : bg_color; 
                    if (show_exit_dialog) ch_color = dim_color;
                    set_char(r + 3, c + 1, ' ', ch_color);
                }
            }
        } else { 
            for (c = 0; c < view_w; c++) set_char(r + 3, c + 1, ' ', base_ui_color); 
        }
    }
    
    if (setting_v_scrollbar) {
        tsize = (req_vp > 1) ? (int)((15L + req_vp / 2) / req_vp) : 15; 
        if (tsize < 1) tsize = 1; 
        if (tsize > 15) tsize = 15;
        thumb_pos = (req_vp > 1) ? (int)(((long)(cur_vp - 1) * (15 - tsize) + (req_vp - 1) / 2) / (req_vp - 1)) : 0;
        
        for (i = 0; i < 15; i++) {
            set_char(5 + i, 78, (i >= thumb_pos && i < thumb_pos + tsize) ? decode_char('?') : ' ', bg_color);
        }
    }
    
    int is_hovering_hbar = (!show_exit_dialog && setting_h_scrollbar && mouse_y == 23 && mouse_x >= 1 && mouse_x <= (setting_v_scrollbar ? 76 : 78));
    
    if (setting_h_scrollbar && is_hovering_hbar) {
        int hw = 72;
        int h_tsize, h_thumb_pos, max_scroll_x;
        max_scroll_x = (max_line_len > view_w ? max_line_len - view_w : 0);
        
        h_tsize = (max_line_len > view_w) ? (int)((hw * view_w) / max_line_len) : hw;
        if (h_tsize < 3) h_tsize = 3; 
        if (h_tsize > hw) h_tsize = hw;
        
        h_thumb_pos = (max_scroll_x > 0) ? (int)(((long)scroll_x * (hw - h_tsize) + (max_scroll_x - 1) / 2) / max_scroll_x) : 0;
        
        for (i = 0; i < hw; i++) {
            set_char(23, 3 + i, (i >= h_thumb_pos && i < h_thumb_pos + h_tsize) ? decode_char('?') : ' ', bg_color);
        }
    } else {
        int clear_start = setting_h_scrollbar ? 3 : 1;
        int clear_end = setting_h_scrollbar ? 74 : 76;
        
        for (i = clear_start; i <= clear_end; i++) {
            set_char(23, i, ' ', base_ui_color);
        }
        
        for (i = 0; i < total_lines; i++) {
            chars_count += (long)strlen(text_buffer[i]);
        }
        
        sprintf(status_left, "Pg %d/%d", cur_vp, req_vp); 
        print_str(23, 4, status_left, base_ui_color);
        
        sprintf(status_right, "Chars: %ld   Ln %d, Col %d", chars_count, cursor_y + 1, cursor_x + 1); 
        rlen = (int)strlen(status_right); 
        /* Mathematically guaranteed to end right before the Gear Icon boundary! */
        print_str(23, 74 - rlen, status_right, base_ui_color);
    }
    
    draw_main_hovers();
    
    screen_cx = (vis_cx - scroll_x) + 1; 
    screen_cy = (vis_cy - scroll_y) + 3;
    
    if (screen_cy >= 3 && screen_cy <= 21 && screen_cx >= 1 && screen_cx <= view_w && !active_menu && !show_exit_dialog && !show_context_menu) {
        under_cursor = ' '; 
        if (cursor_x < (int)strlen(text_buffer[cursor_y])) {
            under_cursor = text_buffer[cursor_y][cursor_x];
        }
        cur_color = (unsigned char)(is_in_selection(cursor_y, cursor_x) ? bg_color : cur_cursor_color); 
        if (show_exit_dialog) cur_color = dim_color;
        set_char(screen_cy, screen_cx, under_cursor, cur_color);
    }
    
    if (show_context_menu) draw_context_menu();
    else if (show_exit_dialog) draw_exit_dialog(); 
    else if (active_menu > 0) draw_menu();
    
    show_mouse();
}

void execute_menu_action(int menu_id, int action_y) {
    if (menu_id == 1) {
        if (action_y == 3) { 
            total_lines = 1; text_buffer[0][0] = '\0'; 
            cursor_x = 0; cursor_y = 0; is_document_dirty = 0; 
            scroll_y = 0; scroll_x = 0; needs_recalc = 1; 
            clear_entire_history();
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
                char filepath[256]; FILE* f; 
                strcpy(filepath, current_browser_path); 
                strcat(filepath, current_filename); 
                f = fopen(filepath, "w"); 
                if (f) { 
                    int i; 
                    for (i = 0; i < total_lines; i++) { 
                        fputs(text_buffer[i], f); fputs("\n", f); 
                    } 
                    fclose(f); is_document_dirty = 0; 
                } 
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
            if (has_selection) { save_state(); last_was_typing = 0; delete_selection(); }
            else {
                save_state(); last_was_typing = 0;
                total_lines = 1; text_buffer[0][0] = '\0'; 
                cursor_x = 0; cursor_y = 0; is_document_dirty = 1; 
                scroll_y = 0; scroll_x = 0; needs_recalc = 1; 
                needs_ui_redraw = 1; force_cursor_snap = 1;
            }
        }
        else if (action_y == 10) { show_settings = 1; active_tab_element = 0; } 
        else if (action_y == 12) { 
            if (is_document_dirty) { show_exit_dialog = 1; active_tab_element = 1; } else exit(0); 
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
            cursor_y = total_lines - 1; 
            cursor_x = (int)strlen(text_buffer[cursor_y]); 
            has_selection = 1; force_cursor_snap = 1; 
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
        f = fopen(filepath, "w");
        if (f) { 
            int i; 
            for (i = 0; i < total_lines; i++) { 
                fputs(text_buffer[i], f); 
                fputs("\n", f); 
            } 
            fclose(f); 
            strcpy(current_filename, dialog_input_text); 
            is_document_dirty = 0; 
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
        f = fopen(filepath, "r");
        if (f) {
            char line_chunk[512]; 
            int i; 
            for(i = 0; i < total_lines; i++) {
                free(text_buffer[i]);
            }
            total_lines = 0;
            
            char* current_line = (char*)malloc(1); 
            current_line[0] = '\0'; 
            int cur_len = 0;
            
            while (fgets(line_chunk, sizeof(line_chunk), f)) {
                int chunk_len = (int)strlen(line_chunk); 
                int has_newline = 0;
                
                if (chunk_len > 0 && line_chunk[chunk_len - 1] == '\n') {
                    has_newline = 1;
                }
                while (chunk_len > 0 && (line_chunk[chunk_len-1] == '\n' || line_chunk[chunk_len-1] == '\r')) { 
                    line_chunk[chunk_len-1] = '\0'; 
                    chunk_len--; 
                }
                
                current_line = (char*)realloc(current_line, (size_t)(cur_len + chunk_len + 1)); 
                strcpy(current_line + cur_len, line_chunk); 
                cur_len += chunk_len;
                
                if (has_newline) { 
                    if (total_lines >= buffer_capacity) { 
                        buffer_capacity *= 2; 
                        text_buffer = (char**)realloc(text_buffer, buffer_capacity * sizeof(char*)); 
                    } 
                    text_buffer[total_lines++] = current_line; 
                    current_line = (char*)malloc(1); 
                    current_line[0] = '\0'; 
                    cur_len = 0; 
                }
            }
            if (total_lines >= buffer_capacity) { 
                buffer_capacity *= 2; 
                text_buffer = (char**)realloc(text_buffer, buffer_capacity * sizeof(char*)); 
            } 
            text_buffer[total_lines++] = current_line;
            
            fclose(f); 
            strcpy(current_filename, dialog_input_text); 
            is_document_dirty = 0; 
            cursor_x = 0; cursor_y = 0; scroll_y = 0; scroll_x = 0; 
            show_open_dialog = 0; show_save_dialog = 0; 
            active_tab_element = 0;
            clear_entire_history();
            needs_ui_redraw = 1; needs_recalc = 1; force_cursor_snap = 1; 
            draw_editor();
        } else { 
            strcpy(dialog_status_msg, "Error: File not found!"); 
            draw_editor(); 
        }
    }
}

int main(int argc, char* argv[]) {
    int running = 1, last_mouse_x = -1, last_mouse_y = -1, key, max_x, target_y; 
    int r, c;
    int last_mouse_left = 0, last_mouse_right = 0;
    unsigned int last_click_ticks = 0; 
    int last_click_cx = -1, last_click_cy = -1;
    char cwd_buf[256];
    
    int alt_state = 0;
    int last_alt_state = 0;
    int keys_pressed_during_alt = 0;
    
    if (argv[0] != NULL) {
        char* last_slash = strrchr(argv[0], '\\');
        if (!last_slash) last_slash = strrchr(argv[0], '/');
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
    load_cursor_glyph(); 
    init_buffer();
    hide_hardware_cursor();
    
    if (!init_mouse()) {
        printf("Notice: DOS Mouse driver not loaded.\n");
    }
    
    /* Attempt to load file passed as argument */
    if (argc > 1) {
        FILE* f = fopen(argv[1], "r");
        if (f) {
            char line_chunk[512]; 
            int i; 
            for(i = 0; i < total_lines; i++) {
                free(text_buffer[i]);
            }
            total_lines = 0;
            
            char* current_line = (char*)malloc(1); 
            current_line[0] = '\0'; 
            int cur_len = 0;
            
            while (fgets(line_chunk, sizeof(line_chunk), f)) {
                int chunk_len = (int)strlen(line_chunk); 
                int has_newline = 0;
                
                if (chunk_len > 0 && line_chunk[chunk_len - 1] == '\n') {
                    has_newline = 1;
                }
                while (chunk_len > 0 && (line_chunk[chunk_len-1] == '\n' || line_chunk[chunk_len-1] == '\r')) { 
                    line_chunk[chunk_len-1] = '\0'; 
                    chunk_len--; 
                }
                
                current_line = (char*)realloc(current_line, (size_t)(cur_len + chunk_len + 1)); 
                strcpy(current_line + cur_len, line_chunk); 
                cur_len += chunk_len;
                
                if (has_newline) { 
                    if (total_lines >= buffer_capacity) { 
                        buffer_capacity *= 2; 
                        text_buffer = (char**)realloc(text_buffer, buffer_capacity * sizeof(char*)); 
                    } 
                    text_buffer[total_lines++] = current_line; 
                    current_line = (char*)malloc(1); 
                    current_line[0] = '\0'; 
                    cur_len = 0; 
                }
            }
            if (total_lines >= buffer_capacity) { 
                buffer_capacity *= 2; 
                text_buffer = (char**)realloc(text_buffer, buffer_capacity * sizeof(char*)); 
            } 
            text_buffer[total_lines++] = current_line;
            fclose(f);
            
            /* Extract filename from path for the title bar */
            char* last_slash = strrchr(argv[1], '\\');
            if (!last_slash) last_slash = strrchr(argv[1], '/');
            
            if (last_slash) strcpy(current_filename, last_slash + 1);
            else strcpy(current_filename, argv[1]);
        } else {
            /* File doesn't exist, but we still adopt the name for saving later */
            char* last_slash = strrchr(argv[1], '\\');
            if (!last_slash) last_slash = strrchr(argv[1], '/');
            
            if (last_slash) strcpy(current_filename, last_slash + 1);
            else strcpy(current_filename, argv[1]);
        }
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
            if (!keys_pressed_during_alt && !show_settings && !show_open_dialog && !show_save_dialog && !show_exit_dialog && !show_context_menu) {
                if (active_menu == 0) { active_menu = 1; active_menu_hover = 0; }
                else { active_menu = 0; active_menu_hover = -1; }
                needs_ui_redraw = 1;
                draw_editor();
            }
        }
        last_alt_state = alt_state;
        
        if (mouse_right && !last_mouse_right) {
            if (!show_settings && !show_open_dialog && !show_save_dialog && !show_exit_dialog) {
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
            int current_hover_hbar = (setting_h_scrollbar && mouse_y == 23 && mouse_x >= 1 && mouse_x <= (setting_v_scrollbar ? 76 : 78));
            static int last_hover_hbar = 0;
            
            if (hid != last_hover_id || is_dragging || current_hover_hbar != last_hover_hbar) {
                int hbar_changed = (current_hover_hbar != last_hover_hbar);
                last_hover_id = hid;
                last_hover_hbar = current_hover_hbar;
                
                if (active_menu > 0 && mouse_y == 1 && !is_dragging && !show_settings && !show_open_dialog && !show_save_dialog && !show_exit_dialog && !show_context_menu) {
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
                    draw_editor(); 
                } else {
                    hide_mouse();
                    if (show_settings) draw_settings(); 
                    else if (show_open_dialog || show_save_dialog) draw_file_dialog(show_save_dialog);
                    else if (show_context_menu) { draw_main_hovers(); draw_context_menu(); }
                    else if (show_exit_dialog) { draw_main_hovers(); draw_exit_dialog(); }
                    else if (active_menu > 0) { draw_main_hovers(); draw_menu(); } 
                    else draw_main_hovers();
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
                if (!is_extended && key == 27) { show_settings = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } 
                else if (!is_extended && key == 9) { /* Tab */
                    int shift = is_shift_pressed();
                    if (!shift) {
                        if (active_tab_element == 0) active_tab_element = 40;
                        else if (active_tab_element == 40) active_tab_element = 41;
                        else if (active_tab_element == 41) active_tab_element = 42;
                        else if (active_tab_element == 42) active_tab_element = 43;
                        else if (active_tab_element == 43) active_tab_element = (current_settings_tab == 1 ? 44 : 40);
                        else if (active_tab_element >= 44 && active_tab_element < 49) active_tab_element++;
                        else if (active_tab_element == 49) active_tab_element = 100;
                        else if (active_tab_element == 100) active_tab_element = 101;
                        else if (active_tab_element == 101) active_tab_element = setting_color ? 102 : 110;
                        else if (active_tab_element >= 102 && active_tab_element < 104) active_tab_element++;
                        else if (active_tab_element == 104) active_tab_element = 110;
                        else if (active_tab_element == 110) active_tab_element = 111;
                        else if (active_tab_element == 111) active_tab_element = setting_color ? 112 : 120;
                        else if (active_tab_element >= 112 && active_tab_element < 114) active_tab_element++;
                        else if (active_tab_element == 114) active_tab_element = setting_color ? 120 : 40;
                        else if (active_tab_element == 120) active_tab_element = 121;
                        else if (active_tab_element >= 121 && active_tab_element < 124) active_tab_element++;
                        else if (active_tab_element == 124) active_tab_element = 130;
                        else if (active_tab_element == 130) active_tab_element = 131;
                        else if (active_tab_element >= 131 && active_tab_element < 134) active_tab_element++;
                        else if (active_tab_element == 134) active_tab_element = 40;
                    } else {
                        /* Shift-Tab (Reverse) is complex, so we'll just loop forward for now to keep it simple */
                        active_tab_element = 40;
                    }
                    needs_ui_redraw = 1; draw_editor();
                }
                else if (!is_extended && key == 13 && active_tab_element > 0) { /* Enter */
                    if (active_tab_element == 40) { current_settings_tab = 0; active_tab_element = 40; draw_editor(); }
                    else if (active_tab_element == 41) { current_settings_tab = 1; active_tab_element = 40; draw_editor(); }
                    else if (active_tab_element == 42) { current_settings_tab = 2; active_tab_element = 40; draw_editor(); }
                    else if (active_tab_element == 43) { show_settings = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); }
                    else if (current_settings_tab == 1) {
                        if (active_tab_element == 44) { setting_hover_effects = 0; needs_ui_redraw = 1; draw_editor(); }
                        else if (active_tab_element == 45) { setting_hover_effects = 1; needs_ui_redraw = 1; draw_editor(); }
                        else if (active_tab_element == 47) { 
                            setting_color = !setting_color; 
                            if (!setting_color) { setting_bg_color = (setting_bg_color == 7) ? 7 : 0; setting_fg_color = (setting_bg_color == 7) ? 0 : 7; }
                            needs_ui_redraw = 1; draw_editor(); 
                        }
                        else if (active_tab_element == 48) { setting_ansi_mode = 0; needs_ui_redraw = 1; draw_editor(); }
                        else if (active_tab_element == 49) { setting_ansi_mode = 1; needs_ui_redraw = 1; draw_editor(); }
                        else if (active_tab_element >= 100) {
                            int row = 18 + (active_tab_element - 100) / 10;
                            int col_idx = active_tab_element % 10;
                            int* target = (row == 18) ? &setting_bg_color : ((row == 19) ? &setting_fg_color : ((row == 20) ? &setting_sel_color : &setting_crit_color));
                            
                            if (col_idx == 0) *target = 0;
                            else if (col_idx == 1) *target = 7;
                            else if (setting_color) {
                                int mask = (col_idx == 2) ? 1 : ((col_idx == 3) ? 2 : 4);
                                if (*target == 0 || *target == 7) *target = mask; else *target ^= mask;
                            }
                            if (setting_bg_color == setting_fg_color) {
                                if (row == 18) setting_fg_color = (setting_bg_color == 7) ? 0 : 7;
                                else if (row == 19) setting_bg_color = (setting_fg_color == 7) ? 0 : 7;
                            }
                            needs_ui_redraw = 1; draw_editor();
                        }
                    }
                }
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
                    } else if (active_tab_element == 7) { /* File List Arrow Nav */
                        if (key == 72) { /* Up */
                            if (browser_selected_index > 0) browser_selected_index--;
                            if (browser_selected_index < browser_scroll_offset) browser_scroll_offset = browser_selected_index;
                        } else if (key == 80) { /* Down */
                            if (browser_selected_index < num_browser_files - 1) browser_selected_index++;
                            if (browser_selected_index >= browser_scroll_offset + 9) browser_scroll_offset = browser_selected_index - 8;
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
                            char filepath[256]; FILE* f; 
                            strcpy(filepath, current_browser_path); strcat(filepath, current_filename); 
                            f = fopen(filepath, "w"); 
                            if (f) { int i; for (i = 0; i < total_lines; i++) { fputs(text_buffer[i], f); fputs("\n", f); } fclose(f); is_document_dirty = 0; } 
                            exit(0);
                        } 
                    }
                    else if (active_tab_element == 2) exit(0); /* Don't Save */
                    else if (active_tab_element == 3) { show_exit_dialog = 0; active_tab_element = 0; needs_ui_redraw = 1; draw_editor(); } /* Cancel */
                }
                continue; 
            }
            
            /* Global Ctrl and Alt Shortcuts */
            if (!show_settings && !show_open_dialog && !show_save_dialog && !show_exit_dialog) {
                if (!is_extended) {
                    if (key == 14) { execute_menu_action(1, 3); draw_editor(); continue; } /* Ctrl+N */
                    if (key == 15) { execute_menu_action(1, 4); draw_editor(); continue; } /* Ctrl+O */
                    if (key == 19) { execute_menu_action(1, 5); draw_editor(); continue; } /* Ctrl+S */
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
                        else active_menu_hover = (active_menu == 1 ? 6 : (active_menu == 2 ? 6 : 3));
                        needs_ui_redraw = 1; draw_editor();
                    }
                    else if (key == 80) { /* Down */
                        int max_items = (active_menu == 1 ? 6 : (active_menu == 2 ? 6 : 3));
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
                
                if (key == 72) { 
                    if (vis_cy > 0) { 
                        int n_cy = vis_cy - 1; 
                        cursor_y = vis_map[n_cy].log_y; 
                        cursor_x = vis_map[n_cy].log_x + vis_cx; 
                        if (cursor_x > vis_map[n_cy].log_x + vis_map[n_cy].len) {
                            cursor_x = vis_map[n_cy].log_x + vis_map[n_cy].len; 
                        }
                    } 
                } else if (key == 80) { 
                    if (vis_cy < vis_lines - 1) { 
                        int n_cy = vis_cy + 1; 
                        cursor_y = vis_map[n_cy].log_y; 
                        cursor_x = vis_map[n_cy].log_x + vis_cx; 
                        if (cursor_x > vis_map[n_cy].log_x + vis_map[n_cy].len) {
                            cursor_x = vis_map[n_cy].log_x + vis_map[n_cy].len; 
                        }
                    } 
                } else if (key == 75) { 
                    if (cursor_x > 0) cursor_x--; 
                    else if (cursor_y > 0) { cursor_y--; cursor_x = (int)strlen(text_buffer[cursor_y]); } 
                } else if (key == 77) { 
                    if (cursor_x < (int)strlen(text_buffer[cursor_y])) cursor_x++; 
                    else if (cursor_y < total_lines - 1) { cursor_y++; cursor_x = 0; } 
                } else if (key == 73) { 
                    if (scroll_y >= 19) scroll_y -= 19; else scroll_y = 0;
                    last_was_typing = 0; needs_ui_redraw = 1;
                } else if (key == 81) { 
                    int rv = (vis_lines + 18) / 19; 
                    if (rv == 0) rv = 1; 
                    if (scroll_y / 19 + 1 < rv) scroll_y += 19; 
                    last_was_typing = 0; needs_ui_redraw = 1;
                } else if (key == 71) { 
                    cursor_x = vis_map[vis_cy].log_x; 
                    force_cursor_snap = 1; 
                } else if (key == 79) { 
                    cursor_x = vis_map[vis_cy].log_x + vis_map[vis_cy].len; 
                    force_cursor_snap = 1; 
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
                        if (cursor_x < (int)strlen(text_buffer[cursor_y])) {
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
                
                max_x = (int)strlen(text_buffer[cursor_y]); 
                if (cursor_x > max_x) cursor_x = max_x; 
            } else {
                if (key == 27) { 
                    if (is_document_dirty) { show_exit_dialog = 1; active_tab_element = 1; } else running = 0; 
                } else if (key == 26) { /* Ctrl+Z */
                    perform_undo();
                } else if (key == 25) { /* Ctrl+Y */
                    perform_redo();
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
            /* Any click clears keyboard tab tracking */
            if (active_tab_element != 0) {
                active_tab_element = 0;
                needs_ui_redraw = 1;
                draw_editor();
            }

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
                        cursor_y = total_lines - 1; 
                        cursor_x = (int)strlen(text_buffer[cursor_y]); 
                        has_selection = 1; force_cursor_snap = 1; last_was_typing = 0;
                    }
                    while(mouse_left) update_mouse(); continue;
                }
            }
            if (show_settings) {
                if (mouse_y == 3) {
                    if (mouse_x >= 25 && mouse_x <= 33) { current_settings_tab = 0; } 
                    else if (mouse_x >= 35 && mouse_x <= 44) { current_settings_tab = 1; }
                    else if (mouse_x >= 46 && mouse_x <= 52) { current_settings_tab = 2; } 
                    else if (mouse_x >= 71 && mouse_x <= 77) { show_settings = 0; needs_ui_redraw = 1; }
                } else if (current_settings_tab == 1) {
                    if (mouse_y == 7 && mouse_x >= 4 && mouse_x <= 20) { setting_hover_effects = 0; needs_ui_redraw = 1; }
                    else if (mouse_y == 8 && mouse_x >= 4 && mouse_x <= 20) { setting_hover_effects = 1; needs_ui_redraw = 1; }
                    else if (mouse_y == 11 && mouse_x >= 2 && mouse_x <= 20) { 
                        setting_color = !setting_color; 
                        if (!setting_color) {
                            setting_bg_color = (setting_bg_color == 7) ? 7 : 0;
                            setting_fg_color = (setting_bg_color == 7) ? 0 : 7;
                        }
                        needs_ui_redraw = 1; 
                    } 
                    else if (mouse_y == 14 && mouse_x >= 4 && mouse_x <= 30) { setting_ansi_mode = 0; needs_ui_redraw = 1; }
                    else if (mouse_y == 15 && mouse_x >= 4 && mouse_x <= 30) { setting_ansi_mode = 1; needs_ui_redraw = 1; }
                    else if (mouse_y >= 18 && mouse_y <= 21) {
                        int* target = (mouse_y == 18) ? &setting_bg_color : ((mouse_y == 19) ? &setting_fg_color : ((mouse_y == 20) ? &setting_sel_color : &setting_crit_color));
                        if (!(!setting_color && mouse_y >= 20)) {
                            if (mouse_x >= 27 && mouse_x <= 35) { *target = 0; needs_ui_redraw = 1; }
                            else if (mouse_x >= 38 && mouse_x <= 46) { *target = 7; needs_ui_redraw = 1; }
                            else if (setting_color) {
                                if (mouse_x >= 49 && mouse_x <= 56) { if (*target == 0 || *target == 7) *target = 1; else *target ^= 1; needs_ui_redraw = 1; }
                                else if (mouse_x >= 59 && mouse_x <= 67) { if (*target == 0 || *target == 7) *target = 2; else *target ^= 2; needs_ui_redraw = 1; }
                                else if (mouse_x >= 70 && mouse_x <= 76) { if (*target == 0 || *target == 7) *target = 4; else *target ^= 4; needs_ui_redraw = 1; }
                            }
                            
                            /* Enforce Contrast */
                            if (setting_bg_color == setting_fg_color) {
                                if (mouse_y == 18) setting_fg_color = (setting_bg_color == 7) ? 0 : 7;
                                else if (mouse_y == 19) setting_bg_color = (setting_fg_color == 7) ? 0 : 7;
                            }
                        }
                    }
                }
                draw_editor(); while(mouse_left) update_mouse(); continue;
            }
            if (show_open_dialog || show_save_dialog) {
                if (mouse_y == 20 && mouse_x >= 61 && mouse_x <= 68) handle_file_ok_click();
                else if (mouse_y == 20 && mouse_x >= 70 && mouse_x <= 77) { 
                    show_open_dialog = 0; show_save_dialog = 0; needs_ui_redraw = 1; draw_editor(); 
                } 
                else if (mouse_y == 1 && mouse_x >= 76 && mouse_x <= 78) { 
                    show_open_dialog = 0; show_save_dialog = 0; needs_ui_redraw = 1; draw_editor(); 
                } 
                else if (mouse_y >= 9 && mouse_y <= 14 && mouse_x >= 2 && mouse_x <= 12) { 
                    char drive_path[4]; 
                    drive_path[0] = (char)('C' + (mouse_y - 9)); 
                    drive_path[1] = ':'; drive_path[2] = '\\'; drive_path[3] = '\0'; 
                    load_directory(drive_path, 1);
                }
                else if (mouse_y == 4) {
                    if (mouse_x >= 2 && mouse_x <= 5 && path_history_index > 0) {
                        path_history_index--;
                        load_directory(path_history[path_history_index], 0);
                    } else if (mouse_x >= 7 && mouse_x <= 10 && path_history_index < path_history_count - 1) {
                        path_history_index++;
                        load_directory(path_history[path_history_index], 0);
                    } else if (mouse_x >= 12 && mouse_x <= 14) {
                        char new_path[256];
                        int len = (int)strlen(current_browser_path);
                        if (len > 3) {
                            len -= 2;
                            while (len > 0 && current_browser_path[len] != '\\') len--;
                            strncpy(new_path, current_browser_path, len + 1);
                            new_path[len + 1] = '\0';
                            load_directory(new_path, 1);
                        }
                    } else if (mouse_x >= 18 && mouse_x <= 58) {
                        active_input_field = 3;
                        strcpy(dialog_address_text, current_browser_path);
                        dialog_address_cursor = (int)strlen(dialog_address_text);
                        strcpy(dialog_status_msg, "Editing address...");
                    } else if (mouse_x >= 64 && mouse_x <= 77) {
                        active_input_field = 2;
                        dialog_search_cursor = mouse_x - 64;
                        if (dialog_search_cursor > (int)strlen(dialog_search_text)) dialog_search_cursor = (int)strlen(dialog_search_text);
                        strcpy(dialog_status_msg, "Searching...");
                    }
                }
                else if (mouse_y == 20 && mouse_x >= 18 && mouse_x <= 58) {
                    active_input_field = 1;
                    dialog_input_cursor = mouse_x - 18;
                    if (dialog_input_cursor > (int)strlen(dialog_input_text)) dialog_input_cursor = (int)strlen(dialog_input_text);
                    strcpy(dialog_status_msg, show_save_dialog ? "Ready to save file..." : "Ready to select file...");
                }
                else if (mouse_y == 7 && mouse_x == 76) {
                    if (browser_scroll_offset > 0) browser_scroll_offset--;
                }
                else if (mouse_y == 17 && mouse_x == 76) {
                    if (browser_scroll_offset < num_browser_files - 9) browser_scroll_offset++;
                }
                else if (mouse_y >= 9 && mouse_y <= 16 && mouse_x == 76) {
                    int lsize = num_browser_files;
                    if (lsize > 9) {
                        while (mouse_left) {
                            int clamped_y = mouse_y;
                            int new_offset;
                            if (clamped_y < 9) clamped_y = 9;
                            if (clamped_y > 16) clamped_y = 16;
                            
                            new_offset = (int)(((long)(clamped_y - 9) * (lsize - 9) + 3) / 7);
                            if (new_offset < 0) new_offset = 0;
                            if (new_offset > lsize - 9) new_offset = lsize - 9;
                            
                            if (new_offset != browser_scroll_offset) {
                                browser_scroll_offset = new_offset;
                                needs_ui_redraw = 1;
                                draw_editor();
                            }
                            update_mouse();
                        }
                    }
                }
                else if (mouse_y >= 9 && mouse_y <= 17 && mouse_x >= 18 && mouse_x <= 75) { 
                    int idx = browser_scroll_offset + (mouse_y - 9); 
                    if (idx < num_browser_files) { 
                        browser_selected_index = idx; 
                        if (browser_files[idx].is_dir) { 
                            char new_path[256]; 
                            if (strcmp(browser_files[idx].name, "..") == 0) { 
                                int len = (int)strlen(current_browser_path); 
                                if (len > 3) { 
                                    len -= 2; 
                                    while (len > 0 && current_browser_path[len] != '\\') len--; 
                                    current_browser_path[len + 1] = '\0'; 
                                } 
                                strcpy(new_path, current_browser_path); 
                            } else { 
                                strcpy(new_path, current_browser_path); 
                                strcat(new_path, browser_files[idx].name); 
                            } 
                            load_directory(new_path, 1); 
                        } else { 
                            strcpy(dialog_input_text, browser_files[idx].name); 
                            dialog_input_cursor = (int)strlen(dialog_input_text); 
                            active_input_field = 1; 
                            strcpy(dialog_status_msg, show_save_dialog ? "Ready to save file..." : "Ready to select file..."); 
                        } 
                    } 
                } else {
                    active_input_field = 0; /* Deselect any input field on dead click */
                }
                needs_ui_redraw = 1; draw_editor();
                while(mouse_left) update_mouse(); continue;
            }
            if (show_exit_dialog) {
                if (mouse_y == 14) { 
                    if (mouse_x >= 22 && mouse_x <= 29) running = 0; 
                    else if (mouse_x >= 33 && mouse_x <= 46) running = 0; 
                    else if (mouse_x >= 50 && mouse_x <= 57) { show_exit_dialog = 0; needs_ui_redraw = 1; draw_editor(); } 
                }
                while(mouse_left) update_mouse(); continue;
            }
            if (active_menu > 0) {
                int old_menu = active_menu;
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
                draw_editor(); while(mouse_left) update_mouse(); continue;
            }
            if (mouse_y == 1 && !is_dragging) {
                if (mouse_x >= 1 && mouse_x <= 6) { active_menu = 1; active_menu_hover = -1; }
                else if (mouse_x >= 8 && mouse_x <= 13) { active_menu = 2; active_menu_hover = -1; }
                else if (mouse_x >= 15 && mouse_x <= 20) { active_menu = 3; active_menu_hover = -1; }
                else if (mouse_x >= 76 && mouse_x <= 78) { 
                    if (is_document_dirty) { show_exit_dialog = 1; active_tab_element = 1; } else running = 0; 
                }
                if (active_menu > 0 || show_exit_dialog) { 
                    needs_ui_redraw = 1; draw_editor(); while(mouse_left) update_mouse(); continue; 
                }
            }
            
            /* FIX: Ensure the settings gear icon triggers independently of the scrollbar logic */
            if (mouse_y == 23 && mouse_x == 78) {
                show_settings = 1; 
                active_tab_element = 0;
                needs_ui_redraw = 1;
                draw_editor();
                continue;
            }

            /* TEXT SELECTION & DOUBLE CLICK LOGIC */
            int view_w = setting_v_scrollbar ? 76 : 78;
            if (mouse_y >= 3 && mouse_y <= 21 && mouse_x >= 1 && mouse_x <= view_w) {
                target_y = scroll_y + (mouse_y - 3);
                if (target_y < vis_lines) {
                    int log_y = vis_map[target_y].log_y;
                    int target_x = vis_map[target_y].log_x + scroll_x + (mouse_x - 1);
                    if (target_x > vis_map[target_y].log_x + vis_map[target_y].len) {
                        target_x = vis_map[target_y].log_x + vis_map[target_y].len;
                    }
                    
                    if (mouse_left && !last_mouse_left) {
                        unsigned int current_ticks = *((unsigned int far*)MK_FP(0x0040, 0x006C));
                        if (current_ticks - last_click_ticks < 9 && log_y == last_click_cy && target_x == last_click_cx) {
                            /* Word Select trigger! */
                            int sx = target_x, ex = target_x; 
                            char* line = text_buffer[log_y]; 
                            int len = (int)strlen(line);
                            while (sx > 0 && (isalnum(line[sx-1]) || line[sx-1] == '_')) sx--;
                            while (ex < len && (isalnum(line[ex]) || line[ex] == '_')) ex++;
                            sel_start_x = sx; sel_start_y = log_y; 
                            cursor_x = ex; cursor_y = log_y;
                            has_selection = 1; is_dragging = 1; force_cursor_snap = 1; last_was_typing = 0;
                        } else {
                            last_click_ticks = current_ticks; 
                            last_click_cy = log_y; 
                            last_click_cx = target_x;
                            is_dragging = 1; 
                            sel_start_x = target_x; 
                            sel_start_y = log_y; 
                            has_selection = 0;
                        }
                    } else if (is_dragging) {
                        if (target_x != sel_start_x || log_y != sel_start_y) has_selection = 1;
                    }
                    
                    if (!has_selection) { cursor_y = log_y; cursor_x = target_x; }
                    else if (mouse_left && last_mouse_left) { cursor_y = log_y; cursor_x = target_x; }
                    
                    max_x = (int)strlen(text_buffer[cursor_y]); 
                    if (cursor_x > max_x) cursor_x = max_x;
                    
                    if (is_dragging) force_cursor_snap = 1;
                    draw_editor();
                }
            }
            
            /* SCROLLBAR INTERACTION LOGIC */
            if (setting_v_scrollbar && mouse_x == 78) {
                if (mouse_y == 3) { 
                    if (scroll_y >= 19) scroll_y -= 19; else scroll_y = 0; 
                    needs_ui_redraw = 1; 
                } 
                else if (mouse_y == 21) { 
                    int rv = (vis_lines + 18) / 19; 
                    if (rv == 0) rv = 1; 
                    if (scroll_y / 19 + 1 < rv) scroll_y += 19; 
                    needs_ui_redraw = 1; 
                } 
                else if (mouse_y >= 5 && mouse_y <= 19) { 
                    int rv = (vis_lines + 18) / 19; 
                    if (rv == 0) rv = 1; 
                    while (mouse_left) {
                        int clamped_y = mouse_y;
                        int n_vp, new_scroll_y;
                        if (clamped_y < 5) clamped_y = 5;
                        if (clamped_y > 19) clamped_y = 19;
                        n_vp = (int)(1 + ((long)(clamped_y - 5) * (rv - 1) + 7) / 14); 
                        new_scroll_y = (n_vp - 1) * 19;
                        if (scroll_y != new_scroll_y) {
                            scroll_y = new_scroll_y;
                            needs_ui_redraw = 1;
                            draw_editor();
                        }
                        update_mouse();
                    }
                }
                draw_editor();
            } else if (setting_h_scrollbar && mouse_y == 23 && !setting_word_wrap) {
                int m_scroll = (max_line_len > view_w ? max_line_len - view_w : 0);
                if (mouse_x == 1 && !last_mouse_left) { 
                    scroll_x -= 5; 
                    if (scroll_x < 0) scroll_x = 0; 
                    needs_ui_redraw = 1; 
                }
                else if (mouse_x == (setting_v_scrollbar ? 76 : 78) && !last_mouse_left) { 
                    scroll_x += 5; 
                    if (scroll_x > m_scroll) scroll_x = m_scroll; 
                    needs_ui_redraw = 1; 
                }
                else if (mouse_x >= 3 && mouse_x <= 74) {
                    if (m_scroll > 0) {
                        int denom = 71;
                        while (mouse_left) {
                            int clamped_x = mouse_x;
                            int new_scroll_x;
                            if (clamped_x < 3) clamped_x = 3;
                            if (clamped_x > 74) clamped_x = 74;
                            new_scroll_x = (int)(((long)(clamped_x - 3) * m_scroll) / denom);
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
                }
                draw_editor();
            }
        } else is_dragging = 0;
        
        last_mouse_left = mouse_left;
        last_mouse_right = mouse_right;
    }
    
    save_config();
    clear_entire_history();
    for (r = 0; r < total_lines; r++) free(text_buffer[r]);
    free(text_buffer);
    free(vis_map);
    if (internal_clipboard) free(internal_clipboard);

    hide_mouse();
    show_hardware_cursor();
    
    /* Cleanly restore DOS screen before quitting! */
    for (r = 0; r < 25; r++) { 
        for (c = 0; c < 80; c++) {
            set_char(r, c, ' ', make_color(0, 7)); 
        }
    } 
    return 0;
}