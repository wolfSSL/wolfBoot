FROM antmicro/renode:1.15.3

RUN whoami
USER root
RUN apt-get update
RUN apt-get install gcc-arm-none-eabi make -y

RUN mkdir -p /tmp/test_results/
