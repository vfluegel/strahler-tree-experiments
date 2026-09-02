#include <stdio.h>

#include "dot_utils.h"

[[nodiscard]] static bool put_text(FILE *out, char const *text) {
  return fputs(text, out) >= 0;
}

bool dot_write_quoted(FILE *out, char const *text) {
  if (out == nullptr || text == nullptr || fputc('"', out) == EOF) {
    return false;
  }

  for (unsigned char const *cur = (unsigned char const *)text; *cur != '\0';
       cur++) {
    switch (*cur) {
    case '"':
      if (!put_text(out, "\\\"")) {
        return false;
      }
      break;
    case '\\':
      if (!put_text(out, "\\\\")) {
        return false;
      }
      break;
    case '\n':
      if (!put_text(out, "\\n")) {
        return false;
      }
      break;
    case '\r':
      if (!put_text(out, "\\r")) {
        return false;
      }
      break;
    case '\t':
      if (!put_text(out, "\\t")) {
        return false;
      }
      break;
    default:
      if (*cur < 0x20U || *cur == 0x7fU) {
        if (fprintf(out, "\\%03o", (unsigned)*cur) < 0) {
          return false;
        }
      } else if (fputc(*cur, out) == EOF) {
        return false;
      }
      break;
    }
  }

  return fputc('"', out) != EOF;
}
