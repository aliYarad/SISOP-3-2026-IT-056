#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.h"

int sock = -1;
int running = 1;

// Signal Handler
void handle_sigint(int sig) {
    (void)sig;

    if (sock != -1) {
        send(sock, "/exit", 5, 0);
        char buf[BUFFER_SIZE];
        memset(buf, 0, sizeof(buf));
        recv(sock, buf, sizeof(buf) - 1, 0);
        printf("%s", buf);
    }
    running = 0;
    close(sock);
    printf("[SYSTEM] Disconnecting from The Wired...\n");
    exit(EXIT_SUCCESS);
}

void *recv_thread(void *arg) {
    (void)arg;
    char buf[BUFFER_SIZE];

    while (running) {
        memset(buf, 0, sizeof(buf));
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            running = 0;
            break;
        }

	printf("\r%s", buf);
        fflush(stdout);

        if (strstr(buf, "Disconnecting")) {
            running = 0;
            break;
        }
    }
    return NULL;
}

void *send_thread(void *arg) {
    (void)arg;
    char input[BUFFER_SIZE];

    while (running) {
        memset(input, 0, sizeof(input));
	printf("> ");
	fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        input[strcspn(input, "\n")] = '\0';

        if (!running) break;

        if (strlen(input) == 0) continue;

        send(sock, input, strlen(input), 0);

        if (strcmp(input, "/exit") == 0) {
            running = 0;
            break;
        }
    }
    return NULL;
}

int main() {
signal(SIGINT, handle_sigint);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	perror("[ERROR] socket");
	exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, HOST, &serv_addr.sin_addr) <= 0) {
        perror("[ERROR] inet_pton"); exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[ERROR] Cannot connect to The Wired"); exit(EXIT_FAILURE);
    }

    printf("[SYSTEM] Connected to The Wired (%s:%d)\n", HOST, PORT);

    char buf[BUFFER_SIZE];
    char input[NAME_SIZE];

    while (1) {
        memset(buf, 0, sizeof(buf));
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            printf("[SYSTEM] Connection lost.\n");
            close(sock);
            exit(EXIT_FAILURE);
        }
        printf("%s", buf);
        fflush(stdout);

        if (strstr(buf, "Welcome to The Wired") ||
            strstr(buf, "THE KNIGHTS CONSOLE"))  break;

        if (strstr(buf, "Connection refused"))  {
	    close(sock);
	    exit(EXIT_FAILURE);
	}

        memset(input, 0, sizeof(input));
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        input[strcspn(input, "\n")] = '\0';
        send(sock, input, strlen(input), 0);
    }

    pthread_t tid_recv, tid_send;
    pthread_create(&tid_recv, NULL, recv_thread, NULL);
    pthread_create(&tid_send, NULL, send_thread, NULL);

    pthread_join(tid_recv, NULL);
    pthread_join(tid_send, NULL);

    close(sock);
    return 0;
}
