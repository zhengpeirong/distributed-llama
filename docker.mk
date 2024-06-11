# docker.mk

# clean and stop
.PHONY: clean-unnamed-images
.PHONY: stop-alpine-dllama-worker
clean-unnamed-images:
	@docker images -f "dangling=true" -q | xargs -r docker rmi
stop-alpine-dllama-worker:
	@docker ps -q --filter "ancestor=alpine_dllama_worker" | xargs -r docker stop


docker-worker-build:
	@docker build -f Dockerfile -t alpine_dllama_worker \
		--target worker \
		--build-arg A_ALPINE_IMG_TAG=latest \
		.

docker-inference-build:
	@docker build -f Dockerfile -t alpine_dllama_inference \
		--target inference \
		--build-arg A_ALPINE_IMG_TAG=latest \
		.

docker_create_network:
	@docker network create -d bridge --subnet 10.0.0.0/24 dllama-net || true

WORKER_IP_BASE = 10.0.0.
INFERENCE_IP = 10.0.0.2
# 10.0.0.1 has been taken by the gateway.
# Begin from 10.0.0.3 (that's why $(worker_id) + 2)
# it should align wth the `run_worker` `--ip $(WORKER_IP_BASE)$(shell expr $(1) + 2)`

E_WORKER_PARAM="--port 9998 --nthreads 2"
docker-workers-run:
	@echo "NUM_WORKERS=$(NUM_WORKERS)"
	$(foreach worker_id,$(shell seq 1 $(NUM_WORKERS)),\
		$(MAKE) WORKER_ID=$(worker_id) WORKER_IP=$(WORKER_IP_BASE)$(shell expr $(worker_id) + 2) run_worker;)

run_worker:
	@docker run -itd --rm --privileged \
		--name alpine-dllama-worker-$(WORKER_ID) \
		--network dllama-net \
		--ip $(WORKER_IP) \
		-e E_WORKER_PARAM=$(E_WORKER_PARAM) \
		alpine_dllama_worker


# generate_worker_ips:
# 	$(eval WORKER_IPS := $(foreach worker_id, $(shell seq 1 $(NUM_WORKERS)), $(WORKER_IP_BASE)$(shell expr $(worker_id) + 2):9998))

# Generate worker IPs
generate_worker_ips = $(foreach worker_id, $(shell seq 1 $(NUM_WORKERS)), $(WORKER_IP_BASE)$(shell expr $(worker_id) + 2):9998 )
# note the blank in the prompt.
E_INFERENCE_PARAM="--prompt Fuck --steps 64 --nthreads 2"
# WORKER_IPS = "10.0.0.3:9998\ 10.0.0.4:9998\ 10.0.0.5:9998"
docker-inference-run:
	$(eval WORKER_IPS := $(call generate_worker_ips))
	@echo "WORKER_IPS=$(WORKER_IPS)"
	$(eval E_OTHER_PARAM = "--workers $(WORKER_IPS)")
	@docker run -it --rm --privileged \
		--network dllama-net \
		--name alpine-dllama-inference \
		--ip $(INFERENCE_IP) \
		-e E_INFERENCE_PARAM=$(E_INFERENCE_PARAM) \
		-e E_OTHER_PARAM=$(E_OTHER_PARAM) \
		alpine_dllama_inference

docker-inference-only-run:
	@docker run -it --rm --privileged \
		--name alpine-dllama-inference \
		-e E_INFERENCE_PARAM=$(E_INFERENCE_PARAM) \
		alpine_dllama_inference


docker-inference:
	@$(MAKE) docker-inference-build
	@$(MAKE) docker-inference-only-run

docker-inference-%-worker:
	$(eval NUM_WORKERS := $*)
	@$(MAKE) docker_create_network
	@$(MAKE) docker-worker-build
	@$(MAKE) docker-inference-build
	@$(MAKE) stop-alpine-dllama-worker
	@$(MAKE) docker-workers-run NUM_WORKERS=$(NUM_WORKERS)
	@$(MAKE) docker-inference-run NUM_WORKERS=$(NUM_WORKERS)
	@$(MAKE) stop-alpine-dllama-worker
