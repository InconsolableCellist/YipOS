# VRCX Integration

YipOS reads data from [VRCX](https://github.com/vrcx-team/VRCX)'s local SQLite database to display world history, friend activity, and more on the PDA wrist display.

## Database Location

| Platform | Path |
|----------|------|
| Linux    | `~/.config/VRCX/VRCX.sqlite3` |
| Windows  | `%APPDATA%\VRCX\VRCX.sqlite3` |

The database is opened **read-only** (`SQLITE_OPEN_READONLY`). VRCX must have been run at least once to populate it. A busy timeout of 1000ms is set since VRCX may be writing concurrently.

## Configuration

In `config.ini`:

```ini
[vrcx]
enabled = true
db_path =          ; empty = auto-detect from default path
```

The companion UI (Config tab) provides an Enable checkbox, path input, and Connect/Disconnect buttons. Toggling Enable auto-saves immediately.

## User Prefix

Most tables are prefixed with the VRChat user ID (dashes stripped), e.g.:

```
usrcff9574d5e52436689e4e75c1e7fb5bd_feed_online_offline
```

The prefix is auto-detected by finding the table matching `*_friend_log_current`. This happens on `Open()` and is stored in `user_prefix_`. Tables without this prefix (like `gamelog_location`) are global.

## Database Schema

### Global Tables (no user prefix)

#### `gamelog_location` — World Visit History

```sql
CREATE TABLE gamelog_location (
    id INTEGER PRIMARY KEY,
    created_at TEXT,          -- "2026-03-15 12:34:56"
    location TEXT,            -- full instance string (see below)
    world_id TEXT,            -- "wrld_xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
    world_name TEXT,          -- human-readable name
    time INTEGER,             -- time spent in seconds
    group_name TEXT,          -- VRC group name if group instance, else empty
    UNIQUE(created_at, location)
);
```

**Status: Implemented** — `VRCXData::GetWorlds()`, displayed in WORLDS screen.

#### `gamelog_join_leave` — Player Joins/Leaves (6973 rows)

```sql
CREATE TABLE gamelog_join_leave (
    id INTEGER PRIMARY KEY,
    created_at TEXT,
    type TEXT,                -- "join" or "leave"
    display_name TEXT,
    location TEXT,
    user_id TEXT,
    time INTEGER,
    UNIQUE(created_at, type, display_name)
);
```

**Status: Not implemented.** Could be used to show who was in a world with you.

#### `gamelog_portal_spawn` — Portal Events

```sql
CREATE TABLE gamelog_portal_spawn (
    id INTEGER PRIMARY KEY,
    created_at TEXT,
    display_name TEXT,        -- who dropped the portal
    location TEXT,
    user_id TEXT,
    instance_id TEXT,         -- dedicated instance_id column
    world_name TEXT,
    UNIQUE(created_at, display_name)
);
```

**Status: Not implemented.**

#### `cache_world` — World Metadata Cache

```sql
CREATE TABLE cache_world (
    id TEXT PRIMARY KEY,      -- world_id
    added_at TEXT,
    author_id TEXT,
    author_name TEXT,
    created_at TEXT,
    description TEXT,
    image_url TEXT,
    thumbnail_image_url TEXT,
    name TEXT,
    release_status TEXT,
    updated_at TEXT,
    version INTEGER
);
```

**Status: Not implemented.** May be empty — VRCX populates this on demand. If populated, could enrich the WORLD detail screen with author and description.

### User-Prefixed Tables

All use `{user_prefix}_` before the table name. The prefix is the user ID with dashes stripped: `usr_cff9574d-5e52-4366-89e4-e75c1e7fb5bd` becomes `usrcff9574d5e52436689e4e75c1e7fb5bd`.

#### `{prefix}_feed_online_offline` — Friend Online/Offline Activity (73k rows)

```sql
CREATE TABLE {prefix}_feed_online_offline (
    id INTEGER PRIMARY KEY,
    created_at TEXT,
    user_id TEXT,
    display_name TEXT,
    type TEXT,                -- "Online" or "Offline"
    location TEXT,
    world_name TEXT,
    time INTEGER,
    group_name TEXT
);
```

**Status: Query implemented** (`VRCXData::GetFeed()`), **screen not built.** Planned for FEED sub-screen on VRCX landing page. Note: cannot filter by favorites — VRChat favorite groups are API-only, not stored locally.

#### `{prefix}_feed_status` — Status Changes (16k rows)

```sql
CREATE TABLE {prefix}_feed_status (
    id INTEGER PRIMARY KEY,
    created_at TEXT,
    user_id TEXT,
    display_name TEXT,
    status TEXT,              -- "active", "join me", "ask me", "busy", etc.
    status_description TEXT,  -- custom status message
    previous_status TEXT,
    previous_status_description TEXT
);
```

**Status: Query implemented** (`VRCXData::GetStatus()`), **screen not built.** Planned for STATUS sub-screen.

#### `{prefix}_notifications` — Notifications (337 rows)

```sql
CREATE TABLE {prefix}_notifications (
    id TEXT PRIMARY KEY,
    created_at TEXT,
    type TEXT,                -- "invite", "friendRequest", etc.
    sender_user_id TEXT,
    sender_username TEXT,
    receiver_user_id TEXT,
    message TEXT,
    world_id TEXT,
    world_name TEXT,
    image_url TEXT,
    invite_message TEXT,
    request_message TEXT,
    response_message TEXT,
    expired INTEGER
);
```

**Status: Query implemented** (`VRCXData::GetNotifications()`), **screen not built.** Planned for NOTIF sub-screen.

#### `{prefix}_feed_gps` — Friend Location History (45k rows)

```sql
CREATE TABLE {prefix}_feed_gps (
    id INTEGER PRIMARY KEY,
    created_at TEXT,
    user_id TEXT,
    display_name TEXT,
    location TEXT,
    world_name TEXT,
    previous_location TEXT,
    time INTEGER,
    group_name TEXT
);
```

**Status: Not implemented.** Tracks where friends go. Could be useful for a "where are my friends" screen.

#### `{prefix}_friend_log_current` — Current Friend List (1107 rows)

```sql
CREATE TABLE {prefix}_friend_log_current (
    user_id TEXT PRIMARY KEY,
    display_name TEXT,
    trust_level TEXT,         -- "Trusted User", "Known User", etc.
    friend_number INTEGER DEFAULT 0
);
```

**Status: Used for user prefix detection only.** Could be used to display friend count or filter feeds by known friends.

#### Other Tables

- `{prefix}_feed_avatar` (64k rows) — Avatar change history
- `{prefix}_feed_bio` (1.7k rows) — Bio change history
- `{prefix}_friend_log_history` (418 rows) — Friend add/remove history
- `{prefix}_notes` (10 rows) — User notes
- `{prefix}_avatar_history` (54 rows) — Your own avatar history
- `cache_avatar` (54 rows) — Avatar metadata cache
- `gamelog_video_play` (363 rows) — Video player URLs
- `gamelog_event` (62 rows) — Misc events

## Location String Format

The `location` column encodes the full instance address:

```
wrld_xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx:12345~type(owner)~region(xx)
```

Components after the colon:
- **Instance number**: `12345`
- **Access type**: `~private(usr_xxx)`, `~friends(usr_xxx)`, `~hidden(usr_xxx)`, `~group(grp_xxx)~groupAccessType(plus)`, or absent (public)
- **Region**: `~region(us)`, `~region(use)`, `~region(eu)`, `~region(jp)`

Parsing in C++ (see `VRCXWorldDetailScreen`):
- Instance type: search for `~private(`, `~hidden(`, `~friends(`, `~group(` substrings
- Region: extract between `~region(` and `)`
- World ID: everything before the `:`
- Instance ID: everything after the `:` (needed for rejoin URL)

## Rejoin Mechanism

To rejoin a world instance, open a browser to:

```
https://vrchat.com/home/launch?worldId=wrld_xxx&instanceId=12345~type(owner)~region(xx)
```

Split the `location` string at `:` — left part is `worldId`, right part is `instanceId`.

Implementation:
- Linux: `xdg-open '<url>' &`
- Windows: `start "" "<url>"`

The `vrchat://launch?id=...` URI scheme also exists but doesn't work reliably on Linux (KIO doesn't recognize the scheme). The browser URL is more portable — VRChat's website handles the redirect to the client.

