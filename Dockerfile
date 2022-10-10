FROM python:3.10

RUN mkdir /root/app
RUN mkdir /games
RUN apt-get update && apt-get install -y make g++ wget unzip

COPY . /root/app
RUN pip install -r /root/app/requirements.txt
RUN wget https://stockfishchess.org/files/stockfish_15_linux_x64.zip && \
    unzip stockfish_15_linux_x64.zip && \
    cd /stockfish_15_linux_x64/stockfish_15_src/src && \
    make net && \
    make build ARCH=x86-64-modern

RUN cd /root/app/uci-analyser && make
CMD python /root/app/app.py