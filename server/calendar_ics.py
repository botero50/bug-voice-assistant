"""Real calendar events from ICS links (Google Calendar "secret address in iCal format", Outlook
"published calendar" link) or local .ics files. No account login or API key needed.

Where the links go (never committed to git, because a secret link is a password to your agenda):
  * the BUG_ICS_URLS environment variable (several separated by ';'), or
  * the file server/calendar_sources.txt, one link or file path per line, '#' for comments.

Handled: single events, all-day events, UTC / time-zone / floating times, daily, weekly (BYDAY),
monthly and yearly repeats with INTERVAL, COUNT, UNTIL and EXDATE. Not handled: changed or cancelled
single occurrences of a repeating event (RECURRENCE-ID).
"""
import logging
import os
import time
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

import requests

log = logging.getLogger("bug")

COLOMBIA = timezone(timedelta(hours=-5))
SOURCES_FILE = Path(__file__).with_name("calendar_sources.txt")
CACHE_SECONDS = 300
MAX_ITERATIONS = 20000
WEEKDAYS = ["MO", "TU", "WE", "TH", "FR", "SA", "SU"]

_cache = {"at": 0.0, "events": []}


def sources():
    found = [s.strip() for s in os.environ.get("BUG_ICS_URLS", "").split(";") if s.strip()]
    if SOURCES_FILE.exists():
        for line in SOURCES_FILE.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                found.append(line)
    return found


def configured():
    return bool(sources())


def _unfold(text):
    lines = []
    for raw in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        if raw[:1] in (" ", "\t") and lines:
            lines[-1] += raw[1:]
        else:
            lines.append(raw)
    return lines


def _unescape(value):
    return value.replace("\\n", " ").replace("\\N", " ").replace("\\,", ",").replace("\\;", ";").replace("\\\\", "\\")


def _split(line):
    head, _, value = line.partition(":")
    name, *params = head.split(";")
    return name.upper(), dict(p.split("=", 1) for p in params if "=" in p), value


def _timezone(name):
    try:
        from zoneinfo import ZoneInfo
        return ZoneInfo(name)
    except Exception:
        return COLOMBIA  # no time-zone data for this name: assume the user's own


def _parse_time(value, params):
    """(datetime in Colombia time or date, all_day)."""
    value = value.strip()
    if params.get("VALUE") == "DATE" or (len(value) == 8 and value.isdigit()):
        return datetime.strptime(value[:8], "%Y%m%d").date(), True
    if value.endswith("Z"):
        return datetime.strptime(value, "%Y%m%dT%H%M%SZ").replace(tzinfo=timezone.utc).astimezone(COLOMBIA), False
    naive = datetime.strptime(value[:15], "%Y%m%dT%H%M%S")
    zone = _timezone(params["TZID"]) if "TZID" in params else COLOMBIA
    return naive.replace(tzinfo=zone).astimezone(COLOMBIA), False


def _parse(text):
    events, current = [], None
    for line in _unfold(text):
        if line == "BEGIN:VEVENT":
            current = {"exdates": set()}
        elif line == "END:VEVENT":
            if current and "start" in current and current.get("status") != "CANCELLED" and "recurrence_id" not in current:
                events.append(current)
            current = None
        elif current is not None and ":" in line:
            name, params, value = _split(line)
            try:
                if name == "DTSTART":
                    current["start"], current["all_day"] = _parse_time(value, params)
                elif name == "SUMMARY":
                    current["title"] = _unescape(value)
                elif name == "LOCATION":
                    current["place"] = _unescape(value)
                elif name == "STATUS":
                    current["status"] = value.strip().upper()
                elif name == "RECURRENCE-ID":
                    current["recurrence_id"] = value
                elif name == "RRULE":
                    current["rule"] = dict(p.split("=", 1) for p in value.split(";") if "=" in p)
                elif name == "EXDATE":
                    for part in value.split(","):
                        moment, _ = _parse_time(part, params)
                        current["exdates"].add(moment if isinstance(moment, date) and not isinstance(moment, datetime) else moment.date())
            except Exception as error:
                log.warning("Calendar line skipped (%s): %s", name, error.__class__.__name__)
    return events


def _load():
    if time.time() - _cache["at"] < CACHE_SECONDS:
        return _cache["events"]
    events = []
    for source in sources():
        try:
            if source.lower().startswith(("http://", "https://", "webcal://")):
                url = source.replace("webcal://", "https://", 1)
                text = requests.get(url, timeout=15, headers={"User-Agent": "BugVoiceAssistant/0.1"}).text
            else:
                text = Path(source).read_text(encoding="utf-8", errors="replace")
            events += _parse(text)
        except Exception as error:
            log.warning("Calendar source failed (%s): %s", source.split("?")[0][-40:], error.__class__.__name__)
    _cache.update(at=time.time(), events=events)
    log.info("Calendar: %d events from %d sources", len(events), len(sources()))
    return events


def _day(start):
    return start if not isinstance(start, datetime) else start.date()


def _occurs(event, target):
    first = _day(event["start"])
    if target < first or target in event["exdates"]:
        return False
    rule = event.get("rule")
    if not rule:
        return target == first
    freq = rule.get("FREQ")
    interval = max(int(rule.get("INTERVAL", 1)), 1)
    until = None
    if "UNTIL" in rule:
        try:
            until = datetime.strptime(rule["UNTIL"][:8], "%Y%m%d").date()
        except ValueError:
            pass
    if until and target > until:
        return False
    limit = int(rule["COUNT"]) if "COUNT" in rule else None
    byday = [d[-2:] for d in rule.get("BYDAY", "").split(",") if d] or [WEEKDAYS[first.weekday()]]

    def matches(day):
        if freq == "DAILY":
            return (day - first).days % interval == 0
        if freq == "WEEKLY":
            weeks = ((day - timedelta(days=day.weekday())) - (first - timedelta(days=first.weekday()))).days // 7
            return weeks % interval == 0 and WEEKDAYS[day.weekday()] in byday
        if freq == "MONTHLY":
            return day.day == first.day and ((day.year - first.year) * 12 + day.month - first.month) % interval == 0
        if freq == "YEARLY":
            return (day.month, day.day) == (first.month, first.day) and (day.year - first.year) % interval == 0
        return False

    if limit is None:
        return matches(target)
    seen, day = 0, first  # COUNT needs counting the occurrences from the start
    for _ in range(MAX_ITERATIONS):
        if day > target:
            break
        if matches(day) and day not in event["exdates"]:
            seen += 1
            if seen > limit:
                return False
            if day == target:
                return True
        day += timedelta(days=1)
    return False


def events_on(day):
    """The events of one day (a date), earliest first: dicts with hour, minute, all_day, title, place."""
    found = []
    for event in _load():
        if _occurs(event, day):
            start = event["start"]
            found.append({"all_day": event["all_day"],
                          "hour": 0 if event["all_day"] else start.hour,
                          "minute": 0 if event["all_day"] else start.minute,
                          "title": event.get("title", "Sin título"),
                          "place": event.get("place", "")})
    found.sort(key=lambda e: (not e["all_day"], e["hour"], e["minute"]))
    return found
