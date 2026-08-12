#!/usr/bin/env python3
"""Activity heatmap analytics derived from Claude Code session transcripts.

Claude Code writes one append-only JSONL transcript per session under
``~/.claude/projects/<project-slug>/<session-uuid>.jsonl``. Each assistant
turn in that file carries a ``usage`` block (input/output/cache tokens) and a
UTC ``timestamp``. This module scans those transcripts and reduces them into
a 7-day x 24-hour token-volume heatmap plus a couple of "today" summary
numbers, meant to be merged into the BLE payload the daemon already sends to
the device.

Only TOKEN COUNTS are ever produced here — never dollars. Prices aren't in
the transcripts and hardcoding a price table goes stale silently (see
CLAUDE.md's "no hardcoded config values" rule); the device shows volume, not
cost.

Design notes
------------
* **Two different token definitions, used deliberately in two different
  places.** The heatmap (``hm``) sums ALL FOUR usage fields, including
  ``cache_read_input_tokens`` -- it's an *activity/intensity* proxy ("was
  this hour busy"), and cache reads still mean the API was actively working
  a big context on the user's behalf. The "today" headline (``tt``) sums
  only THREE fields, excluding ``cache_read_input_tokens`` -- it's a
  *"how much did I use today"* number a person reads at a glance, and cache
  reads (re-reads of context already paid for/cached) can outnumber
  everything else by 100x+, making a cache-read-inclusive headline
  nonsensical ("today: 120M tokens" conveys nothing). See TOKEN_FIELDS vs
  NEW_WORK_TOKEN_FIELDS below for the exact rationale. Don't let the two
  drift back into using the same set without re-reading this note.
* **Never re-read an unchanged file.** Each file's per-hour / per-session
  token breakdown is cached by path, keyed on (mtime, size). A file whose
  mtime AND size haven't moved since the last scan is reused from cache
  instead of re-parsed. Session transcripts are append-only, so this is
  exact, not approximate: an unchanged (mtime, size) pair means unchanged
  content.
* **The whole corpus is only rescanned every RESCAN_INTERVAL_SECONDS**
  (3 minutes by default), independent of the daemon's 60s poll cadence. A
  heatmap is a coarse, hour-bucketed picture -- nothing meaningfully changes
  at 60-second resolution -- so re-`stat()`ing ~600 files on every single
  poll would be pure overhead for no visible benefit. Between rescans,
  ``activity_fields()`` returns the previously computed dict at effectively
  zero cost (no filesystem I/O at all).
* **Synchronous, by design.** All the work here is blocking file I/O + JSON
  parsing, so this module never touches asyncio itself. The daemon
  integration wraps the (rate-limited, usually-cached) call in
  ``asyncio.to_thread`` so a cold scan can never stall the event loop --
  see the integration note in ``claude_usage_daemon.py``.
"""

import datetime
import json
import time
from pathlib import Path

DEFAULT_ROOT = Path.home() / ".claude" / "projects"

# How often a full corpus rescan (stat every transcript file, re-parse any
# that changed) is allowed to run. Chosen to sit well above the daemon's 60s
# poll interval -- the heatmap only needs to be "fresh within a few minutes",
# not second-accurate -- while still catching up quickly after a burst of
# Claude Code activity. Calls to activity_fields() in between just return the
# last computed dict.
RESCAN_INTERVAL_SECONDS = 180

# --- token-field sets: two different definitions for two different jobs ---
#
# TOKEN_FIELDS (all four) drives the HEATMAP (hm): a per-hour *activity*
# proxy, not a cost/usage number a person reads literally. Every one of
# these fields is a real thing the API did on the user's behalf during that
# hour -- including cache reads, which still mean a big context was actively
# reloaded and used for that turn -- so summing all four gives the truest
# picture of "how busy was this hour", which is all the heatmap needs to
# convey via a relative 0-9 scale. This mirrors how the API's own rate
# limits are consumed: every one of these fields counts against usage.
TOKEN_FIELDS = (
    "input_tokens",
    "output_tokens",
    "cache_creation_input_tokens",
    "cache_read_input_tokens",
)

