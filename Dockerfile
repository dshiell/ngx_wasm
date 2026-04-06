FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV CARGO_HOME=/root/.cargo
ENV RUSTUP_HOME=/root/.rustup
ENV PATH=/root/.cargo/bin:${PATH}

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        clang-format \
        cpanminus \
        curl \
        file \
        git \
        libclang-rt-dev \
        libpcre2-dev \
        make \
        perl \
        xz-utils \
        zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /work

# Build from the repository root so both sibling trees are available:
#   docker build -f ngx_wasm/Dockerfile -t ngx-wasm-ci .
COPY nginx /work/nginx
COPY ngx_wasm /work/ngx_wasm

WORKDIR /work/ngx_wasm

# Mirror the GitHub Actions Linux job setup.
# Drop any host-copied third_party artifacts so Linux deps are fetched fresh.
RUN rm -rf third_party && mkdir -p third_party
RUN make deps

CMD ["/bin/bash"]
