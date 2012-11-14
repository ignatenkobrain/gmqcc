/*
 * Copyright (C) 2012
 *     Dale Weiler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "gmqcc.h"

/*
 * isatty/STDERR_FILENO/STDOUT_FILNO
 * + some other things likewise.
 */
#ifndef _WIN32
#include <unistd.h>
#endif

#define GMQCC_IS_STDOUT(X) ((X) == stdout)
#define GMQCC_IS_STDERR(X) ((X) == stderr)
#define GMQCC_IS_DEFINE(X) (GMQCC_IS_STDERR(X) || GMQCC_IS_STDOUT(X))

typedef struct {
    FILE *handle_err;
    FILE *handle_out;
    
    int   color_err;
    int   color_out;
} con_t;

/*
 * Doing colored output on windows is fucking stupid.  The linux way is
 * the real way. So we emulate it on windows :)
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/*
 * Windows doesn't have constants for FILENO, sadly but the docs tell
 * use the constant values.
 */
#undef  STDERR_FILENO
#undef  STDOUT_FILENO
#define STDERR_FILENO 2
#define STDOUT_FILENO 1

/*
 * Windows and it's posix underscore bullshit.  We simply fix this
 * with yay, another macro :P
 */
#define isatty _isatty
 
enum {
    RESET = 0,
    BOLD  = 1,
    BLACK = 30,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    GRAY,
    WHITE
};

enum {
    WBLACK,
    WBLUE,
    WGREEN   = 2,
    WRED     = 4,
    WINTENSE = 8,
    WCYAN    = WBLUE  | WGREEN,
    WMAGENTA = WBLUE  | WRED,
    WYELLOW  = WGREEN | WRED,
    WWHITE   = WBLUE  | WGREEN | WRED
}

static const ansi2win[] = {
    WBLACK,
    WRED,
    WGREEN,
    WYELLOW,
    WBLUE,
    WMAGENTA,
    WCYAN,
    WWHITE
};

static void win_fputs(char *str, FILE *f) {
    /* state for translate */
    int acolor;
    int wcolor;
    int icolor;
    
    int state;
    int place;
    
    /* attributes */
    int intense  =  -1;
    int colors[] = {-1, -1 };
    int colorpos = 1;
    
    CONSOLE_SCREEN_BUFFER_INFO cinfo;
    GetConsoleScreenBufferInfo(
        (h == stdout) ?
            GetStdHandle(STD_OUTPUT_HANDLE) :
            GetStdHandle(STD_ERROR_HANDLE), &cinfo
    );
    icolor = cinfo.wAttributes;
    
    while (*str) {
        if (*str == '\e')
            state = '\e';
        else if (state == '\e' && *str == '[')
            state = '[';
        else if (state == '[') {
            if (*str != 'm') {
                colors[colorpos] = *str;
                colorpos--;
            } else {
                int find;
                int mult;
                for (find = colorpos + 1, acolor = 0, mult = 1; find < 2; find++) {
                    acolor += (colors[find] - 48) * mult;
                    mult   *= 10;
                }
                
                /* convert to windows color */
                if (acolor == BOLD)
                    intense = WINTENSE;
                else if (acolor == RESET) {
                    intense = WBLACK;
                    wcolor  = icolor;
                }
                else if (BLACK < acolor && acolor <= WHITE)
                    wcolor = ansi2win[acolor - 30];
                else if (acolor == 90) {
                    /* special gray really white man */
                    wcolor  = WWHITE;
                    intense = WBLACK;
                }
                
                SetConsoleTextattribute(
                    (h == stdout) ?
                    GetStdHandle(STD_OUTPUT_HANDLE) :
                    GetStdHandle(STD_ERROR_HANDLE),
                    
                    wcolor | intense | (icolor & 0xF0)
                );
                colorpos =  1;
                state    = -1;
            }
        } else {
            fputc(*str, h);
        }
    }
    /* restore */
    SetConsoleTextAttribute(
        (h == stdout) ?
        GetStdHandle(STD_OUTPUT_HANDLE) :
        GetStdHandle(STD_ERROR_HANDLE),
        icolor
    );
}
#endif

/*
 * We use standard files as default. These can be changed at any time
 * with con_change(F, F)
 */
static con_t console;