# NEW_WORK_TOKEN_FIELDS (three -- deliberately excludes cache_read_input_tokens)
# drives the "today" HEADLINE (tt), a single absolute number rendered on a
# desk display as e.g. "today: 42K tokens". That number needs to mean
# something to a person, and cache reads don't fit: they're re-reads of
# context the session already paid to create, not new work, and in real
# transcripts they routinely dominate the total by 100x+ (one real turn
# inspected while building this: 51,306 cache-creation tokens on a request
# that read back 0 cache tokens; other turns in the same session read back
# tens of thousands from a prior turn's cache write). Including them turns
# "today: 42K tokens" into "today: 120M tokens", which conveys nothing about
# how much the user actually did today. input + output + cache_creation is
# the tokens a person would recognize as "what I used" -- fresh work plus
# the one-time cost of putting new context into the cache.
NEW_WORK_TOKEN_FIELDS = (
    "input_tokens",
    "output_tokens",
    "cache_creation_input_tokens",
)

GRID_DAYS = 7
GRID_HOURS = 24


def _sum_usage_fields(usage: dict, fields: tuple) -> int:
    total = 0
    for field in fields:
        try:
            total += int(usage.get(field) or 0)
        except (TypeError, ValueError):
            pass
    return total


def _parse_local_timestamp(ts) -> datetime.datetime | None:
    """Parse a transcript record's UTC ISO-8601 timestamp into local time.

    Transcript timestamps look like ``"2026-07-29T00:57:30.057Z"``. Returns
    None for anything that doesn't parse, so a malformed record can be
    skipped rather than raising.
    """
    if not isinstance(ts, str) or not ts:
        return None
    s = ts.strip()
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    try:
        dt = datetime.datetime.fromisoformat(s)
    except ValueError:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=datetime.timezone.utc)
    # Convert to the system's local timezone -- the heatmap must match hours
    # the way the user actually experienced them, not UTC.
    return dt.astimezone()


