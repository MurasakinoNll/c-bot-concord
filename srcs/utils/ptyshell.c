#include "utils/ptyshell.h"
#include "utils/utils.h"
#include <concord/log.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
static void pty_status_changed(struct discord *client,
                               struct discord_timer *timer);

#define PTY_READ_CHUNK 4096
#define PTY_ACCUM_SIZE (PTY_READ_CHUNK * 4)
#define PTY_DISCORD_MAX 1900

static regex_t g_ansi_re;
static bool g_ansi_re_compiled = false;

static void ensure_ansi_regex(void) {
  if (g_ansi_re_compiled)
    return; // discord-ANSI compliant regex filtering
  const char *pattern = "\x1b\\[[0-9;?]*[A-Za-z]"
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
  if (!src)
    return NULL;
  memcpy(src, raw, raw_len);
  src[raw_len] = '\0';

  char *out = malloc(raw_len + 1);
  if (!out) {
    free(src);
    return NULL;
  }
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
        if (*cur) {
          out[out_len++] = *cur;
          cur++;
        } else
          break;
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

static void send_chunked(struct discord *client, u64snowflake channel_id,
                         const char *text) {
  size_t len = strlen(text);
  size_t pos = 0;

  while (pos < len) {
    size_t remaining = len - pos;
    size_t take = remaining > PTY_DISCORD_MAX ? PTY_DISCORD_MAX : remaining;
    size_t cut = take;

    if (take == PTY_DISCORD_MAX) {
      for (size_t i = take; i > 0; i--) {
        if (text[pos + i - 1] == '\n') {
          cut = i;
          break;
        }
      }
    }

    char *block = malloc(cut + 1);
    memcpy(block, text + pos, cut);
    block[cut] = '\0';

    char *wrapped = malloc(cut + 16);
    snprintf(wrapped, cut + 16, "```\n%s\n```", block);

    struct discord_create_message params = {.content = wrapped};
    discord_create_message(client, channel_id, &params, NULL);

    free(block);
    free(wrapped);
    pos += cut;
  }
}

void pty_reader_tick(struct discord *client, struct discord_timer *timer) {
  pty_session *sess = (pty_session *)timer->data;
  if (!sess->running)
    return;

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
    if (n == 0) {
      exited = true;
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
    exited = true;
    break;
  }

  if (accum_len > 0) {
    char *clean = strip_ansi(accum, accum_len);
    if (clean && *clean)
      send_chunked(client, sess->channel_id, clean);
    free(clean);
  }

  if (exited) {
    struct discord_create_message reply = {
        .content = "shell process exited (dropped)"};
    discord_create_message(client, sess->channel_id, &reply, NULL);
    discord_timer_cancel_and_delete(client, sess->timer_id);
  }
}

void ptyinput_fallback(struct discord *client,
                       const struct discord_message *event) {
  if (event->author->bot)
    return;
  if (event->channel_id != PTY_CHANNEL_ID)
    return;
  if (!g_session.running)
    return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {1155152569526669391ULL};
  if (!check_perm_byrole(&ctx, allowlist, 1))
    return;

  if (!event->content || !*event->content)
    return;

  size_t len = strlen(event->content);
  char *data = malloc(len + 2);
  memcpy(data, event->content, len);
  data[len] = '\r';
  data[len + 1] = '\0';

  write(g_session.master_fd, data, len + 1);
  free(data);
}

static void kill_session(void) {
  if (!g_session.running)
    return;
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

void ptystart_command(struct discord *client,
                      const struct discord_message *event) {
  if (event->author->bot)
    return;
  if (event->channel_id != PTY_CHANNEL_ID)
    return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {1155152569526669391ULL};
  if (!check_perm_byrole(&ctx, allowlist, 1)) {
    fprintf(stderr, "ptystart rejected, invalid permissions\n");
    return;
  }

  if (g_session.running) {
    struct discord_create_message reply = {
        .content = "shell session is already running in this channel"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  int master_fd, slave_fd;
  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0) {
    struct discord_create_message reply = {.content = "failed to open pty"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(master_fd);
    close(slave_fd);
    struct discord_create_message reply = {.content = "failed to fork"};
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

    char *argv[] = {"/bin/bash", "--norc", "--noprofile", NULL};
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
  g_session.timer_id = discord_timer_interval(
      client, &pty_reader_tick, &pty_status_changed, &g_session, 0, 150, -1);

  char buf[64];
  snprintf(buf, sizeof buf, "shell session started (PID %d).", pid);
  struct discord_create_message reply = {.content = buf};
  discord_create_message(client, event->channel_id, &reply, NULL);
}

void ptystop_command(struct discord *client,
                     const struct discord_message *event) {
  if (event->author->bot)
    return;
  if (event->channel_id != PTY_CHANNEL_ID)
    return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {1155152569526669391ULL};
  if (!check_perm_byrole(&ctx, allowlist, 1))
    return;

  if (!g_session.running) {
    struct discord_create_message reply = {.content =
                                               "no active shell session."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  discord_timer_cancel_and_delete(client, g_session.timer_id);

  struct discord_create_message reply = {.content =
                                             "shell session ended with SIGINT"};
  discord_create_message(client, event->channel_id, &reply, NULL);
}

static void pty_status_changed(struct discord *client,
                               struct discord_timer *timer) {
  (void)client;
  if (timer->flags & (DISCORD_TIMER_CANCELED | DISCORD_TIMER_DELETE)) {
    kill_session();
  }
}

/* ---- one-shot +exec ----------------------------------------------------
 * channel independent, owner only. same pty spawn as ptystart, but runs a
 * single `bash -c <cmd>`, buffers the output, and reports the exit code once
 * the child is done. */

#define EXEC_OWNER_ID 821998767652995083ULL
#define EXEC_TICK_MS 150
#define EXEC_TIMEOUT_TICKS 400 /* 400 * 150ms = 60s */

typedef struct {
  bool running;
  int master_fd;
  pid_t child_pid;
  u64snowflake channel_id;
  unsigned timer_id;
  char *accum;
  size_t accum_len;
  int ticks;
} exec_session;

static exec_session g_exec = {0};

static void exec_cleanup(void) {
  if (g_exec.master_fd >= 0) {
    close(g_exec.master_fd);
    g_exec.master_fd = -1;
  }
  if (g_exec.child_pid > 0) {
    kill(-g_exec.child_pid, SIGKILL);
    waitpid(g_exec.child_pid, NULL, 0);
    g_exec.child_pid = -1;
  }
  free(g_exec.accum);
  g_exec.accum = NULL;
  g_exec.accum_len = 0;
  g_exec.ticks = 0;
  g_exec.running = false;
}

static void exec_status_changed(struct discord *client,
                                struct discord_timer *timer) {
  (void)client;
  if (timer->flags & (DISCORD_TIMER_CANCELED | DISCORD_TIMER_DELETE)) {
    exec_cleanup();
  }
}

static void exec_tick(struct discord *client, struct discord_timer *timer) {
  exec_session *s = (exec_session *)timer->data;
  if (!s->running)
    return;

  char buf[PTY_READ_CHUNK];
  bool finished = false;

  for (;;) {
    ssize_t n = read(s->master_fd, buf, sizeof buf);
    if (n > 0) {
      char *tmp = realloc(s->accum, s->accum_len + (size_t)n + 1);
      if (tmp) {
        s->accum = tmp;
        memcpy(s->accum + s->accum_len, buf, (size_t)n);
        s->accum_len += (size_t)n;
        s->accum[s->accum_len] = '\0';
      }
      continue;
    }
    /* EOF on the master means every slave fd closed, i.e. the child is gone */
    if (n == 0) {
      finished = true;
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
    finished = true;
    break;
  }

  bool timed_out = false;
  if (!finished && ++s->ticks > EXEC_TIMEOUT_TICKS) {
    timed_out = true;
    finished = true;
    if (s->child_pid > 0)
      kill(-s->child_pid, SIGKILL);
  }

  if (!finished)
    return;

  int status = 0;
  if (s->child_pid > 0) {
    waitpid(s->child_pid, &status, 0);
    s->child_pid = -1;
  }

  if (s->accum_len > 0) {
    char *clean = strip_ansi(s->accum, s->accum_len);
    if (clean && *clean)
      send_chunked(client, s->channel_id, clean);
    free(clean);
  }

  char tail[128];
  if (timed_out)
    snprintf(tail, sizeof tail, "timed out after %ds, killed",
             (EXEC_TIMEOUT_TICKS * EXEC_TICK_MS) / 1000);
  else if (WIFEXITED(status))
    snprintf(tail, sizeof tail, "exit code: %d", WEXITSTATUS(status));
  else if (WIFSIGNALED(status))
    snprintf(tail, sizeof tail, "killed by signal %d", WTERMSIG(status));
  else
    snprintf(tail, sizeof tail, "finished (raw status %d)", status);

  struct discord_create_message reply = {.content = tail};
  discord_create_message(client, s->channel_id, &reply, NULL);

  /* triggers exec_status_changed -> exec_cleanup */
  discord_timer_cancel_and_delete(client, s->timer_id);
}

void exec_command(struct discord *client, const struct discord_message *event) {
  if (event->author->bot)
    return;
  if (event->author->id != EXEC_OWNER_ID) {
    fprintf(stderr, "exec rejected, not owner\n");
    return;
  }

  if (!event->content || !*event->content) {
    struct discord_create_message reply = {.content = "usage: +exec <command>"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  if (g_exec.running) {
    struct discord_create_message reply = {
        .content = "an +exec command is already running"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  int master_fd, slave_fd;
  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0) {
    struct discord_create_message reply = {.content = "failed to open pty"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(master_fd);
    close(slave_fd);
    struct discord_create_message reply = {.content = "failed to fork"};
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

    setenv("TERM", "dumb", 1);

    char *argv[] = {
        "/bin/bash", "--norc", "--noprofile", "-c", (char *)event->content,
        NULL};
    execv("/bin/bash", argv);
    _exit(127);
  }

  /* parent */
  close(slave_fd);
  fcntl(master_fd, F_SETFL, O_NONBLOCK);

  g_exec.running = true;
  g_exec.master_fd = master_fd;
  g_exec.child_pid = pid;
  g_exec.channel_id = event->channel_id;
  g_exec.accum = NULL;
  g_exec.accum_len = 0;
  g_exec.ticks = 0;
  g_exec.timer_id = discord_timer_interval(
      client, &exec_tick, &exec_status_changed, &g_exec, 0, EXEC_TICK_MS, -1);
}
