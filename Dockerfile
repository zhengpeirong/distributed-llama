# https://vsupalov.com/docker-arg-env-variable-guide/
# docker build arg to all FROM

ARG A_ALPINE_IMG_TAG=latest
FROM alpine:${A_ALPINE_IMG_TAG} as base

# docker img meta
LABEL dllama.image.authors="weege007@gmail.com, peirong.zheng@polyu.edu.hk"

# docker build arg after each FROM
ARG A_WORKER_PARAM="--port 9998 --nthreads 4"
ARG A_INFERENCE_PARAM="--prompt 'Hello' --steps 64 --nthreads 4"
ARG A_MODEL_PARAM="--model /distributed-llama/models/tinyllama_1_1b_3t_q40/dllama_model_tinyllama_1_1b_3t_q40.m --tokenizer /distributed-llama/models/tinyllama_1_1b_3t_q40/dllama_tokenizer_tinyllama_1_1b_3t_q40.t "
ARG A_GIT_REPO_URL=https://github.com/zhengpeirong/distributed-llama.git
ARG A_OTHER_PARAM=""
# container env
ENV E_WORKER_PARAM ${A_WORKER_PARAM}
ENV E_INFERENCE_PARAM ${A_INFERENCE_PARAM}
ENV E_MODEL_PARAM ${A_MODEL_PARAM}
ENV E_OTHER_PARAM ${A_OTHER_PARAM}
# build prepare layer, use arg/env
RUN set -eux; \
    \
    # Install necessary packages without cache to keep the image size small
    apk add --no-cache \
    git \
    g++ \
    make \
    python3 \
    py3-pip \
    py3-requests \
    ; \
    # Clone the specified Git repository
    git clone ${A_GIT_REPO_URL} distributed-llama; \
    cd distributed-llama; \
    git checkout -b dev/docker origin/dev/docker; \
    make dllama; \
    \
    echo "Compile Distributed Llama Done\n"

# Custom cache invalidation
ARG CACHEBUST=1

# Download model and run launch.py with automatic "no" input
FROM base as download_model
RUN cd distributed-llama; \
    yes "N" | python3 launch.py tinyllama_1_1b_3t_q40


# docker run container runtime, use env
FROM download_model as worker
RUN echo "E_WORKER_PARAM: ${E_WORKER_PARAM}"
RUN echo "E_OTHER_PARAM: ${E_OTHER_PARAM}"
CMD ["sh","-c","/distributed-llama/dllama worker ${E_WORKER_PARAM} ${E_OTHER_PARAM}"]

FROM download_model as inference
CMD ["sh", "-c", "echo ${E_INFERENCE_PARAM} ${E_MODEL_PARAM} ${E_OTHER_PARAM};/distributed-llama/dllama inference ${E_MODEL_PARAM} ${E_INFERENCE_PARAM} ${E_OTHER_PARAM}"]