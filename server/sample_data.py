"""Sample data shown until the real mailboxes and calendars are connected.

Everything here is made up. The screens say "Datos de ejemplo" while they show it.
"""

EMAILS = [
    {
        "from": "Ana Gómez",
        "subject": "Revisión del diseño del sensor",
        "time": "2:30 pm",
        "unread": True,
        "body": "Hola, te dejo el diseño actualizado del sensor de presión. Cambié el espesor de la "
                "membrana a 0.3 mm. ¿Puedes revisarlo antes de la reunión de las 4?",
    },
    {
        "from": "Carlos Ruiz",
        "subject": "Cotización de repuestos",
        "time": "1:10 pm",
        "unread": True,
        "body": "Ya llegó la cotización de los repuestos. El total es de 2.450.000 pesos y la entrega "
                "sería el jueves. Dime si la aprobamos.",
    },
    {
        "from": "Soporte técnico",
        "subject": "Tu ticket 4821 fue resuelto",
        "time": "11:45 am",
        "unread": True,
        "body": "Resolvimos el problema con el acceso a la VPN. Si vuelve a fallar, responde a este "
                "correo y lo reabrimos.",
    },
    {
        "from": "Laura Mejía",
        "subject": "Acta de la reunión de ayer",
        "time": "ayer",
        "unread": False,
        "body": "Adjunto el acta de la reunión de ayer. Los puntos pendientes están marcados en amarillo.",
    },
]

# day_offset 0 is today, 1 is tomorrow
EVENTS = [
    {"day_offset": 0, "hour": 16, "minute": 0, "title": "Reunión de equipo", "place": "Teams"},
    {"day_offset": 0, "hour": 18, "minute": 30, "title": "Cita con el dentista", "place": "Centro"},
    {"day_offset": 1, "hour": 9, "minute": 0, "title": "Reunión general", "place": "Auditorio"},
    {"day_offset": 1, "hour": 14, "minute": 0, "title": "Uno a uno con el jefe", "place": "Oficina"},
]
