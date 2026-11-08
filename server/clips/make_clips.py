"""Makes the voice the board speaks with when it answers on its own: short words and phrases in Salome's voice,
stored on the board (partition "storage") and joined together at run time.

Run with the server's Python:   python clips\\make_clips.py
Writes firmware/voice_clips.bin (flashed to the storage partition) and firmware/main/voice_clips.h (the ids).
"""
import asyncio
import re
import struct
import sys
import unicodedata
from pathlib import Path

import edge_tts
import miniaudio
import numpy as np

ROOT = Path(__file__).resolve().parent.parent.parent / "firmware"
VOICE = "es-CO-SalomeNeural"
RATE = 16000

NUMBERS = ["cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve", "diez", "once", "doce",
           "trece", "catorce", "quince", "dieciséis", "diecisiete", "dieciocho", "diecinueve", "veinte", "veintiuno",
           "veintidós", "veintitrés", "veinticuatro", "veinticinco", "veintiséis", "veintisiete", "veintiocho",
           "veintinueve"]
TENS = ["treinta", "cuarenta", "cincuenta", "sesenta", "setenta", "ochenta", "noventa", "cien"]
DAYS = ["lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"]
MONTHS = ["enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre",
          "noviembre", "diciembre"]

# (name, text) in the order of the ids. Numbers, tens, days and months get their own runs of consecutive ids.
CLIPS = [(f"N{i}", w) for i, w in enumerate(NUMBERS)]
CLIPS += [(f"T{(i + 3) * 10}", w) for i, w in enumerate(TENS)]
CLIPS += [(f"DAY{i}", w) for i, w in enumerate(DAYS)]
CLIPS += [(f"MONTH{i}", w) for i, w in enumerate(MONTHS)]
CLIPS += [
    ("UNA", "una"), ("SON_LAS", "Son las"), ("ES_LA", "Es la"), ("Y", "i"), ("EN_PUNTO", "en punto"),  # the conjunction "y" sounds like the letter i; a lone "y" is read as "i griega"
    ("Y_MEDIA", "y media"), ("Y_CUARTO", "y cuarto"), ("DE_LA_MANANA", "de la mañana"), ("DE_LA_TARDE", "de la tarde"),
    ("DE_LA_NOCHE", "de la noche"), ("DE_LA_MADRUGADA", "de la madrugada"), ("DEL_MEDIODIA", "del mediodía"),
    ("DE_LA_MEDIANOCHE", "de la medianoche"), ("HOY_ES", "Hoy es"), ("DE", "de"),
    ("LISTO_SUBI", "Listo, subí el volumen."), ("LISTO_BAJE", "Listo, bajé el volumen."),
    ("VOLUMEN_AL", "Listo, volumen al"), ("POR_CIENTO", "por ciento."),
    ("LISTO_APAGUE_MUSICA", "Listo, apagué la música."),
    ("CRONO_MARCHA", "Cronómetro en marcha."), ("CRONO_DETENIDO", "Cronómetro detenido."),
    ("CRONO_CERO", "Listo, cronómetro en cero."),
    ("JUGUEMOS_TRIQUI", "Juguemos al triqui. Tú eres la equis, toca una casilla."),
    ("LISTO", "Listo."), ("BATERIA_AL", "La batería está al"), ("BATERIA_CARGANDO", "y se está cargando."),
    ("NO_ENTENDI", "No te entendí."), ("UN_MOMENTO", "Un momento."), ("SIN_CONEXION", "No tengo conexión en este momento."),
    ("LISTO_TEMPORIZADOR_DE", "Listo, temporizador de"), ("MINUTO", "minuto"), ("MINUTOS", "minutos"),
    ("SEGUNDO", "segundo"), ("SEGUNDOS", "segundos"), ("HORA", "hora"), ("HORAS", "horas"),
    ("LISTO_DESPIERTO", "Listo, te despierto"), ("LISTO_RECORDARE", "Listo, te recordaré"), ("MANANA", "mañana"),
    ("A_LAS", "a las"), ("A_LA", "a la"), ("EN", "en"), ("TODOS_LOS_DIAS", "todos los días"),
    ("CANCELE_TEMP", "Listo, cancelé el temporizador."), ("CANCELE_ALARMA", "Listo, cancelé la alarma."),
    ("CANCELE_RECORD", "Listo, cancelé el recordatorio."), ("CANCELE_TODO", "Listo, cancelé lo que tenías programado."),
    ("NADA_CANCELAR", "No tienes nada programado para cancelar."), ("NADA_PROGRAMADO", "No tienes temporizadores ni alarmas."),
    ("UNA_COSA", "Tienes una cosa programada."), ("TIENES", "Tienes"), ("COSAS", "cosas programadas."),
    ("PARA_CUANDO", "¿Para cuándo? Dime por ejemplo: pon un temporizador de diez minutos."),
]
CLIPS += [(f"EVERY_DAY{i}", f"todos los {w}") for i, w in enumerate(DAYS)]
CLIPS += [
    ("AFUERA_HAY", "Afuera hay"), ("GRADOS", "grados."), ("COND_DESPEJADO", "Está despejado."),
    ("COND_MAYORMENTE", "Está mayormente despejado."), ("COND_PARCIAL", "Está parcialmente nublado."),
    ("COND_NUBLADO", "Está nublado."), ("COND_NIEBLA", "Hay niebla."), ("COND_LLOVIZNA", "Hay llovizna."),
    ("COND_LLUVIA", "Hay lluvia."), ("COND_AGUACEROS", "Hay aguaceros."), ("COND_NIEVE", "Hay nieve."),
    ("COND_TORMENTA", "Hay tormenta eléctrica."), ("HOY_MAXIMA", "Hoy la máxima será de"),
    ("Y_MINIMA", "y la mínima de"), ("CON", "con"), ("PROB_LLUVIA", "por ciento de probabilidad de lluvia."),
    ("PROXIMO_FESTIVO", "El próximo festivo es"), ("EL", "el"), ("DENTRO_DE", "dentro de"), ("DIAS", "días."),
    ("HOY", "hoy."), ("MANANA_PUNTO", "mañana."),
]
CLIPS += [(f"HOLIDAY{i}", w) for i, w in enumerate(['Año Nuevo', 'Día de los Reyes Magos', 'Día de San José', 'Jueves Santo', 'Viernes Santo', 'Día del Trabajo', 'Ascensión del Señor', 'Corpus Christi', 'Sagrado Corazón', 'San Pedro y San Pablo', 'Día de la Independencia', 'Batalla de Boyacá', 'Asunción de la Virgen', 'Día de la Raza', 'Todos los Santos', 'Independencia de Cartagena', 'Inmaculada Concepción', 'Navidad'])]
CLIPS += [
    ("LISTO_AGREGUE", "Listo, lo agregué a tu lista de compras."), ("YA_ESTABA", "Eso ya estaba en tu lista de compras."),
    ("QUE_AGREGO", "¿Qué quieres agregar? Por ejemplo: agrega leche a la lista de compras."),
    ("LISTO_ANOTE", "Listo, lo anoté."), ("QUE_ANOTO", "¿Qué anoto? Por ejemplo: anota que debo llamar a Ana."),
    ("LISTA_VACIA", "Tu lista de compras está vacía."), ("NO_NOTAS", "No tienes notas."),
    ("COSAS_EN_LISTA", "cosas en la lista de compras."), ("UNA_COSA_LISTA", "Tienes una cosa en la lista de compras."),
    ("NOTAS_N", "notas."), ("UNA_NOTA", "Tienes una nota."),
    ("BORRE_LISTA", "Listo, borré la lista de compras."), ("BORRE_NOTAS", "Listo, borré las notas."),
    ("QUITE_ESO", "Listo, quité eso de la lista."), ("NO_ENCONTRE_LISTA", "No encontré eso en tu lista."),
    ("QUE_QUITO", "¿Qué quito? Por ejemplo: quita la leche de la lista."),
]

