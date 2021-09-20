FROM ubuntu:16.04
MAINTAINER Loic Dachary "loic@dachary.org"

# nova.clouds will redirect to the fastest mirror
RUN sed -i -e 's|http://archive.ubuntu|http://nova.clouds.archive.ubuntu|' /etc/apt/sources.list
RUN apt-get update
RUN apt-get install -y python-virtualenv git sudo
RUN echo 'ALL ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers
RUN apt-get install -y wget # for when ceph-qa-suite gets an new image
RUN cd /opt ; git clone --recursive http://ceph-workbench.dachary.org/root/ceph-workbench.git
# install packages
RUN cd /opt/ceph-workbench ; ./bootstrap
ADD entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
