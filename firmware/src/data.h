#pragma once
#include <Arduino.h>

struct UsageData {
    float session_pct;       // utilization 0-100 (5h window Pro/Max; spending % Enterprise)
    int session_reset_mins;  // minutes until reset
    float weekly_pct;        // 7-day utilization (Pro/Max only; 0 for Enterprise)
    int weekly_reset_mins;   // minutes until weekly reset (Pro/Max only)
    char status[16];         // "allowed", "limited", etc.
    int session_window_mins; // length of the session window, from the API's own
                             // header naming; 0 = unknown, don't derive a pace
    int weekly_window_mins;  // same for the 7-day window
    char claim[12];          // which window is currently binding ("five_hour")
    char weekly_status[12];  // 7d window status; "" when the daemon didn't send it
    char overage[12];        // overage status ("rejected"/"allowed"), "" if absent
    char overage_reason[24]; // why overage is unavailable, "" if absent
    int  fallback_pct;       // fallback percentage, -1 when not reported

    // ---- Activity heatmap (from local session transcripts, not the API) ----
    // 7 days x 24 local hours, row-major, oldest day first. Each cell is that
    // hour's token volume normalised 0..9 against the busiest cell. All zero
    // until the daemon sends one.
    uint8_t heat[7 * 24];
    bool    heat_valid;
    int     heat_first_weekday;  // 0 = Monday, weekday of the FIRST row
    int     tokens_today_k;      // today's tokens, in thousands
    int     sessions_today;      // distinct sessions active today
    bool chime;              // play the session-reset chime; false unless daemon opts in
    bool enterprise;         // true = Enterprise spending-limit account
    int time_pct;            // 0-100: fraction of billing period elapsed (Enterprise)
    int period_days;         // total billing period length in days (Enterprise)
    char reset_date[12];     // formatted reset date e.g. "Jul 1" (Enterprise)
    long clock_epoch;        // local wall-clock epoch (s) from daemon; 0 = not provided
    int  clock_fmt;          // 12 or 24 (hour format from daemon); defaults to 24
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};
