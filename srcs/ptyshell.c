#include "ptyshell.h"
#include "utils.h"
#include <concord/log.h>
#include <pty.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>
#include <errno.h>

#define PTY_CHANNEL_ID 1486807756290785400ULL

typedef struct {
  bool running;
  int master_fd;
  pid_t child_pid;
  u64snowflake channel_id;
  unsigned timer_id;
} pty_session;

static pty_session g_session = {0};

void pty_reader_tick(struct discord *client, struct discord_timer *timer);
static void pty_status_changed(struct discord *client, struct discord_timer *timer);


#define PTY_READ_CHUNK   4096
#define PTY_ACCUM_SIZE   (PTY_READ_CHUNK * 4)
#define PTY_DISCORD_MAX  1900

static regex_t g_ansi_re;
static bool g_ansi_re_compiled = false;

static void ensure_ansi_regex(void) {
  if (g_ansi_re_compiled) return;       // discord-ANSI compliant regex filtering
  const char *pattern =
    "\x1b\\[[0-9;?]*[A-Za-z]"
    "|\x1b\\][^\x07\x1b]*(\x07|\x1b\\\\)"
    "|\x1b[()][AB012]"
    "|\x1b[=>]"
    "|\x1b[MDE78]"
    "|\r";
  if (regcomp(&g_ansi_re, pattern, REG_EXTENDED) != 0) {
    log_error("failed to compile ANSI strip regex");
  }
  g_ansi_re_compiled = true;
}

static char *strip_ansi(const char *raw, size_t raw_len) {
  ensure_ansi_regex();

  char *src = malloc(raw_len + 1);
  if (!src) return NULL;
  memcpy(src, raw, raw_len);
  src[raw_len] = '\0';

  char *out = malloc(raw_len + 1);
  if (!out) { free(src); return NULL; }
  size_t out_len = 0;

  char *cur = src;
  regmatch_t m;
  while (*cur) {
    if (regexec(&g_ansi_re, cur, 1, &m, 0) == 0) {
      size_t keep = (size_t)m.rm_so;
      memcpy(out + out_len, cur, keep);
      out_len += keep;
      cur += m.rm_eo;
      if (m.rm_eo == m.rm_so) {
        if (*cur) { out[out_len++] = *cur; cur++; }
        else break;
      }
    } else {
      size_t rem = strlen(cur);
      memcpy(out + out_len, cur, rem);
      out_len += rem;
      break;
    }
  }
  out[out_len] = '\0';
  free(src);
  return out;
}

static void send_chunked(struct discord *client, u64snowflake channel_id, const char *text) {
  size_t len = strlen(text);
  size_t pos = 0;

  while (pos < len) {
    size_t remaining = len - pos;
    size_t take = remaining > PTY_DISCORD_MAX ? PTY_DISCORD_MAX : remaining;
    size_t cut = take;

    if (take == PTY_DISCORD_MAX) {
      for (size_t i = take; i > 0; i--) {
        if (text[pos + i - 1] == '\n') { cut = i; break; }
      }
    }

    char *block = malloc(cut + 1);
    memcpy(block, text + pos, cut);
    block[cut] = '\0';

    char *wrapped = malloc(cut + 16);
    snprintf(wrapped, cut + 16, "```\n%s\n```", block);

    struct discord_create_message params = { .content = wrapped };
    discord_create_message(client, channel_id, &params, NULL);

    free(block);
    free(wrapped);
    pos += cut;
  }
}

