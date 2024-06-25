#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>


// Helper function to calculate checksum
unsigned short checksum(unsigned short *buf, int len) {
    unsigned int sum = 0;
    unsigned short result;

    for (; len > 1; len -= 2) {
        sum += *buf++;
    }
    if (len == 1) {
        sum += *(unsigned char *)buf;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

int main() {
    int sockfd;
    char buffer[1024];
    struct sockaddr_in dest_addr;

    // Open raw socket
    if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Enable IP_HDRINCL to tell the kernel that headers are included in the packet
    int one = 1;
    if (setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Fill the IP header
    struct iphdr *iph = (struct iphdr *)buffer;
    struct udphdr *udph = (struct udphdr *)(buffer + sizeof(struct iphdr));
    char *data = buffer + sizeof(struct iphdr) + sizeof(struct udphdr);
    const char *switchml = "SwitchML"; // Example SwitchML protocol data
    const char *payload = "Payload";   // Actual payload data

    // Copy SwitchML protocol and payload data
    strcpy(data, switchml);
    strcat(data, payload);

    // Fill IP header fields
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + strlen(switchml) + strlen(payload));
    iph->id = htonl(54321);
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_UDP;
    iph->check = 0;
    iph->saddr = inet_addr("127.0.0.1"); // Change to your source IP
    iph->daddr = inet_addr("127.0.0.1"); // Change to your destination IP

    // Calculate IP checksum
    iph->check = checksum((unsigned short *)iph, sizeof(struct iphdr));

    // Fill UDP header fields
    udph->source = htons(12345);
    udph->dest = htons(8083);
    udph->len = htons(sizeof(struct udphdr) + strlen(switchml) + strlen(payload));
    udph->check = 0;

    // Destination address structure
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = iph->daddr;

    // Send the packet
    if (sendto(sockfd, buffer, ntohs(iph->tot_len), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        perror("sendto");
        exit(EXIT_FAILURE);
    }

    close(sockfd);
    return 0;
}