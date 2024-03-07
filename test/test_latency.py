import os
import re
import argparse
import paramiko
import time
import subprocess as sp
import pandas as pd
from multiprocessing.pool import ThreadPool
from datetime import datetime


def save_log(stdout, log_name, test_id):
    output = stdout.decode("utf8").strip()
    with open(os.path.join(args.save_folder,log_name), 'a') as file:
        file.write("Test {}\n".format(test_id))
        file.write(output)
        file.write("\n\n")

def execute(args, command, test_id=-1):
    if len(args.works) != 0:
        pool = ThreadPool(len(args.works))

        for work_ip in args.works:
            pool.apply_async(ssh_worker_execmd, args=(args, work_ip, test_id))
            # Prevent ssh execution from not finishing
            time.sleep(5)
        pool.close() 

    pipe = sp.Popen(command, stdout= sp.PIPE)
    out,_ = pipe.communicate()

    if len(args.works) != 0:
        pool.join()

    return out

def test(args):
    command = ["nice", "-n", "-20", "./main", "inference", 
            "--model", "./model/{}.bin".format(args.model), "--tokenizer", "./model/tokenizer.bin",
            "--weights-float-type", "q40", "--buffer-float-type", "q80", 
            "--prompt", "Hello world", "--steps", "16", "--nthreads", "{}".format(args.thread)]
    
    if len(args.works) != 0:
        command.append("--workers")
        for work_ip in args.works:
            command.append("{}:9998".format(work_ip))

    for _ in range(args.warm_up):
        print("Warm Up")

        stdout = execute(args, command)
        print(stdout.decode("utf8").split("\n")[-4:-1])

    generation_time = []
    inference_time = []
    transfer_time = []
    test_name =[]
    pattern = r'\d+\.\d+'

    for i in range(args.loop):
        print("Test {}".format(i))

        stdout = execute(args, command, i)

        # save master log
        save_log(stdout, "master.log", i)

        res = stdout.decode("utf8").split("\n")[-4:-1]
        print(res)

        generation_time.append(float(re.findall(pattern, res[0])[0]))
        inference_time.append(float(re.findall(pattern, res[1])[0]))
        transfer_time.append(float(re.findall(pattern, res[2])[0]))
        test_name.append("Test {}".format(i))

    generation_time.append(sum(generation_time) / len (generation_time))
    inference_time.append(sum(inference_time) / len (inference_time))
    transfer_time.append(sum(transfer_time) / len (transfer_time))
    test_name.append("Test Avg")

    data = pd.DataFrame({"Test":test_name, "Avg generation time(ms)":generation_time, "Avg inference time(ms)":inference_time, "Avg transfer time(ms)":transfer_time,})
    data.to_csv(os.path.join(args.save_folder,'latency.csv'),index=False,sep=',')

def ssh_worker_execmd(args, worker_ip, test_id): 

    s = paramiko.SSHClient() 
    s.set_missing_host_key_policy(paramiko.AutoAddPolicy()) 
    s.connect(hostname=worker_ip, port=22, username="root", password="123") 
    
    command = "nice -n -20 /root/distributed-llama/main worker --port 9998 --nthreads {}".format(args.thread)
    stdin, stdout, stderr = s.exec_command(command) 
    # print(stdout.read().decode("utf-8").strip())

    if test_id != -1:
        save_log(stdout.read(), "worker_{}.log".format(worker_ip), test_id)

    s.close()

if __name__ == '__main__':
    # Nano1 192.168.6.1
    # Nano2 192.168.6.2
    # Nano4 192.168.6.4
    # Nano5 192.168.6.5
    # Nano7 192.168.6.7
    # Nano8 192.168.6.8
    # Nano10 192.168.6.10

    parser = argparse.ArgumentParser()

    parser.add_argument('--test_name', type=str, default="1PC+7Nano", help="the name of test")
    parser.add_argument('--works', type=list, default=["192.168.6.1","192.168.6.2","192.168.6.4","192.168.6.5", "192.168.6.7", "192.168.6.8","192.168.6.10"], help="the ip of workers")

    # parser.add_argument('--test_name', type=str, default="1PC+3Nano", help="the name of test")
    # parser.add_argument('--works', type=list, default=["192.168.6.1","192.168.6.7","192.168.6.8"], help="the ip of workers")

    # parser.add_argument('--test_name', type=str, default="1PC+1Nano", help="the name of test")
    # parser.add_argument('--works', type=list, default=["192.168.6.1"], help="the ip of workers")

    # parser.add_argument('--test_name', type=str, default="1PC_8threads", help="the name of test")
    # parser.add_argument('--works', type=list, default=[], help="the ip of workers")

    parser.add_argument('--thread', type=int, default=4, help="num of threads")
    parser.add_argument('--model', type=str, default="dllama_llama-2-7b_q40",help="the model")
    parser.add_argument('--warm_up', type=int, default=2)
    parser.add_argument('--loop', type=int, default=10)

    args = parser.parse_args()

    currentDateAndTime = datetime.now()
    save_folder = os.path.join("./test", args.model, args.test_name,currentDateAndTime.strftime("%Y%m%d%H%M%S"))
    os.makedirs(save_folder)
    args.save_folder = save_folder


    test(args)