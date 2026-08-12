#!/usr/bin/env python3
"""Tests for daemon/transcript_stats.py -- the activity-heatmap scanner.

Writes synthetic JSONL transcripts under tmp_path and points a fresh
TranscriptStats instance at them, so these tests never touch the user's real
~/.claude/projects.

Run: daemon/.venv/bin/python -m pytest daemon/tests/test_transcript_stats.py -q
"""
import datetime
import json
import time

from daemon import transcript_stats as ts  # noqa: E402


# --- helpers -----------------------------------------------------------------

def write_session(root, slug, records, session_uuid=None):
    """Write one session transcript file under root/<slug>/<uuid>.jsonl."""
    proj_dir = root / slug
    proj_dir.mkdir(parents=True, exist_ok=True)
    uuid = session_uuid or slug
    path = proj_dir / f"{uuid}.jsonl"
    with open(path, "w") as f:
        for rec in records:
            f.write(json.dumps(rec) + "\n")
    return path


def assistant_record(timestamp, session_id="sess-1", input_tokens=0, output_tokens=0,
                      cache_creation_input_tokens=0, cache_read_input_tokens=0,
                      model="claude-opus-5"):
    return {
        "type": "assistant",
        "timestamp": timestamp,
        "sessionId": session_id,
        "session_id": session_id,
        "message": {
            "model": model,
            "usage": {
                "input_tokens": input_tokens,
                "output_tokens": output_tokens,
                "cache_creation_input_tokens": cache_creation_input_tokens,
                "cache_read_input_tokens": cache_read_input_tokens,
            },
        },
    }


def epoch_utc(year, month, day, hour=0, minute=0, second=0) -> float:
    return datetime.datetime(year, month, day, hour, minute, second,
                              tzinfo=datetime.timezone.utc).timestamp()


def iso_utc(year, month, day, hour=0, minute=0, second=0) -> str:
    return f"{year:04d}-{month:02d}-{day:02d}T{hour:02d}:{minute:02d}:{second:02d}.000Z"


# --- basic shape ---------------------------------------------------------

def test_heatmap_is_168_chars_of_digits(tmp_path):
    now = epoch_utc(2026, 8, 12, 15, 0, 0)
    write_session(tmp_path, "proj1", [
        assistant_record(iso_utc(2026, 8, 12, 10, 0, 0), output_tokens=500),
        assistant_record(iso_utc(2026, 8, 10, 3, 0, 0), input_tokens=50),
    ])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=now)
    assert len(result["hm"]) == 168
    assert set(result["hm"]) <= set("0123456789")


def test_empty_input_yields_all_zero(tmp_path):
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=epoch_utc(2026, 8, 12, 12, 0, 0))
    assert result["hm"] == "0" * 168
    assert result["tt"] == 0
    assert result["tsn"] == 0
    assert 0 <= result["hd"] <= 6


def test_nonexistent_root_yields_all_zero(tmp_path):
    stats = ts.TranscriptStats(tmp_path / "does-not-exist", rescan_interval=0)
    result = stats.activity_fields(now=epoch_utc(2026, 8, 12, 12, 0, 0))
    assert result["hm"] == "0" * 168


# --- normalization ---------------------------------------------------------

def test_busiest_cell_normalizes_to_nine(tmp_path):
    now = epoch_utc(2026, 8, 12, 12, 0, 0)  # "today" local == 2026-08-12 (UTC host or not, see below)
    # Use UTC timestamps at local noon-ish so TZ doesn't push them across a day
    # boundary on any reasonable host timezone; the busiest-cell relationship
    # under test doesn't depend on which exact hour they land in.
    write_session(tmp_path, "proj1", [
        assistant_record(iso_utc(2026, 8, 12, 12, 0, 0), session_id="a", output_tokens=1000),
        assistant_record(iso_utc(2026, 8, 12, 13, 0, 0), session_id="a", output_tokens=400),
    ])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=now)
    assert "9" in result["hm"]
    assert result["hm"].count("9") >= 1
    # The lesser cell (400 vs busiest 1000 -> 400/1000*9 = 3.6 -> rounds to 4)
    # must be strictly less than 9 and non-zero.
    counts = {c: result["hm"].count(c) for c in set(result["hm"])}
    assert counts.get("0", 0) == 168 - 2  # exactly the two populated cells are non-zero


def test_zero_cell_stays_zero_even_when_busy_cells_exist(tmp_path):
    now = epoch_utc(2026, 8, 12, 12, 0, 0)
    write_session(tmp_path, "proj1", [
        assistant_record(iso_utc(2026, 8, 12, 5, 0, 0), output_tokens=99999),
    ])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=now)
    # Every hour except the one populated hour must be exactly '0'.
    assert result["hm"].count("0") == 167
    assert result["hm"].count("9") == 1


