#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pg_game.h"
#include "utils.h"

typedef enum {
  TOKEN_EOF,
  TOKEN_NATURAL,
  TOKEN_KW_PARITY,
  TOKEN_COMMA,
  TOKEN_SEMICOLON,
  TOKEN_QUOTED_NAME,
  TOKEN_ERROR,
} TokenKind;

typedef struct {
  TokenKind kind;
  uint64_t natural;
  size_t begin;
  size_t length;
  size_t line;
  size_t column;
} Token;

typedef struct {
  char *text;
  size_t length;
  size_t offset;
  size_t line;
  size_t column;
  PGParseError *error;
} Lexer;

typedef struct {
  uint64_t external_id;
  size_t line;
  size_t column;
} TempSuccessor;

typedef struct {
  uint64_t external_id;
  uint64_t priority;
  PGPlayer owner;
  char *name;
  TempSuccessor *successors;
  size_t successor_count;
  size_t successor_capacity;
  size_t source_order;
  size_t line;
  size_t column;
} TempDeclaration;

typedef struct {
  Lexer lexer;
  Token token;
  TempDeclaration *declarations;
  size_t declaration_count;
  size_t declaration_capacity;
  bool has_header;
  uint64_t header_maximum;
} Parser;

static void set_error(PGParseError *error, size_t const line,
                      size_t const column, char const *message) {
  if (error == nullptr) {
    return;
  }
  error->line = line;
  error->column = column;

  (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

[[nodiscard]] static bool read_stream(FILE *input, char **text, size_t *length,
                                      PGParseError *error) {
  char *result = nullptr;
  size_t used = 0;
  size_t capacity = 0;

  int byte = 0;
  while ((byte = fgetc(input)) != EOF) {
    if (used == capacity) {
      ArrayGrowth const growth = grow_array(result, capacity, sizeof(*result));
      if (!growth.succeeded) {
        free(result);
        set_error(error, 1, 1, "failed to allocate the input buffer");
        return false;
      }
      result = growth.data;
      capacity = growth.capacity;
    }
    result[used++] = (char)byte;
  }
  if (ferror(input)) {
    free(result);
    set_error(error, 1, 1, "failed to read the input stream");
    return false;
  }

  if (used == capacity) {
    ArrayGrowth const growth = grow_array(result, capacity, sizeof(*result));
    if (!growth.succeeded) {
      free(result);
      set_error(error, 1, 1, "failed to terminate the input buffer");
      return false;
    }
    result = growth.data;
  }
  result[used] = '\0';
  *text = result;
  *length = used;
  return true;
}

static void lexer_advance_byte(Lexer *lexer) {
  if (lexer->text[lexer->offset] == '\n') {
    lexer->line++;
    lexer->column = 1;
  } else {
    lexer->column++;
  }
  lexer->offset++;
}

[[nodiscard]] static Token lexer_error(Lexer *lexer, size_t const line,
                                       size_t const column,
                                       char const *message) {
  set_error(lexer->error, line, column, message);
  return (Token){.kind = TOKEN_ERROR, .line = line, .column = column};
}

[[nodiscard]] static Token lexer_next(Lexer *lexer) {
  while (lexer->offset < lexer->length &&
         isspace((unsigned char)lexer->text[lexer->offset])) {
    lexer_advance_byte(lexer);
  }

  size_t const begin = lexer->offset;
  size_t const line = lexer->line;
  size_t const column = lexer->column;
  if (begin == lexer->length) {
    return (Token){
        .kind = TOKEN_EOF, .begin = begin, .line = line, .column = column};
  }

  unsigned char const first = (unsigned char)lexer->text[begin];
  if (isdigit(first)) {
    while (lexer->offset < lexer->length &&
           isdigit((unsigned char)lexer->text[lexer->offset])) {
      lexer_advance_byte(lexer);
    }
    char const saved = lexer->text[lexer->offset];
    lexer->text[lexer->offset] = '\0';
    errno = 0;
    char *end = nullptr;
    uintmax_t const natural = strtoumax(lexer->text + begin, &end, 10);
    lexer->text[lexer->offset] = saved;
    if (errno == ERANGE || end != lexer->text + lexer->offset ||
        natural > UINT64_MAX) {
      return lexer_error(lexer, line, column,
                         "natural number does not fit in uint64_t");
    }
    return (Token){.kind = TOKEN_NATURAL,
                   .natural = (uint64_t)natural,
                   .begin = begin,
                   .length = lexer->offset - begin,
                   .line = line,
                   .column = column};
  }

  if (isalpha(first)) {
    while (lexer->offset < lexer->length &&
           isalpha((unsigned char)lexer->text[lexer->offset])) {
      lexer_advance_byte(lexer);
    }
    size_t const length = lexer->offset - begin;
    if (length == 6 && memcmp(lexer->text + begin, "parity", length) == 0) {
      return (Token){.kind = TOKEN_KW_PARITY,
                     .begin = begin,
                     .length = length,
                     .line = line,
                     .column = column};
    }
    return lexer_error(lexer, line, column, "unexpected keyword");
  }

  if (first == ',') {
    lexer_advance_byte(lexer);
    return (Token){.kind = TOKEN_COMMA,
                   .begin = begin,
                   .length = 1,
                   .line = line,
                   .column = column};
  }
  if (first == ';') {
    lexer_advance_byte(lexer);
    return (Token){.kind = TOKEN_SEMICOLON,
                   .begin = begin,
                   .length = 1,
                   .line = line,
                   .column = column};
  }
  if (first == '"') {
    lexer_advance_byte(lexer);
    size_t const content_begin = lexer->offset;
    while (lexer->offset < lexer->length && lexer->text[lexer->offset] != '"') {
      unsigned char const byte = (unsigned char)lexer->text[lexer->offset];
      if (byte == 0 || byte > 0x7fU) {
        return lexer_error(lexer, lexer->line, lexer->column,
                           "a name must contain only non-NUL ASCII bytes");
      }
      lexer_advance_byte(lexer);
    }
    if (lexer->offset == lexer->length) {
      return lexer_error(lexer, line, column, "unterminated quoted name");
    }
    size_t const content_length = lexer->offset - content_begin;
    lexer_advance_byte(lexer);
    return (Token){.kind = TOKEN_QUOTED_NAME,
                   .begin = content_begin,
                   .length = content_length,
                   .line = line,
                   .column = column};
  }

  lexer_advance_byte(lexer);
  return lexer_error(lexer, line, column, "unexpected character");
}

static void parser_advance(Parser *parser) {
  if (parser->token.kind != TOKEN_ERROR) {
    parser->token = lexer_next(&parser->lexer);
  }
}

[[nodiscard]] static bool expect(Parser *parser, TokenKind const kind,
                                 char const *message) {
  if (parser->token.kind == kind) {
    return true;
  }
  if (parser->token.kind != TOKEN_ERROR) {
    set_error(parser->lexer.error, parser->token.line, parser->token.column,
              message);
  }
  return false;
}

[[nodiscard]] static char *copy_token_text(Parser const *parser,
                                           Token const token) {
  if (token.length == SIZE_MAX) {
    return nullptr;
  }
  char *copy = malloc(token.length + 1);
  if (copy != nullptr) {
    memcpy(copy, parser->lexer.text + token.begin, token.length);
    copy[token.length] = '\0';
  }
  return copy;
}

static void destroy_declaration(TempDeclaration *declaration) {
  free(declaration->name);
  free(declaration->successors);
  *declaration = (TempDeclaration){0};
}

static void destroy_declarations(TempDeclaration *declarations,
                                 size_t const count) {
  for (size_t index = 0; index < count; index++) {
    destroy_declaration(declarations + index);
  }
  free(declarations);
}

[[nodiscard]] static bool append_successor(Parser *parser,
                                           TempDeclaration *declaration,
                                           Token const token) {
  if (declaration->successor_count == declaration->successor_capacity) {
    ArrayGrowth const growth =
        grow_array(declaration->successors, declaration->successor_capacity,
                   sizeof(declaration->successors[0]));
    if (!growth.succeeded) {
      set_error(parser->lexer.error, token.line, token.column,
                "failed to allocate the successor list");
      return false;
    }
    declaration->successors = growth.data;
    declaration->successor_capacity = growth.capacity;
  }
  declaration->successors[declaration->successor_count++] = (TempSuccessor){
      .external_id = token.natural, .line = token.line, .column = token.column};
  return true;
}

[[nodiscard]] static bool append_declaration(Parser *parser,
                                             TempDeclaration *declaration) {
  if (parser->declaration_count == parser->declaration_capacity) {
    ArrayGrowth const growth =
        grow_array(parser->declarations, parser->declaration_capacity,
                   sizeof(parser->declarations[0]));
    if (!growth.succeeded) {
      set_error(parser->lexer.error, declaration->line, declaration->column,
                "failed to allocate the declaration list");
      return false;
    }
    parser->declarations = growth.data;
    parser->declaration_capacity = growth.capacity;
  }
  declaration->source_order = parser->declaration_count;
  parser->declarations[parser->declaration_count++] = *declaration;
  *declaration = (TempDeclaration){0};
  return true;
}

[[nodiscard]] static bool parse_declaration(Parser *parser) {
  TempDeclaration declaration = {
      .external_id = parser->token.natural,
      .line = parser->token.line,
      .column = parser->token.column,
  };
  if (parser->has_header && declaration.external_id > parser->header_maximum) {
    set_error(parser->lexer.error, declaration.line, declaration.column,
              "node identifier exceeds the parity header maximum");
    return false;
  }
  parser_advance(parser);

  if (!expect(parser, TOKEN_NATURAL, "expected a node priority")) {
    goto failure;
  }
  declaration.priority = parser->token.natural;
  parser_advance(parser);

  if (!expect(parser, TOKEN_NATURAL, "expected owner 0 or 1")) {
    goto failure;
  }
  if (parser->token.natural > 1) {
    set_error(parser->lexer.error, parser->token.line, parser->token.column,
              "owner must be exactly 0 or 1");
    goto failure;
  }
  declaration.owner = (PGPlayer)parser->token.natural;
  parser_advance(parser);

  if (!expect(parser, TOKEN_NATURAL,
              "expected at least one successor identifier")) {
    goto failure;
  }
  while (true) {
    Token const successor = parser->token;
    if (parser->has_header && successor.natural > parser->header_maximum) {
      set_error(parser->lexer.error, successor.line, successor.column,
                "successor identifier exceeds the parity header maximum");
      goto failure;
    }
    if (!append_successor(parser, &declaration, successor)) {
      goto failure;
    }
    parser_advance(parser);
    if (parser->token.kind != TOKEN_COMMA) {
      break;
    }
    parser_advance(parser);
    if (!expect(parser, TOKEN_NATURAL,
                "expected a successor identifier after ','")) {
      goto failure;
    }
  }

  if (parser->token.kind == TOKEN_QUOTED_NAME) {
    declaration.name = copy_token_text(parser, parser->token);
    if (declaration.name == nullptr) {
      set_error(parser->lexer.error, parser->token.line, parser->token.column,
                "failed to allocate the node name");
      goto failure;
    }
    parser_advance(parser);
  }

  if (!expect(parser, TOKEN_SEMICOLON,
              "expected ';' after the node declaration")) {
    goto failure;
  }
  parser_advance(parser);
  if (!append_declaration(parser, &declaration)) {
    goto failure;
  }
  return true;

failure:
  destroy_declaration(&declaration);
  return false;
}

[[nodiscard]] static bool parse_stream(Parser *parser) {
  parser->token = lexer_next(&parser->lexer);
  if (parser->token.kind == TOKEN_KW_PARITY) {
    parser->has_header = true;
    parser_advance(parser);
    if (!expect(parser, TOKEN_NATURAL,
                "expected a maximum identifier after 'parity'")) {
      return false;
    }
    parser->header_maximum = parser->token.natural;
    parser_advance(parser);
    if (!expect(parser, TOKEN_SEMICOLON,
                "expected ';' after the parity header")) {
      return false;
    }
    parser_advance(parser);
  }

  while (parser->token.kind != TOKEN_EOF) {
    if (!expect(parser, TOKEN_NATURAL,
                "expected a node declaration or end of input") ||
        !parse_declaration(parser)) {
      return false;
    }
  }
  if (parser->declaration_count == 0) {
    set_error(parser->lexer.error, parser->token.line, parser->token.column,
              "expected at least one node declaration");
    return false;
  }
  return true;
}

[[nodiscard]] static int compare_declarations(void const *left,
                                              void const *right) {
  TempDeclaration const *first = left;
  TempDeclaration const *second = right;
  if (first->external_id < second->external_id) {
    return -1;
  }
  if (first->external_id > second->external_id) {
    return 1;
  }
  if (first->source_order < second->source_order) {
    return -1;
  }
  return first->source_order > second->source_order ? 1 : 0;
}

[[nodiscard]] static size_t
retain_last_declarations(TempDeclaration *declarations, size_t const count) {
  qsort(declarations, count, sizeof(declarations[0]), compare_declarations);
  size_t write_index = 0;
  for (size_t begin = 0; begin < count;) {
    size_t end = begin + 1;
    while (end < count &&
           declarations[end].external_id == declarations[begin].external_id) {
      end++;
    }
    for (size_t index = begin; index + 1 < end; index++) {
      destroy_declaration(declarations + index);
    }
    if (write_index != end - 1) {
      declarations[write_index] = declarations[end - 1];
      declarations[end - 1] = (TempDeclaration){0};
    }
    write_index++;
    begin = end;
  }
  return write_index;
}

[[nodiscard]] static size_t find_vertex(PGGame const *game,
                                        uint64_t const external_id) {
  size_t begin = 0;
  size_t end = game->vertex_count;
  while (begin < end) {
    size_t const middle = begin + (end - begin) / 2;
    uint64_t const candidate = game->vertices[middle].external_id;
    if (candidate < external_id) {
      begin = middle + 1;
    } else {
      end = middle;
    }
  }
  return begin < game->vertex_count &&
                 game->vertices[begin].external_id == external_id
             ? begin
             : SIZE_MAX;
}

[[nodiscard]] static bool allocate_csr(PGGame *game, size_t const edge_count,
                                       PGParseError *error) {
  if (game->vertex_count == SIZE_MAX ||
      edge_count > SIZE_MAX / sizeof(game->successors[0])) {
    set_error(error, 1, 1, "the graph is too large for this platform");
    return false;
  }
  game->succ_offsets =
      calloc(game->vertex_count + 1, sizeof(game->succ_offsets[0]));
  game->pred_offsets =
      calloc(game->vertex_count + 1, sizeof(game->pred_offsets[0]));
  game->successors = malloc(edge_count * sizeof(game->successors[0]));
  game->predecessors = malloc(edge_count * sizeof(game->predecessors[0]));
  if (game->succ_offsets == nullptr || game->pred_offsets == nullptr ||
      (edge_count != 0 &&
       (game->successors == nullptr || game->predecessors == nullptr))) {
    set_error(error, 1, 1, "failed to allocate the graph edges");
    return false;
  }
  return true;
}

[[nodiscard]] static bool finalize_game(Parser *parser, PGGame *game) {
  size_t const count =
      retain_last_declarations(parser->declarations, parser->declaration_count);
  if (count == 0) {
    set_error(parser->lexer.error, 1, 1,
              "cannot finalize a game without declarations");
    return false;
  }
  if (count > SIZE_MAX / sizeof(game->vertices[0])) {
    set_error(parser->lexer.error, 1, 1,
              "the graph has too many vertices for this platform");
    return false;
  }
  game->vertices = calloc(count, sizeof(game->vertices[0]));
  if (game->vertices == nullptr) {
    set_error(parser->lexer.error, 1, 1, "failed to allocate graph vertices");
    return false;
  }
  game->vertex_count = count;

  size_t edge_count = 0;
  for (size_t index = 0; index < count; index++) {
    TempDeclaration *declaration = parser->declarations + index;
    game->vertices[index] = (PGVertex){.external_id = declaration->external_id,
                                       .priority = declaration->priority,
                                       .owner = declaration->owner,
                                       .name = declaration->name};
    declaration->name = nullptr;
    if (declaration->priority > game->max_priority) {
      game->max_priority = declaration->priority;
    }
    if (edge_count > SIZE_MAX - declaration->successor_count) {
      set_error(parser->lexer.error, declaration->line, declaration->column,
                "the graph has too many edges for this platform");
      return false;
    }
    edge_count += declaration->successor_count;
  }
  game->edge_count = edge_count;
  if (!allocate_csr(game, edge_count, parser->lexer.error)) {
    return false;
  }

  size_t *predecessor_counts = calloc(count, sizeof(predecessor_counts[0]));
  if (predecessor_counts == nullptr) {
    set_error(parser->lexer.error, 1, 1,
              "failed to allocate predecessor counts");
    return false;
  }

  size_t edge = 0;
  for (size_t source = 0; source < count; source++) {
    TempDeclaration const *declaration = parser->declarations + source;
    game->succ_offsets[source] = edge;
    for (size_t index = 0; index < declaration->successor_count; index++) {
      TempSuccessor const successor = declaration->successors[index];
      size_t const target = find_vertex(game, successor.external_id);
      if (target == SIZE_MAX) {
        set_error(parser->lexer.error, successor.line, successor.column,
                  "successor identifier has no final declaration");
        free(predecessor_counts);
        return false;
      }
      game->successors[edge++] = target;
      predecessor_counts[target]++;
    }
  }
  game->succ_offsets[count] = edge;

  for (size_t index = 0; index < count; index++) {
    if (game->pred_offsets[index] > SIZE_MAX - predecessor_counts[index]) {
      set_error(parser->lexer.error, 1, 1,
                "predecessor offsets overflow size_t");
      free(predecessor_counts);
      return false;
    }
    game->pred_offsets[index + 1] =
        game->pred_offsets[index] + predecessor_counts[index];
  }

  size_t *cursor = malloc(count * sizeof(cursor[0]));
  if (cursor == nullptr) {
    set_error(parser->lexer.error, 1, 1,
              "failed to allocate predecessor cursors");
    free(predecessor_counts);
    return false;
  }
  memcpy(cursor, game->pred_offsets, count * sizeof(cursor[0]));
  for (size_t source = 0; source < count; source++) {
    for (size_t index = game->succ_offsets[source];
         index < game->succ_offsets[source + 1]; index++) {
      size_t const target = game->successors[index];
      game->predecessors[cursor[target]++] = source;
    }
  }
  free(cursor);
  free(predecessor_counts);
  return true;
}

bool pg_game_read(FILE *input, PGGame *game, PGParseError *error) {
  if (error != nullptr) {
    *error = (PGParseError){.line = 1, .column = 1, .message = ""};
  }
  if (input == nullptr || game == nullptr) {
    set_error(error, 1, 1, "input and game must not be null");
    return false;
  }
  *game = (PGGame){0};

  char *text = nullptr;
  size_t length = 0;
  if (!read_stream(input, &text, &length, error)) {
    return false;
  }
  Parser parser = {.lexer = {.text = text,
                             .length = length,
                             .line = 1,
                             .column = 1,
                             .error = error}};
  bool const parsed = parse_stream(&parser) && finalize_game(&parser, game);
  if (!parsed) {
    pg_game_destroy(game);
  }
  destroy_declarations(parser.declarations, parser.declaration_count);
  free(text);
  return parsed;
}

void pg_game_destroy(PGGame *game) {
  if (game == nullptr) {
    return;
  }
  for (size_t index = 0; index < game->vertex_count; index++) {
    free(game->vertices[index].name);
  }
  free(game->vertices);
  free(game->succ_offsets);
  free(game->successors);
  free(game->pred_offsets);
  free(game->predecessors);
  *game = (PGGame){0};
}

bool pg_game_write_pgsolver(FILE *out, PGGame const *game,
                            bool const include_names) {
  if (out == nullptr || game == nullptr || game->vertex_count == 0 ||
      game->vertices == nullptr || game->succ_offsets == nullptr ||
      game->successors == nullptr) {
    return false;
  }
  uint64_t const maximum_id =
      game->vertices[game->vertex_count - 1].external_id;
  if (fprintf(out, "parity %" PRIu64 ";\n", maximum_id) < 0) {
    return false;
  }

  for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
    PGVertex const *data = game->vertices + vertex;
    if (fprintf(out, "%" PRIu64 " %" PRIu64 " %u ", data->external_id,
                data->priority, (unsigned)data->owner) < 0) {
      return false;
    }
    size_t const begin = game->succ_offsets[vertex];
    size_t const end = game->succ_offsets[vertex + 1];
    if (begin >= end || end > game->edge_count) {
      return false;
    }
    for (size_t edge = begin; edge < end; edge++) {
      size_t const successor = game->successors[edge];
      if (successor >= game->vertex_count ||
          (edge != begin && fputc(',', out) == EOF) ||
          fprintf(out, "%" PRIu64, game->vertices[successor].external_id) < 0) {
        return false;
      }
    }
    if (include_names && data->name != nullptr &&
        fprintf(out, " \"%s\"", data->name) < 0) {
      return false;
    }
    if (fputs(";\n", out) < 0) {
      return false;
    }
  }
  return true;
}