CLIPS += [("CIENTO", "ciento")] + [(f"H{(i + 2) * 100}", w) for i, w in enumerate(
    ["doscientos", "trescientos", "cuatrocientos", "quinientos", "seiscientos", "setecientos", "ochocientos", "novecientos"])]
CLIPS += [
    ("MIL", "mil"), ("UN", "un"), ("UN_MILLON", "un millón"), ("MILLONES", "millones"), ("MENOS", "menos"), ("COMA", "coma"),
    ("EL_RESULTADO_ES", "El resultado es"), ("SON_RES", "Son"), ("NO_DIVIDIR", "No se puede dividir entre cero."),
    ("EL_DOLAR_ESTA", "El dólar está en"), ("EL_EURO_ESTA", "El euro está en"), ("EL_BITCOIN_ESTA", "El bitcoin está en"),
    ("EL_ETHEREUM_ESTA", "El ethereum está en"), ("PESOS", "pesos."), ("DOLARES", "dólares."),
    ("U_CELSIUS", "grados celsius."), ("U_FAHRENHEIT", "grados fahrenheit."), ("U_KM", "kilómetros."), ("U_MILLAS", "millas."),
    ("U_KILOS", "kilos."), ("U_LIBRAS", "libras."), ("U_METROS", "metros."), ("U_PIES", "pies."), ("U_LITROS", "litros."),
    ("HOY_TIENES", "Hoy tienes"), ("HOY_NO_EVENTOS", "Hoy no tienes eventos."), ("UN_EVENTO", "un evento."),
    ("EVENTOS_N", "eventos."), ("PRIMERO_ES", "El primero es"), ("MANANA_TIENES", "Mañana tienes"), ("MAS_PUNTO", "más."),
    ("TODO_EL_DIA", "todo el día."),
    ("NO_CORREOS_NUEVOS", "No tienes correos sin leer."), ("UN_CORREO_NUEVO", "Tienes un correo sin leer."),
    ("CORREOS_NUEVOS", "correos sin leer."),
    ("NO_ENCONTRE_WIKI", "No encontré eso en Wikipedia."),
    ("SE_DICE", "Se dice"), ("FAV_GUARDADA", "Listo, guardé la emisora en tus favoritas."), ("FAV_YA", "Esa emisora ya está en tus favoritas."),
    ("FAV_NINGUNA", "Todavía no tienes emisoras favoritas."), ("FAV_QUITADA", "Listo, quité la emisora de tus favoritas."),
    ("NADA_QUE_GUARDAR", "No hay ninguna emisora para guardar."), ("NO_LEYENDO", "No estás leyendo ningún libro."),
    ("JUGUEMOS", "Juguemos."),
    ("CAMBIE_INTERFAZ", "Listo, cambié la interfaz."), ("CAMBIE_COLORES", "Listo, cambié los colores."),
    ("TE_ESCUCHO", "Te escucho."), ("PONGO_RADIO", "Listo, pongo la emisora."), ("PONGO_TARJETA", "Listo, pongo tu música."),
    ("NO_EMISORA", "No encontré esa emisora."),
    ("U_GALONES", "galones."), ("U_CM", "centímetros."), ("U_PULGADAS", "pulgadas."),
]