# --- malformed input --------------------------------------------------------

def test_malformed_and_unrelated_lines_are_skipped_without_raising(tmp_path):
    proj_dir = tmp_path / "proj1"
    proj_dir.mkdir(parents=True)
    path = proj_dir / "s1.jsonl"
    with open(path, "w") as f:
        f.write("{not valid json\n")
        f.write(json.dumps({"type": "summary", "sessionId": "s1"}) + "\n")
        f.write(json.dumps({"type": "user", "message": {"role": "user"}}) + "\n")
        f.write(json.dumps({"type": "assistant", "message": {"model": "x"}}) + "\n")  # no usage
        f.write("\n")  # blank line
        f.write(json.dumps(assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), output_tokens=42)) + "\n")

    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=epoch_utc(2026, 8, 12, 12, 0, 0))
    assert result["hm"].count("0") == 167
    assert result["hm"].count("9") == 1
    assert result["tsn"] == 1


# --- mtime/size cache ---------------------------------------------------------

def test_unchanged_file_is_not_reparsed(tmp_path):
    write_session(tmp_path, "proj1", [assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), output_tokens=10)])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)

    calls = []
    original = stats._parse_file

    def counting(path):
        calls.append(path)
        return original(path)

    stats._parse_file = counting

    now = epoch_utc(2026, 8, 12, 12, 0, 0)
    stats.activity_fields(now=now)
    assert len(calls) == 1

    # Second call, unchanged file, rescan_interval=0 so a rescan IS attempted
    # -- but the file's (mtime, size) haven't moved, so it must not reparse.
    stats.activity_fields(now=now + 1)
    assert len(calls) == 1

    # Modify the file's contents (changes size, and almost always mtime) ->
    # must be reparsed.
    time.sleep(0.01)
    write_session(tmp_path, "proj1", [
        assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), output_tokens=10),
        assistant_record(iso_utc(2026, 8, 12, 10, 0, 0), output_tokens=20),
    ])
    stats.activity_fields(now=now + 2)
    assert len(calls) == 2


def test_rescan_interval_throttles_full_rescans(tmp_path):
    write_session(tmp_path, "proj1", [assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), output_tokens=10)])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=300)

    calls = []
    original = stats._parse_file

    def counting(path):
        calls.append(path)
        return original(path)

    stats._parse_file = counting

    now = epoch_utc(2026, 8, 12, 12, 0, 0)
    stats.activity_fields(now=now)
    assert len(calls) == 1

    # New session file appears, but we're well inside the rescan window ->
    # the scanner must not even look at the filesystem again.
    write_session(tmp_path, "proj2", [assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), output_tokens=999)])
    result = stats.activity_fields(now=now + 30)
    assert len(calls) == 1
    assert result["tt"] == 0  # still reflects the pre-rescan (cached) state

    # Once the interval elapses, the new file must be picked up.
    result = stats.activity_fields(now=now + 301)
    assert len(calls) == 2
    assert result["tt"] > 0


# --- session counting -----------------------------------------------------

def test_distinct_sessions_today_counted_once_each(tmp_path):
    now = epoch_utc(2026, 8, 12, 20, 0, 0)
    write_session(tmp_path, "proj1", [
        assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), session_id="s1", output_tokens=10),
        assistant_record(iso_utc(2026, 8, 12, 10, 0, 0), session_id="s1", output_tokens=20),
        assistant_record(iso_utc(2026, 8, 12, 11, 0, 0), session_id="s2", output_tokens=5),
        # Yesterday's session shouldn't count toward today's tsn.
        assistant_record(iso_utc(2026, 8, 11, 11, 0, 0), session_id="s3", output_tokens=5),
    ])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=now)
    assert result["tsn"] == 2


def test_today_total_is_in_thousands(tmp_path):
    now = epoch_utc(2026, 8, 12, 20, 0, 0)
    write_session(tmp_path, "proj1", [
        assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), output_tokens=1500),
        assistant_record(iso_utc(2026, 8, 12, 10, 0, 0), output_tokens=1500),
    ])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=now)
    assert result["tt"] == 3  # 3000 tokens -> 3 (thousands)


# --- token summation ---------------------------------------------------------
#
# hm (heatmap) and tt (today headline) deliberately use DIFFERENT token-field
# sets -- see the "two different token definitions" note in
# transcript_stats.py's module docstring. hm sums all four usage fields
# (activity/intensity proxy); tt sums only input+output+cache_creation,
# excluding cache_read_input_tokens (a "how much did I use" number that a
# cache-read-dominated total would make meaningless).