void pty_reader_tick(struct discord *client, struct discord_timer *timer) {
  pty_session *sess = (pty_session*)timer->data;
  if (!sess->running) return;

  char buf[PTY_READ_CHUNK];
  char accum[PTY_ACCUM_SIZE];
  size_t accum_len = 0;
  bool exited = false;

  for (;;) {
    ssize_t n = read(sess->master_fd, buf, sizeof buf);
    if (n > 0) {
      if (accum_len + (size_t)n < sizeof accum) {
        memcpy(accum + accum_len, buf, (size_t)n);
        accum_len += (size_t)n;
      }
      continue;
    }
    if (n == 0) { exited = true; break; }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    exited = true;
    break;
  }

  if (accum_len > 0) {
    char *clean = strip_ansi(accum, accum_len);
    if (clean && *clean) send_chunked(client, sess->channel_id, clean);
    free(clean);
  }

  if (exited) {
    struct discord_create_message reply = { .content = "shell process exited (dropped)" };
    discord_create_message(client, sess->channel_id, &reply, NULL);
    discord_timer_cancel_and_delete(client, sess->timer_id);
  }
}

void ptyinput_fallback(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;
  if (event->channel_id != PTY_CHANNEL_ID) return;
  if (!g_session.running) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)) return;

  if (!event->content || !*event->content) return;

  size_t len = strlen(event->content);
  char *data = malloc(len + 2);
  memcpy(data, event->content, len);
  data[len] = '\r';
  data[len + 1] = '\0';

  write(g_session.master_fd, data, len + 1);
  free(data);
}

static void kill_session(void) {
  if (!g_session.running) return;
  g_session.running = false;

  if (g_session.child_pid > 0) {
    kill(-g_session.child_pid, SIGKILL);
    waitpid(g_session.child_pid, NULL, 0);
    g_session.child_pid = -1;
  }
  if (g_session.master_fd >= 0) {
    close(g_session.master_fd);
    g_session.master_fd = -1;
  }
}

void ptystart_command(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;
  if (event->channel_id != PTY_CHANNEL_ID) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)) {
    fprintf(stderr, "ptystart rejected, invalid permissions\n");
    return;
  }

  if (g_session.running) {
    struct discord_create_message reply = { .content = "shell session is already running in this channel" };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  int master_fd, slave_fd;
  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0) {
    struct discord_create_message reply = { .content = "failed to open pty" };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(master_fd);
    close(slave_fd);
    struct discord_create_message reply = { .content = "failed to fork" };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  if (pid == 0) {
    /* child */
    setsid();
    ioctl(slave_fd, TIOCSCTTY, 0);
    dup2(slave_fd, STDIN_FILENO);
    dup2(slave_fd, STDOUT_FILENO);
    dup2(slave_fd, STDERR_FILENO);
    close(master_fd);
    close(slave_fd);

    setenv("TERM", "xterm-256color", 1);
    setenv("PS1", "$ ", 1);

    char *argv[] = { "/bin/bash", "--norc", "--noprofile", NULL };
    execv("/bin/bash", argv);
    _exit(127);
  }

  /* parent */
  close(slave_fd);
  fcntl(master_fd, F_SETFL, O_NONBLOCK);

  g_session.running = true;
  g_session.master_fd = master_fd;
  g_session.child_pid = pid;
  g_session.channel_id = event->channel_id;
  g_session.timer_id = discord_timer_interval(client, &pty_reader_tick, &pty_status_changed,
                                               &g_session, 0, 150, -1);

  char buf[64];
  snprintf(buf, sizeof buf, "shell session started (PID %d).", pid);
  struct discord_create_message reply = { .content = buf };
  discord_create_message(client, event->channel_id, &reply, NULL);
}

void ptystop_command(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;
  if (event->channel_id != PTY_CHANNEL_ID) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)) return;

  if (!g_session.running) {
    struct discord_create_message reply = { .content = "no active shell session." };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  discord_timer_cancel_and_delete(client, g_session.timer_id);

  struct discord_create_message reply = { .content = "shell session ended with SIGINT" };
  discord_create_message(client, event->channel_id, &reply, NULL);
}

static void pty_status_changed(struct discord *client, struct discord_timer *timer) {
  (void)client;
  if (timer->flags & (DISCORD_TIMER_CANCELED | DISCORD_TIMER_DELETE)) {
    kill_session();
  }
}
