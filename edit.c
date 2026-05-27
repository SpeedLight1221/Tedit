#include <bits/types/error_t.h>
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/errno.h>
#include <stdarg.h>
#include <time.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <stdbool.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
#define TAB_STOP 8

// #region Data
enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN

};

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig {
  int cx, cy;
  int rx;
  struct termios orig_termios;
  int screenrows;
  int screencols;
  int rowoffset;
  int coloffset;
  int numrows;
  int dirty;
  int uilinecount;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  erow *row;

  bool searchOpen;
  erow *searchRow;
  int sx;


};

typedef struct WindowData {
  int aX, aY;
  int width, height;

} WindowData;

struct editorConfig E;
// #endregion

// #region prototypes
void setStatusMessage(const char *fmt,...);
// #endregion

// #region Terminal
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}
void disableRawTerminal() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcsetattr");
  }
}
void enanableRawTerminal() {

  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
    die("tcgetattr");
  }
  atexit(disableRawTerminal);
  struct termios raw = E.orig_termios;

  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  }
}

int readKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (c == '\x1b') { // If the read char is \x1b, its an escape sequence,
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) !=
        1) // try to read first char of the sequence, if nothing is read, \x1b
           // is returned alone (== ESC key)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) // second char of sequnce
      return '\x1b';

    if (seq[0] == '[') { // first char is a bracket == ANSI escape sequence
      if (seq[1] >= '0' &&
          seq[1] <= '9') { // whether its numerical (VT) sequence  or not
        if (read(STDIN_FILENO, &seq[2], 1) != 1) // attempt reading third char
          return '\x1b';
        if (seq[2] == '~') { // Third char is a tilde, which terminates the seq
                             // == the sequence only consists of a single digit.
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else { // a Xterm sequnce
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      { // first char is O, also used by some terminal emulators / OS
        switch (seq[1]) {
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    }

    return '\x1b'; // if the sequence isnt handled, return ESC
  } else {         // not a Sequence, return the character
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {

  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;

    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}
// #endregion

// #region operations

int rowCxtoRx(erow *row, int cx){
  int rx=0;
  int j;
  for(j = 0;j <cx; j++){
    if(row->chars[j]== '\t')
      rx += (TAB_STOP-1)-(rx % TAB_STOP);
    rx++;
  }
  return rx;
}
void updateRow(erow *row){
  int tabs =0;
  int j;
  for(j = 0; j < row->size;j++){
    if(row->chars[j]== '\t') tabs++;
  }


  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP-1)+ 1);

 
  int idx =0;
  for(j = 0; j < row->size; j++){
    if(row->chars[j] =='\t'){
      row->render[idx++] = ' ';
      while (idx  % TAB_STOP !=0) {
        row->render[idx++] = ' ';
      }
    }
    else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}


void addCharToRow(erow *row, int chr, int at){
  if(at < 0 || at > row->size ) at = row->size;
  row->chars = realloc(row->chars, row->size+2);
  memmove(&row->chars[at+1], &row->chars[at], row->size-at+1);
  row->size++;
  row->chars[at] = chr;
  updateRow(row);
}


void appendRow(char *s, size_t len) {
  erow *new = realloc(E.row, sizeof(erow)*(E.numrows+1));
  if(new == NULL){
    setStatusMessage("Realloc failed at row append");
    return;
  }
  else {
    E.row = new;
  }


  int at = E.numrows;
  E.row[at].size =len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  updateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void rowInsertCharacter(int c) {
  erow *curent;
  int at = 0;
  if (E.searchOpen) {
    curent = E.searchRow;
    at = E.sx;
  }
  else {
    curent = &E.row[E.cy];
    at = E.cx;
  }

 

  addCharToRow(curent, c, at);
  if(!E.searchOpen){
    E.dirty++;
  }
 
}

void deleteChar(erow *row, int at){
  //probably just ignore reallocation for now
  memmove(&row->chars[at], &row->chars[at+1], row->size-at);
  row->size--;
  updateRow(row);
  E.dirty++;
}


void shiftRowsUp(int toRow){
  
  for(int i= toRow; i < E.numrows-1; i++){
    E.row[i] = E.row[i+1];
    updateRow(&E.row[i]);
  }
  erow *newp = realloc(E.row, sizeof(erow)*(E.numrows-1));
  if(newp == NULL){
    setStatusMessage("Realloc failed at shifting rows up: %s", strerror(errno));
    return;
  }
  else {
    E.row = newp;
    E.numrows--;
  }
}

int mergeRows(int dest, int src){
  erow *destRow = &E.row[dest];
  erow *sourceRow = &E.row[src];

  int oldDestSize = destRow->size;
  int newsize =  sourceRow->size+ destRow->size;
  char temp[newsize+1];
  memcpy(temp, destRow->chars, destRow->size);
  memcpy(temp+destRow->size, sourceRow->chars, sourceRow->size);
  temp[newsize] = '\0';

  char *newp =realloc(destRow->chars, newsize+1);
  if(newp == NULL){
    setStatusMessage("Row Merge Realloc failed");
    return -1;
  }
  destRow->chars = newp;
  memcpy(destRow->chars, temp, newsize+1);
  destRow->size = newsize;
  updateRow(destRow);
  shiftRowsUp(src);
  return oldDestSize;

}

void rowDeleteChar(int at,int row){
  erow *current = &E.row[row];

  if(at==-1){
    if(row == 0){
      return;
    }
    int mergePoint = mergeRows( row-1,row);

    E.cy--;
    E.cx = mergePoint;
   

  }
  else if(at == current->size){
    if(row == E.numrows){
      return;
    }
    mergeRows( row,row+1);


  }
  else {
    //delete char
    deleteChar(current, at);
  }

}
int tse =0;
void shiftRowsDown(int fromRow){
 
  erow *newp = realloc(E.row, sizeof(erow)*(E.numrows+1));
  if(newp == NULL){
    setStatusMessage("Realloc failed at shifting rows down: %s", strerror(errno));
    return;
  }
  else {  
    E.row = newp;
  }

  appendRow(E.row[E.numrows-1].chars, E.row[E.numrows-1].size); // create a copy of the last line
  
  for(int i = (E.numrows-2); i > fromRow; i--){
    
    E.row[i].size = E.row[i-1].size;
    free(E.row[i].chars);
    E.row[i].chars = malloc(E.row[i].size+1 ); 
    memcpy(E.row[i].chars, E.row[i-1].chars, E.row[i].size );
    updateRow(&E.row[i]); 
  }
}


void insertRow( int atChar, int linenum){

  int to = linenum; //when shifting rows down, it stops here (either where the new line was created or one below)
  if(atChar != 0) to +=1;

  shiftRowsDown(to);

  if(atChar == 0){
    E.row[to].size = 0;
    free(E.row[to].chars);
    E.row[to].chars = malloc(1 ); 
    E.row[to].chars[0] = '\0';
    updateRow(&E.row[to]);

    return;
  }

  int splicePoint = E.row[linenum].size-atChar;

  E.row[to].size = splicePoint;
  free(E.row[to].chars);
  E.row[to].chars = malloc(sizeof(char)*(splicePoint+1));
  memcpy(E.row[to].chars, E.row[linenum].chars+atChar, splicePoint*sizeof(char));
  E.row[to].chars[splicePoint] = '\0';
  updateRow(&E.row[to]);


  char temp[atChar+1];
  memcpy(temp, E.row[linenum].chars,atChar );
  temp[atChar] = '\0';
  E.row[linenum].size = atChar;
  free(E.row[linenum].chars);
  E.row[linenum].chars = malloc(sizeof(char)*(atChar+1));
  memcpy( E.row[linenum].chars,temp,atChar+1 );
  updateRow(&E.row[linenum]);
  E.dirty++;




  /*int copylen = E.row[to].size - atChar;
  if(copylen == E.row[to].size){
    copylen = 0;
  }
 

  E.row[to].size = copylen;
  free(E.row[to].chars);
  E.row[to].chars = malloc(copylen+1);
  strncpy(E.row[to].chars, E.row[to-1].chars+atChar, copylen);
  E.row[to].chars[copylen] = '\0';
  updateRow(&E.row[to]);




  int newlen = E.row[to-1].size-copylen;
  E.row[to-1].size = newlen;
  char temps[newlen+1];
  strncpy(temps, E.row[to-1].chars, newlen);
  temps[newlen] = '\0';
  free(E.row[to-1].chars);
  E.row[to-1].chars = malloc(newlen+1);
  strncpy(E.row[to-1].chars, temps, newlen+1);
  updateRow(&E.row[to-1]);

      E.cx = 0;
  E.cy++;*/


  

}

void insertNewLine(){

  if(E.searchOpen){
    return;
  }
  insertRow( E.cx,E.cy);
  E.cy++;
  E.cx =0;



}
// #endregion


// #region file IO


char *erowsToString(int *buflen){
  int len = 0;
  int j;
  for(j =0; j < E.numrows; j++){
    len += E.row[j].size+1;
  }
  *buflen = len;

  char *buff = malloc(len);
  char *p = buff; //will point to the end of the buffer
  for(j =0; j < E.numrows; j++){
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;

  }

  return buff;

}

void editorSave(){
  if(E.filename == NULL) return;

  int len;
  char *buff = erowsToString(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644); //opens file, creates if it doesnt exit and sets its flags to rw to owner, r to others
  if(fd != -1){
    if(ftruncate(fd, len)!=-1){
      if( write(fd, buff, len) == len){
        close(fd);
        free(buff);
        setStatusMessage("%d bytes written to the disk", len);
        E.dirty = 0;
        return;
      }
    }
    close(fd);
  }
    free(buff);
    setStatusMessage("Save Failed, I/O error %s", strerror(errno));
  

  
}


void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;  // will point to the line read by getline
  size_t linecap = 0; // will be set to the amount of memory allocated by getline
  ssize_t linelen; // will be set to the lenght of the line read by getline
  while ((linelen = getline(&line, &linecap, fp)) !=-1 ) {
   
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--; }
    appendRow(line, linelen);
   
  }

  free(line);
  fclose(fp);
    E.dirty = 0;
}
// #endregion
// #region Apend Buffer
struct abuf {
  char *b;
  int len;
};

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;

}

void abStrip(struct abuf *ab,int len){
  char *new = realloc(ab->b, ab->len-len);

  if(new == NULL){
    setStatusMessage("Ab strip failed", strerror(errno));
    return;
  }
  

  ab->b = new;
  ab->len -=len;
   

}

void abFree(struct abuf *ab) { free(ab->b); }

#define ABUF_INIT {NULL, 0}
// #endregion
// #region  Output

void Scroll(){
  E.rx = 0;
  if(E.cy < E.numrows){
    E.rx = rowCxtoRx(&E.row[E.cy], E.cx);
  }


  if(E.cy < E.rowoffset){ //cursors Y pos is less than the offset, ei we are scrolling up
    E.rowoffset = E.cy;
  }
  if(E.cy >= E.rowoffset+ E.screenrows){// cursors Y pos is more than the offset+ number of rows , ei we are scrolling down
    E.rowoffset = E.cy-E.screenrows+1;
  }
  if(E.rx < E.coloffset){
    E.coloffset = E.rx;
  }
  if(E.rx > E.coloffset + E.screencols){
    E.coloffset = E.rx - E.screencols+1;
  }


}

void setStatusMessage(const char *fmt,...){
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void drawUIRows(struct abuf *ab){
  abAppend(ab, "\x1b[7m", 4);
  char lbuff[80],rbuff[80];
  int llen = snprintf(lbuff , sizeof(lbuff),"CX: %d CY: %d RX: %d Rows: %d RL: %d RS: %zu T: %d",E.cx,E.cy, E.rx, E.numrows, E.row[E.cy].size,sizeof(E.row[E.cy].chars),tse);
  int rlen = snprintf(rbuff, sizeof(rbuff),"%c%.30s", E.dirty==0 ? ' ' : '*' ,E.filename ? E.filename : "[No name]" );

  int space = E.screencols-(rlen+llen);
  abAppend(ab, lbuff, llen);
  for(int i =0; i < space; i++){
    abAppend(ab, " ", 1);
  }
  abAppend(ab, rbuff, rlen);

  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);

  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if(msglen > E.screencols) msglen = E.screencols;
  if(msglen && time(NULL) - E.statusmsg_time <5){
    abAppend(ab, E.statusmsg, msglen);
  }
 
 
 
}

struct tWin{
  int anchY;
  int anchX;
  int height;
  int width;
};

struct tWin searchWin = {1,20,4,20};

void test(struct abuf *ab, int rowlen,int currentRow){

  if(!E.searchOpen) return;
  
  struct tWin testwin = searchWin;
  
  if(currentRow < testwin.anchY || currentRow >= testwin.anchY+testwin.height){ //not on a row occupied by a window
    return;
  }

  if(testwin.anchX+testwin.width > E.screencols){ //window outside the screen
    return;
  }

  int offset = testwin.anchX-rowlen;
  if(rowlen >= testwin.anchX) //window would overlap -tbc
  {
    int overlap = rowlen-testwin.anchX;
    setStatusMessage("overlap %d",overlap);

    abStrip(ab, overlap);
    rowlen -= overlap;
    offset = 0;

    //return;
  }



  if(offset > 0){
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%dC", offset);
    abAppend(ab, buf, strlen(buf));
  }
  

  
 
 
  abAppend(ab, "\x1b[7m", 4);// invert colors

  if(currentRow == testwin.anchY || currentRow == testwin.anchY+testwin.height-1)
  {
    char border[testwin.width];
    for(int i = 0; i < testwin.width;i++){
      border[i] = '/';
    }
    abAppend(ab, border, testwin.width);
  }
  else if(currentRow == testwin.anchY+1){
    abAppend(ab, "/", 1);
    abAppend(ab, "\x1b[m", 3);// reset colors

    int len = E.searchRow->rsize;
    if( len>0){

      if(len > testwin.width-2) len =testwin.width-2;

      abAppend(ab, E.searchRow->render, len);
    }
    abAppend(ab, "\x1b[7m", 4);// invert colors
    char spbuf[32];
    snprintf(spbuf, sizeof(spbuf), "\x1b[%dC", testwin.width-2-len);
    abAppend(ab, spbuf, strlen(spbuf));
    abAppend(ab, "/", 1);

  }
  else {
    abAppend(ab, "/", 1);
    char spbuf[32];
    snprintf(spbuf, sizeof(spbuf), "\x1b[%dC", testwin.width-2);
    abAppend(ab, spbuf, strlen(spbuf));
    abAppend(ab, "/", 1);

  }
 
  abAppend(ab, "\x1b[m", 3);// reset colors

  char sbuf[32];
  snprintf(sbuf, sizeof(sbuf), "\x1b[%dD", offset);
  abAppend(ab, sbuf, strlen(sbuf));

}


  void openSearch()
  {
    E.searchOpen = true;
    E.sx = 0;
    E.searchRow = malloc(sizeof(erow));
    E.searchRow->chars = malloc(sizeof(char));
    E.searchRow->chars[0] = '\0';
    E.searchRow->render = NULL;
    E.searchRow->rsize = 0;
    E.searchRow->size = 0;
   
    

  }

void drawRows(struct abuf *ab) {
  int y;

  for (y = 0; y < E.screenrows; y++) {
    int filerow = y +E.rowoffset;
    int len;
    if (filerow >= E.numrows) {

      abAppend(ab, "~", 1);
      len = 1;

    } else {

      len = E.row[filerow].rsize -E.coloffset;
      if(len < 0) len =0;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloffset], len);
    }
    abAppend(ab, "\x1b[K", 3); // Erase in line, args: 0 (def) - erase to the right of the cursor

    test(ab,len,y);
    
    abAppend(ab, "\r\n", 2);
    
  }
  

}


