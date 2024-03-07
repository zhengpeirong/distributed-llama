
import paramiko
import socket


def ssh_worker_execmd(worker_ip, port, username, password, command):
    s = paramiko.SSHClient()
    s.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        s.connect(hostname=worker_ip, port=port,
                  username=username, password=password, timeout=10)
        stdin, stdout, stderr = s.exec_command(command)
        # print(stdout.read().decode("utf-8").strip())
        return stdout.read()

    except paramiko.AuthenticationException:
        print("Authentication failed.")
        return None

    except paramiko.SSHException as e:
        print("Unable to establish SSH connection:", e)
        return None

    except socket.timeout:
        print("Connection timed out.")
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
    ips = ["192.168.6.1", "192.168.6.2", "192.168.6.4",
           "192.168.6.5", "192.168.6.7", "192.168.6.8", "192.168.6.10"]
    for ip in ips:
        out = ssh_worker_execmd(ip, 22, "root", "123", "ps aux|grep distr")
        print(ip)
        print(out.decode("utf-8").strip())
