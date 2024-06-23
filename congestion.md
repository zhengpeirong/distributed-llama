# communication time of congestion

## A big increase of communication time 

```py
    bw1=1000 #Mbps
    bw2=1000 
    delay1='0ms'
    delay2='0ms'
    max_queue_size_1=40//(num_workers+1)
    max_queue_size_2=40//(num_workers+1)
```
In this scenario, there is an incast problem.
Specifically, the TCP retransmission happens a lot when the worker sends data to the server.
To simulate more accurately, the `max_queue_size_1` should be tuned.

```py
    max_queue_size_1=40//(num_workers+1)
    max_queue_size_2=84//(num_workers+1) #84 is the number of TCP packets for the GS205 Buffer Memory 1Mbit
```
Still, the TCP retransmission happens a lot.

To view in `Wireshark`, paste `tcp.analysis.retransmission`.



## Result


| Model\Device      | 2 x Container                                                   | 4 x Container                                                       | 8 x Container                                                       |
| ---------- | --------------------------------------------------------------- | ------------------------------------------------------------------- | ------------------------------------------------------------------- |
| Llama3-8B-int4 | **394.03 ms**<br><sub><sup>I: 386.66 ms, T: 6.91 ms</sup></sub> | **353.66 ms** 🔥<br><sub><sup>I: 281.84 ms, T: 71.22 ms</sup></sub> | **2691.47 ms**<br><sub><sup>I: 214.19 ms, T: 2476.84 ms</sup></sub> |


### assign single CPU for each container

Avg generation time: 392.00 ms
Avg inference time:  385.06 ms
Avg transfer time:   6.38 ms


Avg generation time: 362.25 ms
Avg inference time:  272.44 ms
Avg transfer time:   89.38 ms

Avg generation time: 2343.47 ms
Avg inference time:  219.16 ms
Avg transfer time:   2123.22 ms

### assign 50% of a CPU for each container

Avg generation time: 836.28 ms
Avg inference time:  761.34 ms
Avg transfer time:   74.41 ms

Avg generation time: 687.69 ms
Avg inference time:  557.19 ms
Avg transfer time:   130.03 ms

Avg generation time: 3405.25 ms
Avg inference time:  370.59 ms
Avg transfer time:   3034.12 ms

## Conclusion

So, this happens only when the most overloaded link has a small `max_queue_size`.