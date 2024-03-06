
import paramiko


def ssh_worker_execmd(worker_ip): 

    s = paramiko.SSHClient() 
    s.set_missing_host_key_policy(paramiko.AutoAddPolicy()) 
    s.connect(hostname=worker_ip, port=22, username="root", password="123") 
    
    command = "sed -i '/.*alias setproxy=\"export http_proxy=http:\/\/192.168.10.26:27890 && export https_proxy=http:\/\/192.168.10.26:27890\"*/c\\alias setproxy=\"export http_proxy=http:\/\/202.38.78.205:27890 && export https_proxy=http:\/\/202.38.78.205:27890\"' /root/.bashrc"
    stdin, stdout, stderr = s.exec_command(command) 
    print(stdout.read().decode("utf-8").strip())

    s.close()


if __name__ == '__main__':
    ssh_worker_execmd("192.168.6.2")