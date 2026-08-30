#include <concord/discord.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
void temp_command(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;

  char output[128] = "";
  FILE *fp = popen("vcgencmd measure_temp 2>&1", "r");
  if (!fp) {
    struct discord_create_message reply = { .content = "failed to read temp, couldn't spawn process" };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  if (!fgets(output, sizeof output, fp)) {
    output[0] = '\0';
  }

  int status = pclose(fp);

  /* strip trailing newline, same as Python's .strip() */
  size_t len = strlen(output);
  while (len > 0 && (output[len-1] == '\n' || output[len-1] == '\r')) {
    output[--len] = '\0';
  }

  char buf[160];
  if (status != 0 || !*output) {
    snprintf(buf, sizeof buf, "failed to read temp, ret = %d", WEXITSTATUS(status));
  } else {
    snprintf(buf, sizeof buf, "%s", output);
  }

  struct discord_create_message reply = { .content = buf };
  discord_create_message(client, event->channel_id, &reply, NULL);
}