## Screen Architecture

### VRCX Landing (macro slot 8)

3x2 tile grid. Currently only WORLDS is active (inverted = touchable). FEED, STATUS, NOTIF tiles exist but are not wired to screens yet.

### WORLDS List (macro slot 9)

Paginated world visit history. 6 rows per page, up to 300 entries loaded.

- **Joystick**: Cycle highlight cursor down through items (wraps, auto-advances pages)
- **TR**: Select highlighted world → push WORLD detail screen
- **ML/BL**: Page up/down
- Right border `→` glyph hints that TR selects
- Status bar shows `n/total` position indicator

Selected row is rendered with inverted text (compact: `"name dur"`, no gap padding). Cursor moves only redraw the two affected rows — no macro re-stamp.

### WORLD Detail (macro slot 10)

Shows selected world info:
- Row 1: World name
- Row 2: Instance type (Public/Private/Friends/Friends+/Group) + Region
- Row 3: Time spent
- Row 4: Group name (if applicable)
- Row 5: Timestamp
- Row 6: Inverted REJOIN button (touch 53 to activate)

REJOIN opens the browser launch URL. TL goes back to the WORLDS list.

## SQLite3 Dependency

Uses the SQLite amalgamation (sqlite3.c + sqlite3.h) compiled as a static library. Build flags:
- `SQLITE_OMIT_LOAD_EXTENSION` — not needed, reduces attack surface
- `SQLITE_THREADSAFE=1` — safe for concurrent access

Linux additionally links `-ldl` (required by SQLite internals).

## Limitations

- **Favorite friends filtering**: VRChat favorite groups are stored via the API only, not in the local VRCX database. Filtering feeds by favorites requires an alternative approach (e.g. maintaining a local favorites list in NVRAM).
- **`cache_world` often empty**: VRCX populates this on demand. World metadata (author, description) may not be available for all visited worlds.
- **Read-only access**: We never write to the VRCX database. All persistent YipOS state goes in `config.ini`.
- **Concurrent access**: VRCX may be writing while we read. The busy timeout and read-only mode handle this gracefully.