void refreshScreen() {
  Scroll();
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  drawRows(&ab);
  drawUIRows(&ab);

  char buf[32];

  int x,y;
  if(E.searchOpen){
    x = E.sx + searchWin.anchX+2;
    y = searchWin.anchY+2;
  }
  else {
     x=(E.rx - E.coloffset )+ 1;
     y=(E.cy - E.rowoffset)+1;
  }

  
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y, x);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}
// #endregion

// #region Windows




// #endregion

// #region  Input

void MoveCursor(int key) {
  erow *row;
  int rowSize;
  int *x;
  int *y;

  if(E.searchOpen){
    row = E.searchRow;
    x = &E.sx;
    y = &E.cy;
    rowSize = E.searchRow->size;
   
  }
  else {
    row= (E.cy >= E.numrows ) ? NULL : &E.row[E.cy];
    x = &E.cx;
    y = &E.cy;
    rowSize = E.row[E.cy].size;
  }

//causes seg fault, idk why something with accessing the variable x/y
  //printf("adr %p",(void *)x);
  //printf("con %d",*x);
   
  switch (key) {
  case ARROW_LEFT:
    
    if (*x != 0) {
     
      *x-=1;

    } else if (*y >0 && !E.searchOpen){

      *y -=1;
      *x = rowSize;

    
    }
    break;
  case ARROW_RIGHT:
    if (row && *x < row->size ) {
      *x+=1;
    }else if (row && *x == row->size && *y < E.numrows-1 &&!E.searchOpen) {
      *y+=1;
      *x =0;
    }
    break;
  case ARROW_UP:
    if (y != 0 && !E.searchOpen) {
      *y-=1;
    }
    break;
  case ARROW_DOWN:
    if (*y < E.numrows-1 &&!E.searchOpen) {
      *y+=1;
    }
    break;
  }

  if(E.searchOpen == false){
    row = (E.cy >= E.numrows ) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if(E.cx > rowlen){
      E.cx = rowlen;
    }
  }
  
}

