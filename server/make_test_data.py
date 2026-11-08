"""Makes the fixtures test_client.py needs: two tone "songs", two books (txt and epub) and a calendar
file with events relative to today. Everything goes into server/test_data/ (ignored by git).

Start the server for a test run with the variables this prints (run_tests.ps1 does all of it).
"""
import math
import struct
import wave
import zipfile
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).with_name("test_data")
COLOMBIA = timezone(timedelta(hours=-5))


def songs():
    folder = ROOT / "songs"
    folder.mkdir(parents=True, exist_ok=True)
    for name, hz, seconds in (("Shakira - Waka Waka", 440, 3), ("Carlos Vives - La Bicicleta", 660, 2)):
        with wave.open(str(folder / f"{name}.wav"), "w") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(22050)
            out.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * hz * i / 22050)))
                                     for i in range(22050 * seconds)))
    return folder


def books():
    folder = ROOT / "books"
    folder.mkdir(parents=True, exist_ok=True)
    sentences = [f"Esta es la oración número {n} del libro de prueba y tiene algunas palabras para llenar la página." for n in range(1, 41)]
    (folder / "Libro de Prueba.txt").write_text(" ".join(sentences), encoding="utf-8")

    chapter = "<html><head><title>x</title></head><body><h1>Capítulo uno</h1><p>" + " ".join(
        f"Frase épica {n} de la aventura del epub." for n in range(1, 30)) + "</p></body></html>"
    with zipfile.ZipFile(folder / "Aventura Epub.epub", "w") as book:
        book.writestr("mimetype", "application/epub+zip")
        book.writestr("META-INF/container.xml",
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">'
                      '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        book.writestr("OEBPS/content.opf",
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0"><manifest>'
                      '<item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/></manifest>'
                      '<spine><itemref idref="c1"/></spine></package>')
        book.writestr("OEBPS/ch1.xhtml", chapter)
    return folder


def calendar():
    today = datetime.now(COLOMBIA).date()
    tomorrow = today + timedelta(days=1)
    day = lambda d: d.strftime("%Y%m%d")
    ics = "\r\n".join([
        "BEGIN:VCALENDAR",
        "BEGIN:VEVENT", f"DTSTART:{day(today)}T150000Z", "SUMMARY:Reunión de prueba", "LOCATION:Teams\\, sala 2", "END:VEVENT",
        "BEGIN:VEVENT", f"DTSTART;TZID=America/Bogota:{day(today - timedelta(days=30))}T083000", "RRULE:FREQ=DAILY",
        "SUMMARY:Standup diario", "END:VEVENT",
        "BEGIN:VEVENT", f"DTSTART;VALUE=DATE:{day(tomorrow)}", "SUMMARY:Festivo de prueba", "END:VEVENT",
        "END:VCALENDAR", ""])
    path = ROOT / "calendar.ics"
    path.write_text(ics, encoding="utf-8")
    return path


if __name__ == "__main__":
    ROOT.mkdir(exist_ok=True)
    print(f"BUG_MUSIC_DIR={songs()}")
    print(f"BUG_BOOKS_DIR={books()}")
    print(f"BUG_ICS_URLS={calendar()}")
