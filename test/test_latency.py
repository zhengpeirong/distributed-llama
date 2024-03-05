import os
import re
import argparse
import paramiko
import time
import shutil
import subprocess as sp
import pandas as pd
from multiprocessing.pool import ThreadPool

def execute(args,command):
    if len(args.works) != 0:
        pool = ThreadPool(len(args.works))

        for work_ip in args.works:
            pool.apply_async(sshworker_execmd, args=(work_ip,))
            # Prevent ssh execution from not finishing
            time.sleep(1)
        pool.close() 

    pipe = sp.Popen(command, stdout= sp.PIPE)
    out,_ = pipe.communicate()

    if len(args.works) != 0:
        pool.join()

    return out

def test(args):
    command = [
        "nice", "-n", "-20", "/root/WorkSpace/distributed-llama/main", "inference", 
        "--model", "/root/WorkSpace/distributed-llama/model/{}.bin".format(args.model), "--tokenizer", "/root/WorkSpace/distributed-llama/model/tokenizer.bin",
        "--weights-float-type", "q40", "--buffer-float-type", "q80", 
        "--prompt", "Hello world", "--steps", "16", "--nthreads", "4",
            ]
    
    if len(args.works) != 0:
        command.append("--workers")
        for work_ip in args.works:
            command.append("{}:9998".format(work_ip))

    for _ in range(args.warm_up):
        print("Warm Up")

        out = execute(args,command)
        print(out.decode("utf8").split("\n")[-4:-1])

    generation_time = []
    inference_time = []
    transfer_time = []
    test_name =[]
    pattern = r'\d+\.\d+'

    for i in range(args.loop):
        print("Test {}".format(i))

        out = execute(args,command)
        output = out.decode("utf8").split("\n")[-4:-1]
        print(output)

        generation_time.append(float(re.findall(pattern, output[0])[0]))
        inference_time.append(float(re.findall(pattern, output[1])[0]))
        transfer_time.append(float(re.findall(pattern, output[2])[0]))
        test_name.append("Test {}".format(i))

    generation_time.append(sum(generation_time) / len (generation_time))
    inference_time.append(sum(inference_time) / len (inference_time))
    transfer_time.append(sum(transfer_time) / len (transfer_time))
    test_name.append("Test Avg")

    data = pd.DataFrame({"Test":test_name, "Avg generation time(ms)":generation_time, "Avg inference time(ms)":inference_time, "Avg transfer time(ms)":transfer_time,})
    data.to_csv(os.path.join(args.save_folder,'{}.csv'.format(args.test_name)),index=False,sep=',')

def sshworker_execmd(server_ip): 
    # print(server_ip)
    # paramiko.util.log_to_file("paramiko_{}.log".format(server_ip))

    s = paramiko.SSHClient() 
    s.set_missing_host_key_policy(paramiko.AutoAddPolicy()) 
    s.connect(hostname=server_ip, port=22, username="root",password="123") 
    
    command = "nice -n -20 /root/distributed-llama/main worker --port 9998 --nthreads 4"
    stdin, stdout, stderr = s.exec_command(command) 

    # print(stdout.read().decode("utf-8").strip())
    output = stdout.read().decode("utf-8").strip()

    with open(os.path.join(args.save_folder,"worker_{}.log".format(server_ip)), 'a') as file:
        file.write(output)
        file.write("\n\n")

    s.close()

if __name__ == '__main__':
    # Nano1 192.168.6.1
    # Nano2 192.168.6.2
    # Nano5 192.168.6.5

    parser = argparse.ArgumentParser()
    # parser.add_argument('--test_name', type=str, default="1PC+3Nano",help="the name of test")
    # parser.add_argument('--works', type=list, default=["192.168.6.1","192.168.6.2","192.168.6.5"],help="the ip of workers")

    # parser.add_argument('--test_name', type=str, default="1PC+1Nano",help="the name of test")
    # parser.add_argument('--works', type=list, default=["192.168.6.1"],help="the ip of workers")

    parser.add_argument('--test_name', type=str, default="1PC",help="the name of test")
    parser.add_argument('--works', type=list, default=[],help="the ip of workers")

    parser.add_argument('--model', type=str, default="dllama_llama-2-7b_q40",help="the model")
    parser.add_argument('--warm_up', type=int, default=2)
    parser.add_argument('--loop', type=int, default=5)

    args = parser.parse_args()

    save_folder = os.path.join("/root/WorkSpace/distributed-llama/test", args.model, args.test_name)
    args.save_folder = save_folder
    if os.path.exists(save_folder):
        shutil.rmtree(save_folder)
    os.makedirs(save_folder)

    test(args)