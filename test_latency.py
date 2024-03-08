import os
import re
import argparse
import time
import subprocess as sp
import pandas as pd

from multiprocessing.pool import ThreadPool
from datetime import datetime
from functools import partial
from myutils.common import save_log, ssh_worker_execmd


def execute(args, master_command, worker_command, test_id=-1):
    if len(args.works) != 0:
        pool = ThreadPool(len(args.works))

        for worker_ip in args.works:
            save_log_without_stdout = partial(save_log, save_path=os.path.join(
                args.save_folder, "worker_{}.log".format(worker_ip)), test_id=test_id)
            pool.apply_async(ssh_worker_execmd, args=(
                worker_ip, 22, "root", "123", worker_command), callback=save_log_without_stdout)
        pool.close()

        # Prevent ssh execution from not finishing
        time.sleep(5)

    pipe = sp.Popen(master_command, stdout=sp.PIPE)
    out, _ = pipe.communicate()
    save_log(stdout=out, save_path=os.path.join(
        args.save_folder, "master.log"), test_id=test_id)

    if len(args.works) != 0:
        pool.join()

    return out.decode("utf8")


def test(args):
    master_command = ["nice", "-n", "-20", "./main", "inference",
                      "--model", "./model/{}.bin".format(
                          args.model), "--tokenizer", "./model/tokenizer.bin",
                      "--weights-float-type", "q40", "--buffer-float-type", "q80",
                      "--prompt", "Hello world", "--steps", "{}".format(args.steps), "--nthreads", "{}".format(args.threads)]

    if len(args.works) != 0:
        master_command.append("--workers")
        for work_ip in args.works:
            master_command.append("{}:9998".format(work_ip))

    worker_command = "nice -n -20 /root/distributed-llama/main worker --port 9998 --nthreads {}".format(
        args.threads)

    print(" ".join(master_command))
    print(worker_command)

    for _ in range(args.warm_up):
        print("Warm Up")

        print(execute(args, master_command, worker_command))

    generation_time = []
    inference_time = []
    transfer_time = []
    test_name = []
    pattern = r'\d+\.\d+'

    for i in range(args.loop):
        print("Test {}".format(i))

        out = execute(args, master_command, worker_command, i)
        res = out.split("\n")[-4:-1]
        print(res)

        generation_time.append(float(re.findall(pattern, res[0])[0]))
        inference_time.append(float(re.findall(pattern, res[1])[0]))
        transfer_time.append(float(re.findall(pattern, res[2])[0]))
        test_name.append("Test {}".format(i))

    generation_time.append(sum(generation_time) / len(generation_time))
    inference_time.append(sum(inference_time) / len(inference_time))
    transfer_time.append(sum(transfer_time) / len(transfer_time))
    test_name.append("Test Avg")

    data = pd.DataFrame({"Test": test_name, "Avg generation time(ms)": generation_time,
                        "Avg inference time(ms)": inference_time, "Avg transfer time(ms)": transfer_time, })
    data.to_csv(os.path.join(args.save_folder, 'latency.csv'),
                index=False, sep=',')


if __name__ == '__main__':
    # Nano1 192.168.6.1
    # Nano2 192.168.6.2
    # Nano4 192.168.6.4
    # Nano5 192.168.6.5
    # Nano7 192.168.6.7
    # Nano8 192.168.6.8
    # Nano10 192.168.6.10

    parser = argparse.ArgumentParser()

    # parser.add_argument('--test_name', type=str, default="1VM+7Nano", help="the name of test")
    # parser.add_argument('--works', type=list, default=["192.168.6.1", "192.168.6.2", "192.168.6.4","192.168.6.5", "192.168.6.7", "192.168.6.8", "192.168.6.10"], help="the ip of workers")

    # parser.add_argument('--test_name', type=str, default="1VM+3Nano", help="the name of test")
    # parser.add_argument('--works', type=list, default=["192.168.6.5","192.168.6.2","192.168.6.4"], help="the ip of workers")

    parser.add_argument('--test_name', type=str, default="1VM+1Nano", help="the name of test")
    parser.add_argument('--works', type=list, default=["192.168.6.2"], help="the ip of workers")

    # parser.add_argument('--test_name', type=str, default="1VM_8threads", help="the name of test")
    # parser.add_argument('--works', type=list, default=[], help="the ip of workers")

    parser.add_argument('--threads', type=int, default=4, help="num of threads")
    parser.add_argument('--steps', type=int, default=16,help="num of generated tokens")
    parser.add_argument('--model', type=str, default="dllama_llama-2-7b_q40", help="the model")
    parser.add_argument('--warm_up', type=int, default=2)
    parser.add_argument('--loop', type=int, default=10)

    args = parser.parse_args()

    currentDateAndTime = datetime.now()
    save_folder = os.path.join("./test", args.model, args.test_name, currentDateAndTime.strftime("%Y%m%d%H%M%S"))
    os.makedirs(save_folder)
    args.save_folder = save_folder

    test(args)
