#include "roleutil/role_sync_parse.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void rejects_without_mutating(const char *input) {
  uint64_t value = 77;
  assert(!rolesync_parse_u64(input, &value));
  assert(value == 77);
}

int main(void) {
  uint64_t value = 0;
  assert(rolesync_parse_u64("123456789012345678", &value));
  assert(value == UINT64_C(123456789012345678));
  assert(rolesync_parse_u64(" 18446744073709551615 \t", &value));
  assert(value == UINT64_MAX);

  rejects_without_mutating(NULL);
  rejects_without_mutating("");
  rejects_without_mutating("0");
  rejects_without_mutating("+1");
  rejects_without_mutating("-1");
  rejects_without_mutating("1x");
  rejects_without_mutating("1 2");
  rejects_without_mutating("18446744073709551616");
  rejects_without_mutating("999999999999999999999999999999999999");

  uint64_t a = 0, b = 0;
  const char *label = NULL;
  assert(rolesync_parse_pair(" 123 456 staff role", &a, &b, &label));
  assert(a == 123 && b == 456);
  assert(label && strcmp(label, "staff role") == 0);
  assert(rolesync_parse_pair("123\t456", &a, &b, &label));
  assert(label == NULL);

  a = 11;
  b = 22;
  const char *sentinel = "unchanged";
  label = sentinel;
  assert(!rolesync_parse_pair("123 +456", &a, &b, &label));
  assert(a == 11 && b == 22 && label == sentinel);
  assert(!rolesync_parse_pair("123 18446744073709551616", &a, &b, &label));
  assert(a == 11 && b == 22 && label == sentinel);
  assert(!rolesync_parse_pair("123 0", &a, &b, &label));
  assert(!rolesync_parse_pair("123x 456", &a, &b, &label));

  puts("role-sync parser tests: PASS");
  return 0;
}
