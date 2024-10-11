FROM ubuntu:22.04

WORKDIR /app

RUN apt-get update
RUN apt-get install -y \
    g++ \
    make \
    cmake \
    liburing-dev \
    python3 \
    python3-pip
RUN rm -rf /var/lib/apt/lists/*

COPY . .

RUN ln -s /usr/bin/python3 /usr/bin/python

CMD ["python", "./exp.py"]
