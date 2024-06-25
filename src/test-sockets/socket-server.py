import socket
import threading
import struct
# TCP Server
def tcp_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind(('127.0.0.1', 8080))
    server_socket.listen(1)

    print("TCP Server is listening on port 8080...")

    while True:
        client_socket, client_address = server_socket.accept()
        print(f"TCP Connection from {client_address} has been established!")

        client_message = client_socket.recv(1024).decode('utf-8')
        print(f"Received from TCP client: {client_message}")

        response_message = "Hello, TCP Client"
        client_socket.send(response_message.encode('utf-8'))

        client_socket.close()

# UDP Server
def udp_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_socket.bind(('127.0.0.1', 8081))

    print("UDP Server is listening on port 8081...")

    while True:
        message, client_address = server_socket.recvfrom(1024)
        print(f"Received from UDP client: {message.decode('utf-8')}")

        response_message = "Hello, UDP Client"
        server_socket.sendto(response_message.encode('utf-8'), client_address)

def parse_packet(packet, bind_port):
    # Parse the IP header (first 20 bytes)
    ip_header = packet[:20]
    iph = struct.unpack('!BBHHHBBH4s4s', ip_header)

    version_ihl = iph[0]
    version = version_ihl >> 4
    ihl = version_ihl & 0xF

    iph_length = ihl * 4
    ttl, protocol, src_addr, dest_addr = iph[5], iph[6], iph[8], iph[9]


    # Filter packets to a specific port, e.g., 8082
    src_port, dest_port, length, checksum = struct.unpack('!HHHH', packet[iph_length:iph_length+8])
    if dest_port != bind_port:
        return
    
    src_addr = socket.inet_ntoa(src_addr)
    dest_addr = socket.inet_ntoa(dest_addr)

    print(f"Received packet from {src_addr}")

    print(f"IP Header: Version: {version}, Header Length: {ihl*4}, TTL: {ttl}, Protocol: {protocol}, Source Address: {src_addr}, Destination Address: {dest_addr}")

    # Parse the UDP header (immediately following the IP header)
    udp_header = packet[iph_length:iph_length+8]
    udph = struct.unpack('!HHHH', udp_header)

    src_port, dest_port, length, checksum = udph
    print(f"UDP Header: Source Port: {src_port}, Destination Port: {dest_port}, Length: {length}, Checksum: {checksum}")
    
    # Extract SwitchML and Payload
    switchml_payload = packet[iph_length+8:]
    print(f"SwitchML and Payload: {switchml_payload.decode('utf-8')}")

def start_raw_udp_server():
    # Create a raw socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_UDP)
    sock.bind(("127.0.0.1", 0))  # Bind to 
    bind_port = 8083
    print(f"Raw socket UDP server is listening on {bind_port}...")

    while True:
        packet, addr = sock.recvfrom(65535)  # Buffer size is 65535 bytes
        parse_packet(packet, bind_port=bind_port)

# Main function to start both servers
def main():
    tcp_thread = threading.Thread(target=tcp_server)
    udp_thread = threading.Thread(target=udp_server)
    raw_udp_thread = threading.Thread(target=start_raw_udp_server)

    tcp_thread.start()
    udp_thread.start()
    raw_udp_thread.start()

    tcp_thread.join()
    udp_thread.join()
    raw_udp_thread.join()

if __name__ == "__main__":
    main()