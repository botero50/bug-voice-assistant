FROM python:3.11-slim

WORKDIR /app

RUN apt-get update && apt-get install -y curl ffmpeg && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app.py .
RUN mkdir -p /app/logs /app/models /app/config

EXPOSE 5003

CMD ["python", "app.py"]
