import json, sqlite3

with open("settings.json") as f:
    data = json.load(f)

GUILD_ID = "1155152569040130101"

db = sqlite3.connect("customcoms.db")
db.execute("""CREATE TABLE IF NOT EXISTS customcoms (
    name TEXT PRIMARY KEY, response TEXT NOT NULL,
    cooldown INTEGER DEFAULT 0, created_by TEXT, uses INTEGER DEFAULT 0)""")

migrated, skipped, dropped_cooldowns, malformed = 0, 0, [], []

for bot_id, bot_data in data.items():
    guild = bot_data.get("GUILD", {}).get(GUILD_ID)
    if not guild:
        continue
    for name, cmd in guild.get("commands", {}).items():
        if not isinstance(cmd, dict):
            malformed.append(name)
            continue

        response = cmd.get("response")
        if not response:
            skipped += 1
            continue

        cooldowns = cmd.get("cooldowns", {}) or {}
        cooldown = cooldowns.get("member", 0)

        other_scopes = set(cooldowns.keys()) - {"member"}
        if other_scopes:
            dropped_cooldowns.append((cmd.get("command", name), other_scopes))

        db.execute(
            "INSERT OR REPLACE INTO customcoms (name, response, cooldown, created_by) VALUES (?, ?, ?, ?)",
            (cmd.get("command", name), response, cooldown, str(cmd.get("author", {}).get("id", "")))
        )
        migrated += 1

db.commit()
print(f"Migrated {migrated} commands, skipped {skipped} with no response.")
if malformed:
    print("Malformed (non-dict) entries skipped:", malformed)
if dropped_cooldowns:
    print("Non-member-scoped cooldowns dropped for:", dropped_cooldowns)
