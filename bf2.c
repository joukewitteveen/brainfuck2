#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

#define E_INPUT  1
#define E_MEMORY 2
#define E_SEEK   3
#define E_SYNTAX 4

typedef unsigned char cell_t;

enum op {
  LEFT  = '<',
  RIGHT = '>',
  PLUS  = '+',
  MINUS = '-',
  IN    = ',',
  OUT   = '.',
  TWO   = '2',
  BEGIN = '[',
  END   = ']'
};

struct page {
  struct page *l, *r;
  size_t n;
  cell_t *m;
};

struct {
  cell_t *ptr, *low, *high;
  struct page *page;
  enum op last;
  unsigned int rep;
  int ( *_op )( enum op ),
      ( *_loop )( enum op, FILE* );
  struct {
    size_t size;
    size_t head;
    fpos_t *data;
  } stack;
} state;

int exec( enum op );
int loop( enum op, FILE* );


struct page *new_page( size_t n ){
  struct page *page = malloc( sizeof( struct page ) );
  if( page == NULL ){
    return NULL;
  } //if
  page->m = malloc( n * sizeof( cell_t ) );
  if( page->m == NULL ){
    free( page );
    return NULL;
  } //if
  page->n = n;
  return page;
} //new_page


int nop( enum op op ){
  return 0;
} //nop


int exec( enum op op ){
  static int rep, ret;

  switch( op ){
    case LEFT:
      if( state.ptr == state.page->m ){
        if( state.page->l == NULL ){
          state.page->l = new_page( 2 * state.page->n );
          if( state.page->l == NULL ){
            return E_MEMORY;
          } //if
          state.page->l->r = state.page;
          state.page->l->l = NULL;
          state.low = state.page->l->m + state.page->l->n - 1;
          *state.low = 0;
        } //if
        state.page = state.page->l;
        state.ptr = state.page->m + state.page->n - 1;
      } else {
        if( state.ptr-- == state.low ){
          *--state.low = 0;
        } //if
      } //if
      break;
    case RIGHT:
      if( state.ptr == state.page->m + state.page->n - 1 ){
        if( state.page->r == NULL ){
          state.page->r = new_page( 2 * state.page->n );
          if( state.page->r == NULL ){
            return E_MEMORY;
          } //if
          state.page->r->l = state.page;
          state.page->r->r = NULL;
          state.high = state.page->r->m;
          *state.high = 0;
        } //if
        state.page = state.page->r;
        state.ptr = state.page->m;
      } else {
        if( state.ptr++ == state.high ){
          *++state.high = 0;
        } //if
      } //if
      break;
    case PLUS:
      ++*state.ptr;
      break;
    case MINUS:
      --*state.ptr;
      break;
    case IN:
      *state.ptr = getchar();
      break;
    case OUT:
      putchar( *state.ptr );
      break;
    case TWO:
      if( state.rep < 1 ){
        return E_SYNTAX;
      } //if
      for( rep = state.rep; rep; --rep ){
        ret = exec( state.last );
        if( ret != 0 ){
          return ret;
        } //if
      } //for
      return 0;
  } //switch

  if( state.last == op ){
    ++state.rep;
  } else {
    state.last = op;
    state.rep = 1;
  } //if
  return 0;
} //exec


int skip( enum op op, FILE *fp ){
  static int level = 0;

  switch( op ){
    case BEGIN:
      ++level;
      return 0;
    case END:
      if( level > 0 ){
        --level;
        return 0;
      } //if
  } //switch
  --state.stack.head;
  state._op = &exec;
  state._loop = &loop;
  return 0;
} //skip


int loop( enum op op, FILE *fp ){
  state.rep = 0;

  switch( op ){
    case BEGIN:
      if( state.stack.head + 1 == state.stack.size ){
        state.stack.size *= 2;
        state.stack.data = realloc( state.stack.data,
                                    state.stack.size * sizeof( fpos_t ) );
        if( state.stack.data == NULL ){
          return E_MEMORY;
        }
      } //if
      if( fgetpos( fp, state.stack.data + state.stack.head++ ) != 0 ){
        return E_SEEK;
      } //if
      break;
    case END:
      if( state.stack.head == 0 ){
        return E_SYNTAX;
      } //if
      if( fsetpos( fp, state.stack.data + state.stack.head - 1 ) != 0 ){
        return E_SEEK;
      } //if
  } //switch
  if( *state.ptr == 0 ){
    state._op = &nop;
    state._loop = &skip;
  } //if
  return 0;
} //loop


int main( int argc, char *argv[] ){
  int c, stop = 0;
  FILE *fp;
  struct termios orig_tio, unbuf_tio;

  if( argc != 2 ){
    printf( "Usage: %s <filename>\n", argv[0] );
    return 1;
  } //if

  fp = fopen( argv[1], "r" );
  if( fp == NULL ){
    printf( "Error: could not open '%s'\n", argv[1] );
    return 2;
  } //if

  state.page = new_page( 1 );
  state.page->l = NULL;
  state.page->r = NULL;
  state.ptr = state.page->m;
  state.low = state.ptr;
  state.high = state.ptr;
  *state.ptr = 0;
  state.last = END;
  state.rep = 0;
  state.stack.data = malloc( sizeof( fpos_t ) );
  state.stack.size = 1;
  state.stack.head = 0;
  state._op = &exec;
  state._loop = &loop;

  tcgetattr( STDIN_FILENO, &orig_tio );
  unbuf_tio = orig_tio;
  unbuf_tio.c_lflag &= ( ~ICANON & ~ECHO );
  tcsetattr( STDIN_FILENO, TCSANOW, &unbuf_tio );

  do {
    c = fgetc( fp );
    switch( c ){
      case LEFT:
      case RIGHT:
      case PLUS:
      case MINUS:
      case IN:
      case OUT:
      case TWO:
        stop = state._op( c );
        break;
      case BEGIN:
      case END:
        stop = state._loop( c, fp );
        break;
      case ' ':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
      case '\v':
        break;
      case EOF:
        stop = E_INPUT;
        break;
      default:
        do {
          c = fgetc( fp );
        } while( c != '\n' && c != EOF );
    } //switch
  } while( stop == 0 );

  tcsetattr( STDIN_FILENO, TCSANOW, &orig_tio );

  switch( stop ){
    case E_MEMORY:
      puts( "Error: could not allocate memory" );
      return 1;
    case E_SEEK:
      puts( "Error: file position indicator not available" );
      return 1;
    case E_SYNTAX:
      printf( "Error: invalid syntax near '%c'\n", c );
      return 1;
    case E_INPUT:
      if( feof( fp ) == 0 ){
        puts( "Error: reading failed" );
        return 1;
      } //if
      if( state.stack.head != 0 ){
        printf( "Error: missing '%c'\n", END );
        return 1;
      } //if
  } //switch
  return 0;
} //main
