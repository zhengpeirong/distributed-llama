#!/usr/bin/python

"""
This example shows how to create a simple network and
how to create docker containers (based on existing images)
to it.
"""

from mininet.net import Containernet
from mininet.node import Controller, Docker, OVSSwitch
from mininet.cli import CLI
from mininet.log import setLogLevel, info
from mininet.link import TCLink, Link
import subprocess
import time
import os

def start_tcpdump(container):
    """
    Start tcpdump on the given container and redirect output to the mount directory.
    """
    container_name=f'{container.name}'
    log_file = f"/mnt/{container_name}.pcap"
    tcpdump_cmd = f"tcpdump -i {container_name}-eth0 -w {log_file} &"
    container.cmd(tcpdump_cmd)
    info(f"*** Started tcpdump on {container_name}, logging to {log_file}\n")


def build_and_start_containers_net(net, num_workers):
    """
    Builds and runs Docker containers within the Containernet network.
    """
    info('*** Building Docker images\n')
    subprocess.run(['make', 'docker-worker-build'], check=True)
    subprocess.run(['make', 'docker-inference-build'], check=True)

    log_dir = os.path.join(os.getcwd(), "tcpdump_logs/")
    CPU_PERIOD = 1000000
    info('*** Adding Docker containers to the network\n')
    # Create and add inference container
    inference_container = net.addDocker('inference', ip='10.0.0.2', dimage='alpine_dllama_inference', \
                                        privileged=True, volumes=[f'{log_dir}:/mnt:rw'], \
                                        cpuset_cpus=str(0), cpu_quota=CPU_PERIOD//2, cpu_period=CPU_PERIOD) # Set CPU quota and period

    # Create and add worker containers
    worker_containers = []
    for worker_id in range(1, num_workers + 1):
        worker_ip = f'10.0.0.{worker_id + 2}'
        worker_container = net.addDocker(f'worker_{worker_id}', ip=worker_ip, dimage='alpine_dllama_worker', \
                                        privileged=True, volumes=[f'{log_dir}:/mnt:rw'], \
                                        cpuset_cpus=str(worker_id), cpu_quota=CPU_PERIOD//2, cpu_period=CPU_PERIOD)  # Set CPU quota and period
        worker_containers.append(worker_container)

    info('*** Creating the Switch\n')
    switch = net.addSwitch('s1')
    info('*** Creating links\n')
    # net.addLink(inference_container, switch)
    # for worker_container in worker_containers:
    #     net.addLink(worker_container, switch)
    

    print("*** Creating links with bandwidth, delay, and queue size limits")
    bw1=1000 #Mbps
    bw2=1000 
    delay1='0ms'
    delay2='0ms'
    max_queue_size_1=40//(num_workers+1) # 84 is the number of TCP packets for the `GS205` Buffer Memory 1Mbit
    max_queue_size_2=84//(num_workers+1)
    queue_size2=100

    net.addLink(inference_container, switch, cls=TCLink,delay=delay1, bw=bw1, max_queue_size=max_queue_size_1)
    # net.addLink(inference_container, switch)
    # print(f"Link between {inference_container} and {switch} - Bandwidth: {bw1} Mbps, Delay: {delay1}, Queue Size: {max_queue_size_1}")
    for worker_container in worker_containers:
        # net.addLink(worker_container, switch)
        net.addLink(worker_container, switch, cls=TCLink,delay=delay2, bw=bw2, max_queue_size=max_queue_size_2)
    return inference_container, worker_containers

def run_workers_and_inference(inference_container, worker_containers):
    "Run the script for workers and the header(inference)"

    # Start tcpdump after the network is set up
    start_tcpdump(inference_container)
    for worker_container in worker_containers:
        start_tcpdump(worker_container)


    info("*** Running workers\n")

    # Start worker containers
    for worker_container in worker_containers:
        worker_cmd = (
            "/distributed-llama/dllama worker "
            "--port 9998 --nthreads 1 &"
        )
        worker_container.cmd(worker_cmd)

    info("*** Running Inference\n")
    worker_ips = " ".join([f"{container.IP()}:9998" for container in worker_containers])
    inference_cmd = (
        "/distributed-llama/dllama inference "
        "--model /distributed-llama/models/llama3_8b_instruct_q40/dllama_model_llama3_8b_instruct_q40.m "
        "--tokenizer /distributed-llama/models/llama3_8b_instruct_q40/dllama_tokenizer_llama3_8b_instruct_q40.t "
        # "--model /distributed-llama/models/tinyllama_1_1b_3t_q40/dllama_model_tinyllama_1_1b_3t_q40.m "
        # "--tokenizer /distributed-llama/models/tinyllama_1_1b_3t_q40/dllama_tokenizer_tinyllama_1_1b_3t_q40.t "
        f"--prompt Hi --steps 32 --nthreads 1  --buffer-float-type q80 --workers {worker_ips}"
    )
    inference_container.cmd(inference_cmd)

def topology(num_workers):
    "Create a network with some docker containers acting as hosts."

    net = Containernet(controller=Controller)

    info('*** Adding controller\n')
    net.addController('c0')

    inference_container, worker_containers = build_and_start_containers_net(net, num_workers)


    info('*** Starting network\n')
    net.start()

    while net.pingAll() != 0:
        time.sleep(1)
    info("*** Connectivity established\n")

    run_workers_and_inference(inference_container, worker_containers)

    info('*** Running CLI\n')
    CLI(net)
    
    info('*** Stopping network\n')
    net.stop()

if __name__ == '__main__':
    setLogLevel('debug')
    import sys
    if len(sys.argv) != 2:
        print("Usage: sudo python3 simulation.py <num_workers>")
        sys.exit(1)
    num_workers = int(sys.argv[1])
    topology(num_workers)

