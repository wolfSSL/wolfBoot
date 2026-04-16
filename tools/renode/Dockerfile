FROM antmicro/renode:1.15.3

RUN whoami
USER root
RUN sed -i \
    -e 's|https\?://archive\.ubuntu\.com/ubuntu/\?|http://mirror.arizona.edu/ubuntu/|g' \
    -e 's|https\?://security\.ubuntu\.com/ubuntu/\?|http://mirror.arizona.edu/ubuntu/|g' \
    -e 's|https\?://[a-z0-9.-]*\.archive\.ubuntu\.com/ubuntu/\?|http://mirror.arizona.edu/ubuntu/|g' \
    /etc/apt/sources.list /etc/apt/sources.list.d/*.list 2>/dev/null || true
RUN apt-get update
RUN apt-get install gcc-arm-none-eabi make -y

RUN mkdir -p /tmp/test_results/
