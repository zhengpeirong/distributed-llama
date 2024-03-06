import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

csv_path = "/root/WorkSpace/distributed-llama/test/dllama_llama-2-7b_q40/1PC/20240306193547/1PC.csv"
save_path = "/root/WorkSpace/distributed-llama/test/dllama_llama-2-7b_q40/1PC/20240306193547/latency_bar.pdf"

data = pd.read_csv(csv_path, delimiter=',')
arr = data.to_numpy()
print(data)
# print(arr.shape)

inference_time = arr[:-1,2]
transfer_time = arr[:-1,3]
# print(inference_time)

loop = 10
x = np.arange(loop)

plt.bar(x, inference_time, label='Inference Time')
plt.bar(x, transfer_time, bottom=inference_time, label='Transfer Time')

plt.xlabel("Test", weight="bold", fontsize=15)
plt.ylabel("Latency (ms)", weight="bold", fontsize=15)
plt.legend(fontsize=15)
plt.savefig(save_path ,bbox_inches='tight')