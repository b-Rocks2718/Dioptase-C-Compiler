#include "unique_name.h"

#include "arena.h"

static int unique_id_counter = 0;

// helper function to calculate length of counter when converted to string
unsigned counter_len(int counter) {
  unsigned len = 0;
  do {
    len++;
    counter /= 10;
  } while (counter != 0);
  return len;
}

// create a unique name by appending a unique id to the original name
struct Slice* make_unique(struct Slice* original_name) {
  unsigned id_len = counter_len(unique_id_counter);
  size_t new_len = original_name->len + 1 + id_len; // +1 for period

  char* new_str = (char*)arena_alloc(new_len);
  for (size_t i = 0; i < original_name->len; i++) {
    new_str[i] = original_name->start[i];
  }
  new_str[original_name->len] = '.';

  // append unique id
  int id = unique_id_counter;
  for (unsigned i = 0; i < id_len; i++) {
    new_str[new_len - 1 - i] = '0' + (id % 10);
    id /= 10;
  }

  unique_id_counter++;

  struct Slice* unique_name = (struct Slice*)arena_alloc(sizeof(struct Slice));
  unique_name->start = new_str;
  unique_name->len = new_len;

  return unique_name;
}

struct Slice* make_unique_label(struct Slice* func_name, const char* suffix) {
  size_t suffix_len = 0;
  while (suffix[suffix_len] != '\0') {
    suffix_len++;
  }
  unsigned id_len = counter_len(unique_id_counter);
  size_t new_len = func_name->len + 1 + suffix_len + 1 + id_len; // +1 for period

  char* new_str = (char*)arena_alloc(new_len);
  for (size_t i = 0; i < func_name->len; i++) {
    new_str[i] = func_name->start[i];
  }
  new_str[func_name->len] = '.';
  for (size_t i = 0; i < suffix_len; i++) {
    new_str[func_name->len + 1 + i] = suffix[i];
  }

  new_str[func_name->len + 1 + suffix_len] = '.';

  // append unique id
  int id = unique_id_counter;
  for (unsigned i = 0; i < id_len; i++) {
    new_str[new_len - 1 - i] = '0' + (id % 10);
    id /= 10;
  }

  unique_id_counter++;

  struct Slice* unique_label = (struct Slice*)arena_alloc(sizeof(struct Slice));
  unique_label->start = new_str;
  unique_label->len = new_len;

  return unique_label;
}