class TranscriptStats:
    """Incremental, cached scanner over a directory of Claude Code transcripts.

    One instance owns its own file cache and rescan clock, so tests can point
    an instance at a throwaway tmp_path without touching module-level state
    (and without touching the user's real ``~/.claude/projects``).
    """

    def __init__(self, root: Path | str = DEFAULT_ROOT,
                 rescan_interval: float = RESCAN_INTERVAL_SECONDS) -> None:
        self.root = Path(root)
        self.rescan_interval = rescan_interval
        self._file_cache: dict[str, dict] = {}
        self._last_scan_at: float | None = None
        self._last_result: dict | None = None

    # -- scanning ----------------------------------------------------------

    def _discover_files(self) -> list[Path]:
        if not self.root.is_dir():
            return []
        # Recursive, not one-level: alongside the top-level
        # <slug>/<session-uuid>.jsonl transcripts, Claude Code also writes
        # subagent transcripts nested a level deeper as
        # <slug>/<session-uuid>/subagents/agent-*.jsonl (their "sessionId"
        # field is the PARENT session's uuid, so they naturally fold into
        # that session's totals below rather than inventing extra sessions).
        # On this machine that's the large majority of files (553 of 589).
        return sorted(self.root.rglob("*.jsonl"))

    def _parse_file(self, path: Path) -> dict:
        """Parse one transcript file into per-(date, hour) token totals --
        both the full-volume total (all four usage fields, feeds the
        heatmap) and the new-work-only total (excludes cache reads, feeds
        the "today" headline) -- plus per-date sets of active session ids.
        Malformed/unrelated lines (metadata records, non-JSON lines, records
        with no usage block) are silently skipped -- transcripts mix many
        record types and only assistant turns with a usage block represent
        token activity."""
        hours_total: dict[tuple[str, int], int] = {}
        hours_new_work: dict[tuple[str, int], int] = {}
        sessions: dict[str, set] = {}
        try:
            raw = path.read_text(errors="replace")
        except OSError:
            return {"hours_total": hours_total, "hours_new_work": hours_new_work, "sessions": sessions}
        for line in raw.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(rec, dict) or rec.get("type") != "assistant":
                continue
            message = rec.get("message")
            if not isinstance(message, dict):
                continue
            usage = message.get("usage")
            if not isinstance(usage, dict):
                continue
            local_dt = _parse_local_timestamp(rec.get("timestamp"))
            if local_dt is None:
                continue
            total_tokens = _sum_usage_fields(usage, TOKEN_FIELDS)
            new_work_tokens = _sum_usage_fields(usage, NEW_WORK_TOKEN_FIELDS)
            date_key = local_dt.date().isoformat()
            hour = local_dt.hour
            cell_key = (date_key, hour)
            hours_total[cell_key] = hours_total.get(cell_key, 0) + total_tokens
            hours_new_work[cell_key] = hours_new_work.get(cell_key, 0) + new_work_tokens
            session_id = rec.get("sessionId") or rec.get("session_id")
            if session_id:
                sessions.setdefault(date_key, set()).add(session_id)
        return {"hours_total": hours_total, "hours_new_work": hours_new_work, "sessions": sessions}

    def _refresh(self) -> None:
        """Re-stat every transcript file; re-parse only those whose (mtime,
        size) changed since the last scan. Drops cache entries for files
        that disappeared (renamed/deleted) since the last scan."""
        seen: set[str] = set()
        for path in self._discover_files():
            key = str(path)
            seen.add(key)
            try:
                st = path.stat()
            except OSError:
                continue
            cached = self._file_cache.get(key)
            if cached is not None and cached["mtime"] == st.st_mtime and cached["size"] == st.st_size:
                continue  # unchanged -- reuse cached hours_total/hours_new_work/sessions as-is
            entry = self._parse_file(path)
            entry["mtime"] = st.st_mtime
            entry["size"] = st.st_size
            self._file_cache[key] = entry
        stale = set(self._file_cache) - seen
        for key in stale:
            del self._file_cache[key]

    # -- aggregation ---------------------------------------------------------

    def _aggregate(self, now: float) -> dict:
        today = datetime.datetime.fromtimestamp(now).astimezone().date()
        days = [today - datetime.timedelta(days=offset) for offset in range(GRID_DAYS - 1, -1, -1)]
        row_by_date = {day.isoformat(): i for i, day in enumerate(days)}

        # Heatmap grid: full volume (TOKEN_FIELDS, all 4) -- an activity/
        # intensity proxy, not a number read literally. See the module
        # docstring's "two different token definitions" note.
        grid = [[0] * GRID_HOURS for _ in days]
        for entry in self._file_cache.values():
            for (date_key, hour), tokens in entry["hours_total"].items():
                row = row_by_date.get(date_key)
                if row is not None and 0 <= hour < GRID_HOURS:
                    grid[row][hour] += tokens

        max_val = max((v for row in grid for v in row), default=0)

        def cell_char(v: int) -> str:
            if v <= 0 or max_val <= 0:
                return "0"
            level = round(v / max_val * 9)
            return str(max(1, min(9, level)))

        hm = "".join(cell_char(v) for row in grid for v in row)

        # "Today" headline: new-work-only (NEW_WORK_TOKEN_FIELDS, excludes
        # cache reads) -- see the module docstring for why this must NOT use
        # the same total as the heatmap above.
        today_key = today.isoformat()
        today_new_work_total = 0
        sessions_today: set = set()
        for entry in self._file_cache.values():
            for (date_key, _hour), tokens in entry["hours_new_work"].items():
                if date_key == today_key:
                    today_new_work_total += tokens
            sessions_today |= entry["sessions"].get(today_key, set())

        return {
            "hm": hm,
            "hd": days[0].weekday(),  # 0 = Monday, matches date.weekday()
            "tt": int(round(today_new_work_total / 1000)),
            "tsn": len(sessions_today),
        }

    # -- public API ----------------------------------------------------------

    def activity_fields(self, now: float | None = None) -> dict:
        """Return {"hm", "hd", "tt", "tsn"} for merging into the BLE payload.

        Rate-limited to a full rescan at most once per ``rescan_interval``
        seconds; calls in between return the previously computed dict with
        no filesystem access at all.
        """
        if now is None:
            now = time.time()
        due = (
            self._last_result is None
            or self._last_scan_at is None
            or (now - self._last_scan_at) >= self.rescan_interval
        )
        if due:
            self._refresh()
            self._last_result = self._aggregate(now)
            self._last_scan_at = now
        return dict(self._last_result)


# Module-level singleton used by the daemon so the file cache and rescan
# clock persist for the life of the process.
_default_stats: TranscriptStats | None = None


def _get_default() -> TranscriptStats:
    global _default_stats
    if _default_stats is None:
        _default_stats = TranscriptStats(DEFAULT_ROOT)
    return _default_stats


def activity_fields(now: float | None = None) -> dict:
    """Module-level convenience wrapper over the default TranscriptStats
    instance (rooted at ~/.claude/projects). This is what the daemon calls."""
    return _get_default().activity_fields(now=now)