/*
 * Enables color on output if supported.
 * NOTE: The support for checking colors is NULL.  On windows this will
 * always work, on *nix it depends if the term has colors.
 * 
 * NOTE: This prevents colored output to piped stdout/err via isatty
 * checks.
 */
static void con_enablecolor() {
    if (console.handle_err == stderr || console.handle_err == stdout)
        console.color_err = !!(isatty(STDERR_FILENO));
    if (console.handle_out == stderr || console.handle_out == stdout)
        console.color_out = !!(isatty(STDOUT_FILENO));
        
    #ifndef _WIN32
    {
        char buf[4] = {0, 0, 0, 0};
        
        /*
         * This is such a hack.  But I'm not linking in any libraries to
         * do this stupidity.  It's insane there is simply not a ttyhascolor
         * in unistd.h
         */
        FILE *tput = popen("tput colors", "r");
        if  (!tput) {
            /*
             * disable colors since we can't determine without tput
             * which should be guranteed on all *nix OSes
             */
            console.color_err = 0;
            console.color_out = 0;
            
            return;
        }
        
        /*
         * Handle to tput was a success, lets read in the amount of
         * color support.  It should be at minimal 8.
         */
        fread(buf, 1, sizeof(buf)-1, tput);
        
        if (atoi(buf) < 8) {
            console.color_err = 0;
            console.color_out = 0;
        }
        
        /*
         * We made it this far, which means we support colors in the
         * terminal.
         */
        fclose(tput);
    }
    #endif
}

/*
 * Does a write to the handle with the format string and list of
 * arguments.  This colorizes for windows as well via translate
 * step.
 */
static int con_write(FILE *handle, const char *fmt, va_list va) {
    int      ln;
    #ifndef _WIN32
    ln = vfprintf(handle, fmt, va);
    #else
    {
        char *data = NULL;
        ln   = _vscprintf(fmt, va);
        data = malloc(ln + 1);
        data[ln] = 0;
        vsprintf(data, fmt, va);
        if (GMQCC_IS_DEFINE(handle))
            ln = win_fputs(data, handle);
        else
            ln = fputs(data, handle);
        free(data);
    }
    #endif
    return ln;
}

/**********************************************************************
 * EXPOSED INTERFACE BEGINS
 *********************************************************************/

void con_close() {
    if (!GMQCC_IS_DEFINE(console.handle_err))
        fclose(console.handle_err);
    if (!GMQCC_IS_DEFINE(console.handle_out))
        fclose(console.handle_out);
}

void con_color(int state) {
    if (state)
        con_enablecolor();
    else {
        console.color_err = 0;
        console.color_out = 0;
    }
}

void con_init() {
    console.handle_err = stderr;
    console.handle_out = stdout;
    con_enablecolor();
}

void con_reset() {
    con_close();
    con_init ();
}

/*
 * This is clever, say you want to change the console to use two
 * files for out/err.  You pass in two strings, it will properly
 * close the existing handles (if they're not std* handles) and
 * open them.  Now say you want TO use stdout and stderr, this
 * allows you to do that so long as you cast them to (char*).
 * Say you need stdout for out, but want a file for error, you can
 * do this too, just cast the stdout for (char*) and stick to a
 * string for the error file.
 */
int con_change(const char *out, const char *err) {
    con_close();
    
    if (GMQCC_IS_DEFINE((FILE*)out)) {
        console.handle_out = (((FILE*)err) == stdout) ? stdout : stderr;
        con_enablecolor();
    } else if (!(console.handle_out = fopen(out, "w"))) return 0;
    
    if (GMQCC_IS_DEFINE((FILE*)err)) {
        console.handle_err = (((FILE*)err) == stdout) ? stdout : stderr;
        con_enablecolor();
    } else if (!(console.handle_err = fopen(err, "w"))) return 0;
    
    return 1;
}

int con_verr(const char *fmt, va_list va) {
    return con_write(console.handle_err, fmt, va);
}
int con_vout(const char *fmt, va_list va) {
    return con_write(console.handle_out, fmt, va);
}

int con_err(const char *fmt, ...) {
    va_list  va;
    int      ln = 0;
    va_start(va, fmt);
    con_verr(fmt, va);
    va_end  (va);
    return   ln;
}
int con_out(const char *fmt, ...) {
    va_list  va;
    int      ln = 0;
    va_start(va, fmt);
    con_vout(fmt, va);
    va_end  (va);
    return   ln;
}
