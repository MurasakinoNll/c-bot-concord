#ifndef ROLE_SYNC_PARSE_H
#define ROLE_SYNC_PARSE_H

#include <stdbool.h>
#include <stdint.h>

/* Parse strict positive decimal Discord snowflakes without partial writes. */
bool rolesync_parse_u64_token(const char *input, uint64_t *value,
                              const char **after);
bool rolesync_parse_u64(const char *input, uint64_t *value);
bool rolesync_parse_pair(const char *input, uint64_t *first,
                         uint64_t *second, const char **label);

#endif
