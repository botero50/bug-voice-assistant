"""Trivia by voice: Bug asks a question with three options and you answer "la A", "la dos" or the
answer itself. The question is shown on the Article screen; the score is kept until the server restarts.

  "juguemos trivia" / "hazme una pregunta"  ->  question      "la B" / "Júpiter"  ->  right or wrong
  "otra pregunta"                           ->  next question  "dime el puntaje"    ->  score
"""
import difflib
import random
import re

import extras
from text_utils import normalize

# (question, [option A, B, C], index of the right option)
QUESTIONS = [
    ("¿Cuál es la capital de Colombia?", ["Bogotá", "Medellín", "Cali"], 0),
    ("¿Cuál es el río más largo de Colombia?", ["El Cauca", "El Magdalena", "El Atrato"], 1),
    ("¿Quién escribió Cien años de soledad?", ["Mario Vargas Llosa", "Julio Cortázar", "Gabriel García Márquez"], 2),
    ("¿Cuántos planetas tiene el sistema solar?", ["Siete", "Ocho", "Nueve"], 1),
    ("¿Cuál es el planeta más grande del sistema solar?", ["Saturno", "Júpiter", "Neptuno"], 1),
    ("¿En qué año llegó el hombre a la Luna?", ["1959", "1969", "1979"], 1),
    ("¿Cuál es el océano más grande del mundo?", ["Atlántico", "Índico", "Pacífico"], 2),
    ("¿Cuántos colores tiene el arcoíris?", ["Seis", "Siete", "Ocho"], 1),
    ("¿Cuál es el animal terrestre más rápido?", ["El león", "El guepardo", "El caballo"], 1),
    ("¿Cuál es el símbolo químico del oro?", ["Au", "Ag", "Or"], 0),
    ("¿Cuántos jugadores de un equipo hay en la cancha en el fútbol?", ["Nueve", "Once", "Doce"], 1),
    ("¿Qué país ganó el Mundial de fútbol de 2010?", ["Brasil", "España", "Alemania"], 1),
    ("¿Cuál es el pico más alto de Colombia?", ["Nevado del Huila", "Pico Cristóbal Colón", "Nevado del Tolima"], 1),
    ("¿Cuál es la moneda de Colombia?", ["El bolívar", "El sol", "El peso"], 2),
    ("¿Quién pintó la Mona Lisa?", ["Picasso", "Leonardo da Vinci", "Van Gogh"], 1),
    ("¿Cuál es el gas más abundante en el aire?", ["Oxígeno", "Nitrógeno", "Hidrógeno"], 1),
    ("¿Cuántos días tiene un año bisiesto?", ["365", "366", "367"], 1),
    ("¿Cuál es el país más grande del mundo?", ["Canadá", "China", "Rusia"], 2),
    ("¿En qué ciudad queda la Torre Eiffel?", ["Roma", "París", "Londres"], 1),
    ("¿Quién libertó a Colombia, Venezuela y Ecuador?", ["Simón Bolívar", "José de San Martín", "Francisco de Paula Santander"], 0),
    ("¿Cuál es el hueso más largo del cuerpo humano?", ["El húmero", "El fémur", "La tibia"], 1),
    ("¿Cuántos lados tiene un hexágono?", ["Cinco", "Seis", "Ocho"], 1),
    ("¿Cuál es el desierto cálido más grande del mundo?", ["El Sahara", "El Gobi", "El Atacama"], 0),
    ("¿Cuál es el mamífero más grande del mundo?", ["El elefante", "La ballena azul", "La jirafa"], 1),
    ("¿En qué departamento queda Cartagena?", ["Atlántico", "Magdalena", "Bolívar"], 2),
    ("¿Cuál es la capital de Argentina?", ["Santiago", "Buenos Aires", "Lima"], 1),
    ("¿Cuánto es 9 por 7?", ["56", "63", "72"], 1),
    ("¿Qué planeta se conoce como el planeta rojo?", ["Marte", "Venus", "Mercurio"], 0),
    ("¿Quién llegó a América en 1492?", ["Américo Vespucio", "Cristóbal Colón", "Fernando de Magallanes"], 1),
    ("¿Cuántas patas tiene una araña?", ["Seis", "Ocho", "Diez"], 1),
]

