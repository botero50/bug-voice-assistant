"""Real email over IMAP: the newest messages of each mailbox listed in server/mail_accounts.json.

That file is never committed (it holds app passwords). Example:
  [{"name": "Gmail", "host": "imap.gmail.com", "user": "you@gmail.com", "password": "abcd efgh ijkl mnop"}]

Gmail: turn on 2-step verification, then create an "App password" (Google Account > Security) and use
it as the password. Outlook/Microsoft 365: host "outlook.office365.com"; many work accounts have
password sign-in disabled by the administrator, and then only OAuth works (not built yet).

Messages are read with PEEK, so Bug never marks anything as read.
"""
import email
import html
import imaplib
import json
import logging
import re
import time
from datetime import datetime, timedelta, timezone
from email.header import decode_header, make_header
from email.utils import parsedate_to_datetime
from pathlib import Path

log = logging.getLogger("bug")

ACCOUNTS_FILE = Path(__file__).with_name("mail_accounts.json")
COLOMBIA = timezone(timedelta(hours=-5))
PER_ACCOUNT = 6
SHOWN = 4
CACHE_SECONDS = 90
BODY_CHARS = 700
MONTHS_SHORT = ["ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"]

_cache = {"at": 0.0, "mails": []}


def accounts():
    try:
        data = json.loads(ACCOUNTS_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []
    return [a for a in data if a.get("host") and a.get("user") and a.get("password")]


def configured():
    return bool(accounts())


def _text(value):
    try:
        return str(make_header(decode_header(value or ""))).strip()
    except Exception:
        return (value or "").strip()


def _sender(message):
    name, address = email.utils.parseaddr(_text(message.get("From")))
    return name or address or "Desconocido"


def _body(message):
    """Plain text of the message; HTML is stripped when there is no plain part."""
    plain, rich = "", ""
    for part in message.walk():
        if part.get_content_maintype() != "text" or part.get_filename():
            continue
        try:
            text = part.get_payload(decode=True).decode(part.get_content_charset() or "utf-8", errors="replace")
        except Exception:
            continue
        if part.get_content_subtype() == "plain" and not plain:
            plain = text
        elif part.get_content_subtype() == "html" and not rich:
            rich = text
    if not plain and rich:
        rich = re.sub(r"(?is)<(script|style|head).*?</\1>", " ", rich)
        rich = re.sub(r"(?i)<br\s*/?>|</p>|</div>", "\n", rich)
        plain = html.unescape(re.sub(r"<[^>]+>", " ", rich))
    lines = [ln.strip() for ln in plain.splitlines() if ln.strip() and not ln.lstrip().startswith(">")]
    text = re.sub(r"\s+", " ", " ".join(lines))
    return text if len(text) <= BODY_CHARS else text[: BODY_CHARS - 1].rsplit(" ", 1)[0] + "…"


def _time_label(moment):
    if moment is None:
        return ""
    moment = moment.astimezone(COLOMBIA)
    today = datetime.now(COLOMBIA).date()
    if moment.date() == today:
        return f"{moment.hour % 12 or 12}:{moment.minute:02d} {'am' if moment.hour < 12 else 'pm'}"
    if moment.date() == today - timedelta(days=1):
        return "ayer"
    return f"{moment.day} {MONTHS_SHORT[moment.month - 1]}"


def parse(raw, unread):
    """One IMAP message (bytes) -> the dict the Email screen uses, plus its date for sorting."""
    message = email.message_from_bytes(raw)
    try:
        moment = parsedate_to_datetime(message.get("Date"))
        if moment.tzinfo is None:
            moment = moment.replace(tzinfo=timezone.utc)
    except Exception:
        moment = None
    return {"from": _sender(message), "subject": _text(message.get("Subject")) or "(sin asunto)",
            "time": _time_label(moment), "unread": unread, "body": _body(message) or "(sin texto)",
            "_when": moment or datetime.fromtimestamp(0, timezone.utc)}


def _fetch_account(account):
    mails = []
    box = imaplib.IMAP4_SSL(account["host"], int(account.get("port", 993)), timeout=15)
    try:
        box.login(account["user"], account["password"])
        box.select("INBOX", readonly=True)
        _, data = box.search(None, "ALL")
        for number in data[0].split()[-PER_ACCOUNT:]:
            _, reply = box.fetch(number, "(FLAGS BODY.PEEK[])")
            raw = next(part[1] for part in reply if isinstance(part, tuple))
            flags = next((part[0] for part in reply if isinstance(part, tuple)), b"")
            mails.append(parse(raw, b"\\Seen" not in flags))
    finally:
        try:
            box.logout()
        except Exception:
            pass
    return mails


def newest():
    """The newest messages over all mailboxes, newest first. Keeps the last result when a server is down."""
    if time.time() - _cache["at"] < CACHE_SECONDS:
        return _cache["mails"]
    mails = []
    for account in accounts():
        try:
            mails += _fetch_account(account)
        except Exception as error:
            log.warning("Mailbox %s failed: %s", account.get("name", account["host"]), error.__class__.__name__)
            return _cache["mails"] if _cache["mails"] else mails
    mails.sort(key=lambda m: m["_when"], reverse=True)
    _cache.update(at=time.time(), mails=[{k: v for k, v in m.items() if k != "_when"} for m in mails[:SHOWN]])
    log.info("Email: %d messages", len(_cache["mails"]))
    return _cache["mails"]
