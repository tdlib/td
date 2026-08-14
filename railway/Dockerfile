FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake gperf git ca-certificates \
    libssl-dev zlib1g-dev php-cli \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN php SplitSource.php \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTD_ENABLE_LTO=OFF \
 && cmake --build build --target tdjson -j2

FROM python:3.12-slim-bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 zlib1g ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/libtdjson.so /usr/local/lib/libtdjson.so
RUN ldconfig

WORKDIR /app
COPY railway/requirements.txt ./requirements.txt
RUN pip install --no-cache-dir -r requirements.txt
COPY railway/worker.py ./worker.py

ENV PYTHONUNBUFFERED=1
ENV TDLIB_DATA_DIR=/data/tdlib

CMD ["python", "/app/worker.py"]