LETTERS = "ABC"
ORDINAL_WORDS = {"a": 0, "ah": 0, "uno": 0, "1": 0, "primera": 0, "primero": 0,
                 "b": 1, "be": 1, "dos": 1, "2": 1, "segunda": 1, "segundo": 1,
                 "c": 2, "ce": 2, "se": 2, "tres": 2, "3": 2, "tercera": 2, "tercero": 2}

state = {"current": None, "score": 0, "asked": 0, "unused": []}


def _next_question():
    if not state["unused"]:
        state["unused"] = list(range(len(QUESTIONS)))
        random.shuffle(state["unused"])
    state["current"] = state["unused"].pop()
    state["asked"] += 1
    return QUESTIONS[state["current"]]


def _show(title, text, footer):
    extras.ARTICLE_LAST.update(title=title, text=text, footer=footer)


def ask():
    question, options, _ = _next_question()
    text = question + "\n" + "\n".join(f"{LETTERS[i]}. {o}" for i, o in enumerate(options))
    _show(f"Trivia · pregunta {state['asked']}", text, f"Puntos: {state['score']}")
    spoken = question + " " + ", ".join(f"{LETTERS[i]}, {o}" for i, o in enumerate(options)) + "."
    return spoken, "article"


def skill_trivia_start(_text):
    state.update(score=0, asked=0)
    spoken, screen = ask()
    return "Juguemos trivia. " + spoken, screen


def skill_trivia_next(_text):
    return ask()


def skill_trivia_score(_text):
    if not state["asked"]:
        return "Todavía no hemos jugado. Dime: juguemos trivia.", None
    answered = state["asked"] - (1 if state["current"] is not None else 0)
    return f"Llevas {state['score']} {'punto' if state['score'] == 1 else 'puntos'} en {max(answered, 0)} preguntas.", None


def skill_trivia_answer(text):
    """An answer to the question in play; None (let other skills try) when nothing is being asked."""
    if state["current"] is None:
        return None
    question, options, right = QUESTIONS[state["current"]]
    words = text.split()
    chosen = None
    if len(words) <= 4:
        for word in words:
            if word in ORDINAL_WORDS:
                chosen = ORDINAL_WORDS[word]
        if chosen is None:  # the answer itself, even if it was heard a little differently
            scores = [difflib.SequenceMatcher(None, normalize(o), text).ratio() for o in options]
            contains = [i for i, o in enumerate(options) if normalize(o) in text]
            if contains:
                chosen = contains[0]
            elif max(scores) >= 0.7:
                chosen = scores.index(max(scores))
    if chosen is None:
        return None
    state["current"] = None
    if chosen == right:
        state["score"] += 1
        spoken = f"¡Correcto! Es {options[right]}. Llevas {state['score']}. Di otra pregunta para seguir."
        _show("¡Correcto!", f"{question}\n{options[right]}", f"Puntos: {state['score']}")
    else:
        spoken = f"No, era {options[right]}. Llevas {state['score']}. Di otra pregunta para seguir."
        _show("Casi", f"{question}\nLa respuesta era: {options[right]}", f"Puntos: {state['score']}")
    return spoken, "article"


SKILLS = [
    (re.compile(r"^(?:\w+ ){0,3}(?:a|ah|b|be|c|ce|se|uno|dos|tres|primera|segunda|tercera|primero|segundo|tercero)$|"
                r"^\w+(?: \w+){0,3}$"), skill_trivia_answer),
    (re.compile(r"\b(otra|siguiente|nueva)\b.*\bpregunta\b|\bpregunta (nueva|siguiente)\b"), skill_trivia_next),
    (re.compile(r"\b(puntaje|puntos|cuantos puntos|como voy)\b"), skill_trivia_score),
    (re.compile(r"\b(trivia|trivial|hazme una pregunta|preguntame|jugar a las preguntas)\b"), skill_trivia_start),
]
