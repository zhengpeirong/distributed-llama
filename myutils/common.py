
import paramiko
import socket

# ssh连接其他nodes并打印distributed-llama的输出
def ssh_worker_execmd(worker_ip, port, username, password, command):
    s = paramiko.SSHClient()
    s.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        s.connect(hostname=worker_ip, port=port, username=username, password=password, timeout=10)
        stdin, stdout, stderr = s.exec_command(command)
        # print(stdout.read().decode("utf-8").strip())
        return stdout.read()

    except paramiko.AuthenticationException:
        print("SSH: Authentication failed.")
        return None

    except paramiko.SSHException as e:
        print("SSH: Unable to establish SSH connection:", e)
        return None

    except socket.timeout:
        print("SSH: Connection timed out.")
        return None

    except Exception as e:
        print("Error:", e)
        return None

    finally:
        s.close()


def save_log(stdout, save_path, test_id):
    if test_id != -1:
        output = stdout.decode("utf8").strip()
        with open(save_path, 'a') as file:
            file.write("Test {}\n".format(test_id))
            file.write(output)
            file.write("\n\n")


if __name__ == '__main__':
    ips = ["192.168.1.11", "192.168.1.12", "192.168.1.13"] #,"192.168.1.14"
    for ip in ips:
        out = ssh_worker_execmd(ip, 22, "pi", "raspberry", "ps aux|grep distr")
        print(ip)
        print(out.decode("utf-8").strip())