void processKeypress() {
  int c = readKey();

  switch (c) {
  case '\r':
    insertNewLine();
    break;
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    if(E.searchOpen){
      printf("sr %s",E.searchRow->chars);
    }
    exit(0);
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case HOME_KEY:
    //E.cx = 0;
   
    
    break;
  case END_KEY:
  
    if(E.cy <E.numrows){
      E.cx = E.row[E.cy].size;
    }
    break;
  case PAGE_DOWN:
  case PAGE_UP: {
    if (c == PAGE_UP) {
          E.cy = E.rowoffset;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoffset + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }
        int times = E.screenrows;
        while (times--)
          MoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case DEL_KEY:
  if(E.cy == E.numrows-1 && E.cx == E.row[E.numrows-1].size && !E.searchOpen){
    return;
  }
  rowDeleteChar(E.cx, E.cy);
  break;
  case BACKSPACE:
    rowDeleteChar(E.cx-1, E.cy);
    if(E.cx >0 && !E.searchOpen){
      E.cx--;
    }
  break;
  case CTRL_KEY('h'):
  openSearch();
  break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_RIGHT:
  case ARROW_LEFT:
    MoveCursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b':
  break; 
  default:
    rowInsertCharacter(c);
    break;
  }
  
}
// #endregion
// #region  init

void initEditor() {

  E.uilinecount = 2;
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.numrows = 0;
  E.rowoffset =0;
  E.coloffset = 0;
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.dirty =0;

  E.searchOpen = false;
  E.sx =0;
  

  

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");

  E.screenrows -= E.uilinecount;
}

int main(int argc, char *argv[]) {
  enanableRawTerminal();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  else {
    appendRow("",0);
  }
  setStatusMessage("HELP : Ctrl-Q to exit, Ctrl-S to Save");
  while (1) {
    refreshScreen();
    processKeypress();
  }
  

  return 0;
}

// #endregion

// todo : pressing enter at a newly created line causes a segfaul if done immidiately after creation, or looses a couple of charsif not imiidiately

