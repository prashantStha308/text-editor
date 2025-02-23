/*** Includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** Defines ***/
// sets upper 3 bits to 0
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

enum editorKey{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    // <esc>[3~
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    // <esc>[5~
    PAGE_UP,
    // <esc>[6~
    PAGE_DOWN
};

/*** Datas ***/
typedef struct erows{
    int size;
    char *chars;
} erows;

struct editorConfig{
    int cx , cy;
    // row offset
    int rowoff;
    // cols offset
    int coloff;
    int screenRows;
    int screenCols;
    int numrows;
    erows *row;
    struct termios orign_termios;
};

struct editorConfig E;

/*** Terminal ***/
void die( char *s ){

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write( STDOUT_FILENO , "\x1b[H" , 3 );

    // perror() looks at global errno variable(look it up) prints a descriptive message for it.
    // most libraries sets errno variable when error occures
    perror(s);
    exit(1);
}

void disableRawMode(){
    // tcsetattr returns -1 if error has occured
    if( tcsetattr( STDIN_FILENO , TCSAFLUSH , &E.orign_termios ) == -1 ){
        die("tcsetattr");
    }
}

void enableRawMode(){

    // tcgetattr returns -1 if error has occured
    if( tcgetattr( STDIN_FILENO , &E.orign_termios ) == -1 ){
        die("tcgetattr");
    }
    // Executes disableRawMode() when the program exits
    atexit(disableRawMode);

    struct termios raw = E.orign_termios;
    tcgetattr( STDIN_FILENO , &raw );

    // c.'i'flag means input flags
    // IXON is a input flag, both XON and XOFF are combined in IXON.
    // CTRL + Q = XON, resumes data transmission
    // CTRL + S = XOFF, pauses data transmissiobn
    // ---------
    // ANYTHING EXCEPT IXON AND ICRNL are just for convention and doesn't contribute much.
    // ---------
    raw.c_iflag &= ~( BRKINT | INPCK | ISTRIP | ICRNL | IXON );

    // raw.'o'flag means output flags
    // OPOST here turns ("\r\n") to just ("\n") while pressing enter.
    raw.c_oflag &= ~( OPOST );
    // No idea what this does. THis is just for convention
    raw.c_cflag |= (CS8);

    // c_'l'flag stands for localflag.
    // ~ is NOT FLag, just like && is AND and || is OR flags
    // ECHO makes it possible to display what we press in keyboard into terminal.
    // Here we are disabeling ECHO
    // ICANON is for canonical mode
    // ISIG is to disable commands sent by CTRL+Z and CTRL+C
    raw.c_lflag &= ~( ECHO | ICANON | ISIG | IEXTEN );

    // raw.c_'cc' stands for control characters
    // VMIN sets minimum amount of bytes required to read()
    // VTIME sets the max time to wait before read(). The time is in 10th of a second. So, 1 = 100 miliseconds
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if( tcsetattr( STDIN_FILENO , TCSAFLUSH , &raw ) == -1 ){
        // call die() if error
        die("tcsetattr");
    }
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if( c == '\x1b' ){
        char seq[3];

        if( read( STDIN_FILENO , &seq[0] , 1 ) != 1 ) return '\x1b';
        if( read( STDIN_FILENO , &seq[1] , 1 ) != 1 ) return '\x1b';

        if( seq[0] == '[' ){

            if( seq[0] >= '0' && seq[1] <= '9' ){
                if( read( STDIN_FILENO , &seq[2] , 1 ) != 1 ) return '\x1b';
                if( seq[2] == '~' ){
                    switch( seq[1] ){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }else{
                switch( seq[1] ){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }else if( seq[0] == '0' ){
            switch( seq[1] ){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }else{
        return c;
    }
}

int getCursorPosition( int *rows , int *cols ){

    char buf[32];
    unsigned int i = 0;

    if( write( STDOUT_FILENO , "\x1b[6n", 4 ) != 4 ) return -1;

    printf("\r\n");
    char c;
    while( i < sizeof(buf) - 1 ){
        if( read( STDIN_FILENO , &buf[i] , 1 ) != 1 ) break;
        if( buf[i] == 'R' ) break;
        i++;
    }
    buf[i] = '\0';

    if( buf[0] != '\x1b' || buf[1] != '[' ) return -1;
    if( sscanf( &buf[2], "%d;%d" , rows , cols ) != 2 ) return -1;

    editorReadKey();

    return 0;
}

int getWindowSize( int *rows , int *cols ){
    struct winsize ws;

    // ioctl places number of rows and colums in the winsize struct variable on success.
    // On failure it return -1
    // TIOCGWINSZ stands for: TIOCGWINSZ request. (As far as I can tell, it stands for Terminal IOCtl (which itself stands for Input/Output Control) Get WINdow SiZe.)
    // ioctl is not garunteed, hence we also use a fallback method
    if( ioctl( STDIN_FILENO , TIOCGWINSZ , &ws ) == -1 || ws.ws_col == 0 ){

        // Here The C command moves the cursor right and B command moves cursor down.
        // both have 999 as arguments, which ensures that the cursor is on bottom right of terminal
        if( write( STDOUT_FILENO , "\x1b[999C\x1b[999B" , 12 ) != 12 ) return -1;
        return getCursorPosition(  rows , cols );
    }else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** Row Operations ***/
void editorAppendRow( char *s , size_t len ){
    E.row = realloc( E.row , sizeof(erows) * ( E.numrows + 1 ) );

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc( len + 1 );
    memcpy( E.row[at].chars , s , len );
    E.row[at].chars[len]  ='\0';
    E.numrows++;

}


/*** File I/O ***/
void editorOpen( char *filename ){

    FILE *fp = fopen( filename , "r" );
    if( !fp ) die("fopen");

    char *line = NULL;
    // look into size_t and ssize_t
    size_t linecap = 0;
    ssize_t linelen;
    linelen = getline( &line , &linecap , fp );

    while( ( linelen = getline( &line , &linecap , fp ) ) != -1 ){
        while( linelen > 0 && ( line[linelen - 1] == '\r' || line[linelen - 1] == '\r' ) ) linelen--;
        editorAppendRow( line , linelen );
    }

    free(line);
    fclose(fp);
}

/*** append buffer ***/
struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT { NULL , 0 };

void abAppend( struct abuf *ab , char *s , int len ){
    char *new = realloc( ab->b , ab->len + len );
    if( new == NULL ) return;
    memcpy( &new[ab->len] , s , len );
    ab->b = new;
    ab->len += len;
}

void abFree( struct abuf *ab ){
    free( ab->b );
}

/*** Inputs ***/
void editorMoveCursor( int key ){
    switch(key){
        case ARROW_LEFT:
            if( E.cx != 0 ){
                E.cx--;
            }
            break;

        case ARROW_RIGHT:
                E.cx++;
            break;

        case ARROW_UP:
            if( E.cy != 0 ){
                E.cy--;
            }
            break;

        case ARROW_DOWN:
            if( E.cy < E.numrows ){
                E.cy++;
            }
            break;
    }
}

void editorProcessKeypress(){

    int c = editorReadKey();

    switch(c){
        case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write( STDOUT_FILENO , "\x1b[H" , 3 );
        exit(0);
        break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
        E.cx = E.screenCols - 1;
        break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenRows;
                while( times-- ){
                    editorMoveCursor( c == PAGE_UP ? ARROW_UP : ARROW_DOWN );
                }
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** Output ***/
void editorScroll(){
    if( E.cy < E.rowoff ){
        E.rowoff = E.cy;
    }
    if( E.cy >= E.rowoff + E.screenRows ){
        E.rowoff = E.cy - E.screenRows + 1;
    }

    if( E.cx < E.coloff ){
        E.coloff = E.cx;
    }
    if( E.cx >= E.coloff + E.screenCols ){
        E.coloff = E.cx - E.screenCols + 1;
    }
}

void editorDrawRows( struct abuf *ab ){
    int y;
    for( y =  0; y < E.screenRows ; y++ ){
        int filerow = y + E.rowoff;
        if( filerow >= E.numrows ){
            if( E.numrows == 0 && y == E.screenRows/3 ){
                char welcome[80];
                int welcomelen = snprintf( welcome , sizeof(welcome) , "Kilo editor -- Version %s" , KILO_VERSION );
                if( welcomelen > E.screenCols ) welcomelen = E.screenCols;

                int padding = ( E.screenCols - welcomelen ) / 2;
                if( padding ){
                    abAppend( ab , "~" , 1 );
                    padding--;
                }
                while( padding-- ) abAppend( ab , " " , 1 );
                abAppend( ab , welcome , welcomelen );
            }else{
                abAppend( ab , "~" , 1 );
            }
        }else{
            int len = E.row[filerow].size - E.coloff ;
            if( len < 0 ) len = 0;
            if( len > E.screenCols ) len = E.screenCols;
            abAppend( ab , &E.row[filerow].chars[E.coloff] , len );
        }

        // The K commands erases part of current line.
        abAppend( ab , "\x1b[K",3 );

        if( y < E.screenRows - 1 ){
            abAppend( ab , "\r\n" , 2 );
        }
    }
}


void editorRefreshScreen(){
    
    editorScroll();

    struct abuf ab = ABUF_INIT;
    // The l is used to RESET MODE, or turn off various terminal features
    abAppend( &ab , "\x1b[?25l" , 6 );
    // The H command position the cursor.
    // The default arguement is 1;1, that is 1st row and 1st column. The ; sign is used to give multiple arguements
    abAppend( &ab , "\x1b[H" , 3 );

    editorDrawRows(&ab);

    char buf[32];
    snprintf( buf , sizeof(buf) , "\x1b[%d;%dH" , ( E.cy - E.rowoff ) + 1 ,( E.cx - E.coloff ) + 1 );
    abAppend( &ab , buf , strlen(buf) );
    // The h is used to SET MODE, or turn on various terminal features
    abAppend( &ab , "\x1b[?25h" , 6 );

    write( STDOUT_FILENO , ab.b , ab.len );
    abFree(&ab);
}

/*** Init ***/

void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.row = NULL;

    if( getWindowSize( &E.screenRows , &E.screenCols ) == -1 ) die("getWindowSize");
}

int main( int argc , char *argv[] ){
    enableRawMode();
    initEditor();
    if( argc >= 2 ){
        editorOpen( argv[1] );
    }

    while( 1 ){
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}