def test_heatmap_sums_all_four_fields_including_cache_read(tmp_path):
    now = epoch_utc(2026, 8, 12, 20, 0, 0)
    write_session(tmp_path, "proj1", [
        assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), input_tokens=100, output_tokens=200,
                          cache_creation_input_tokens=300, cache_read_input_tokens=400),
    ])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=now)
    # 1000 total tokens (100+200+300+400) -> the single populated cell is
    # both the only nonzero cell and the busiest -> '9'.
    assert result["hm"].count("9") == 1
    assert result["hm"].count("0") == 167


def test_today_headline_excludes_cache_read_tokens(tmp_path):
    """A large cache_read_input_tokens must NOT inflate tt -- it's a re-read
    of already-cached context, not new work, and in real transcripts it can
    dwarf everything else (a real turn seen while building this had 51,306
    cache-creation tokens against a request that itself read back 0; other
    turns in the same session read back tens of thousands from a prior
    write). If this regresses, tt goes back to reading like nonsense on the
    device (e.g. "today: 120M tokens")."""
    now = epoch_utc(2026, 8, 12, 20, 0, 0)
    write_session(tmp_path, "proj1", [
        assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), input_tokens=1000, output_tokens=2000,
                          cache_creation_input_tokens=3000, cache_read_input_tokens=500_000),
    ])
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=now)
    # New-work total = 1000+2000+3000 = 6000 -> tt == 6 (thousand).
    # If cache_read leaked in, total would be 506000 -> tt == 506.
    assert result["tt"] == 6


def test_heatmap_and_headline_can_disagree_on_which_hour_is_busiest(monkeypatch, tmp_path):
    """Direct proof the two definitions diverge: an hour that is ALL cache
    reads (zero new work) must still show as the busiest heatmap cell, while
    contributing nothing to the "today" headline total.

    Forces TZ=UTC so the record timestamps (also UTC) land in the exact
    hours asserted below, independent of the host machine's timezone.
    """
    monkeypatch.setenv("TZ", "UTC")
    time.tzset()
    try:
        now = epoch_utc(2026, 8, 12, 20, 0, 0)
        write_session(tmp_path, "proj1", [
            # Hour 9: pure cache-read replay, no new work at all.
            assistant_record(iso_utc(2026, 8, 12, 9, 0, 0), cache_read_input_tokens=90_000),
            # Hour 10: real new work, but far less total volume than hour 9.
            assistant_record(iso_utc(2026, 8, 12, 10, 0, 0), output_tokens=1_000),
        ])
        stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
        result = stats.activity_fields(now=now)
        idx_hour9 = 6 * 24 + 9
        idx_hour10 = 6 * 24 + 10
        assert result["hm"][idx_hour9] == "9"   # busiest by total volume
        assert result["hm"][idx_hour10] == "1"  # present, but far less busy
        assert result["tt"] == 1  # only hour 10's 1000 new-work tokens count
    finally:
        monkeypatch.delenv("TZ", raising=False)
        time.tzset()


# --- hour bucketing in local time ---------------------------------------------

def test_hour_bucketing_uses_local_time(monkeypatch, tmp_path):
    """Force a fixed, non-UTC, non-DST timezone and verify a UTC timestamp
    lands in the grid cell for the correctly-converted LOCAL day/hour,
    computed independently of the module's own conversion."""
    monkeypatch.setenv("TZ", "Etc/GMT+5")  # POSIX sign is inverted: fixed UTC-5, no DST
    time.tzset()
    try:
        # 2026-08-12T02:00:00Z in UTC-5 is 2026-08-11T21:00:00 local.
        record_ts = iso_utc(2026, 8, 12, 2, 0, 0)
        now = epoch_utc(2026, 8, 12, 2, 0, 0)  # "now" -> local today == 2026-08-11
        write_session(tmp_path, "proj1", [assistant_record(record_ts, output_tokens=77)])

        stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
        result = stats.activity_fields(now=now)

        expected_local_date = datetime.date(2026, 8, 11)
        expected_hour = 21
        # Grid row 6 (last) is always "today", which local "now" resolves to
        # expected_local_date -- so the populated cell is row 6, hour 21.
        index = 6 * 24 + expected_hour
        assert result["hm"][index] == "9"
        assert result["hm"].count("0") == 167

        expected_first_row_date = expected_local_date - datetime.timedelta(days=6)
        assert result["hd"] == expected_first_row_date.weekday()
    finally:
        monkeypatch.delenv("TZ", raising=False)
        time.tzset()


def test_hd_is_weekday_of_first_row_monday_zero(tmp_path):
    now = epoch_utc(2026, 8, 12, 12, 0, 0)  # a Wednesday
    stats = ts.TranscriptStats(tmp_path, rescan_interval=0)
    result = stats.activity_fields(now=now)
    today_local = datetime.datetime.fromtimestamp(now).astimezone().date()
    first_row_date = today_local - datetime.timedelta(days=6)
    assert result["hd"] == first_row_date.weekday()
    assert 0 <= result["hd"] <= 6