def plain(text):
    text = unicodedata.normalize("NFKD", text)
    return "".join(c for c in text if not unicodedata.combining(c))


async def synth(text):
    for _ in range(4):
        try:
            audio = bytearray()

            async def run():
                async for chunk in edge_tts.Communicate(text, VOICE).stream():
                    if chunk["type"] == "audio":
                        audio.extend(chunk["data"])

            await asyncio.wait_for(run(), 15)
            if audio:
                decoded = miniaudio.decode(bytes(audio), output_format=miniaudio.SampleFormat.SIGNED16,
                                           nchannels=1, sample_rate=RATE)
                return np.frombuffer(decoded.samples.tobytes(), dtype=np.int16)
        except Exception:
            await asyncio.sleep(1)
    raise RuntimeError(f"could not make {text!r}")


def trim(samples):
    """Cuts the silence at both ends and fades the edges, so words join without clicks or long gaps."""
    x = samples.astype(np.float32)
    envelope = np.abs(x)
    limit = envelope.max() * 0.02
    voiced = np.nonzero(envelope > limit)[0]
    start, end = max(0, voiced[0] - 160), min(len(x), voiced[-1] + 240)
    x = x[start:end]
    fade = min(120, len(x) // 4)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    x *= 22000.0 / max(np.abs(x).max(), 1.0)  # the same loudness for every clip
    return x.astype(np.int16)


async def main():
    semaphore = asyncio.Semaphore(4)

    async def one(name, text):
        async with semaphore:
            return trim(await synth(text))

    audio = await asyncio.gather(*(one(n, t) for n, t in CLIPS))
    count = len(CLIPS)
    table_size = 8 + count * 8
    offset = table_size
    table = struct.pack("<4sI", b"BUGV", count)
    blob = bytearray()
    for samples in audio:
        table += struct.pack("<II", offset + len(blob), len(samples) * 2)
        blob += samples.tobytes()
    (ROOT / "voice_clips.bin").write_bytes(table + bytes(blob))
    print(f"{count} clips, {len(blob) / 1024:.0f} KB of audio")

    lines = ["// Generated by server/clips/make_clips.py. Do not edit.", "#pragma once", "typedef enum {"]
    for i, (name, _) in enumerate(CLIPS):
        lines.append(f"    CLIP_{name} = {i},")
    lines += [f"    CLIP_COUNT = {count}", "} clip_id_t;"]
    (ROOT / "main" / "voice_clips.h").write_text("\n".join(lines) + "\n", encoding="utf-8")


asyncio.run(main())
