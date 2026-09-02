#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dot_utils.h"

int main(void) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);

  assert(dot_write_quoted(stream, "quote\" slash\\ line\nreturn\rtab\t\001"));
  assert(fflush(stream) == 0);
  assert(fseek(stream, 0, SEEK_SET) == 0);

  char output[128] = {0};
  size_t const length = fread(output, 1, sizeof(output) - 1, stream);
  assert(length > 0);
  assert(strcmp(output,
                "\"quote\\\" slash\\\\ line\\nreturn\\rtab\\t\\001\"") == 0);
  assert(fclose(stream) == 0);
  return 0;
}
