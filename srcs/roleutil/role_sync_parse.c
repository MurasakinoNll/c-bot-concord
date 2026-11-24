#include "roleutil/role_sync_parse.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

static const char *skip_space(const char *p) {
  while (*p && isspace((unsigned char)*p))
    ++p;
  return p;
}

bool rolesync_parse_u64_token(const char *input, uint64_t *value,
                              const char **after) {
  if (!input || !value)
    return false;

  const char *p = skip_space(input);
  if (*p < '0' || *p > '9')
    return false;

  uint64_t parsed = 0;
  do {
    unsigned digit = (unsigned)(*p - '0');
    if (parsed > (UINT64_MAX - digit) / 10U)
      return false;
    parsed = parsed * 10U + digit;
    ++p;
  } while (*p >= '0' && *p <= '9');

  if (*p && !isspace((unsigned char)*p))
    return false;
  if (parsed == 0)
    return false;

  *value = parsed;
  if (after)
    *after = p;
  return true;
}

bool rolesync_parse_u64(const char *input, uint64_t *value) {
  if (!value)
    return false;
  uint64_t parsed;
  const char *after;
  if (!rolesync_parse_u64_token(input, &parsed, &after))
    return false;
  if (*skip_space(after) != '\0')
    return false;
  *value = parsed;
  return true;
}

bool rolesync_parse_pair(const char *input, uint64_t *first,
                         uint64_t *second, const char **label) {
  if (!input || !first || !second)
    return false;

  uint64_t a, b;
  const char *after_a, *after_b;
  if (!rolesync_parse_u64_token(input, &a, &after_a))
    return false;
  const char *second_start = skip_space(after_a);
  if (!rolesync_parse_u64_token(second_start, &b, &after_b))
    return false;

  const char *tail = skip_space(after_b);
  *first = a;
  *second = b;
  if (label)
    *label = *tail ? tail : NULL;
  return true;
}
