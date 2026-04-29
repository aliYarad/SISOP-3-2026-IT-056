#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include "protocol.h"

time_t start_time;

// Logging
void log_message(const char *actor, const char *msg) {
    FILE *f = fopen("history.log", "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s]\n",
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec,
        actor, msg);
    fclose(f);
}

// Client Registry
typedef struct {
    int  fd;
    char name[NAME_SIZE];
    int  registered;
    int  is_admin;
    int  awaiting_password;
} Client;

Client clients[MAX_CLIENTS + 1];
struct pollfd pfds[MAX_CLIENTS + 1];
int nfds = 0;
int server_fd = -1;

// Signal Handler
void handle_sigint(int sig) {
    (void)sig;
    printf("\n[THE WIRED] Shutting down...\n");
    log_message("System", "SERVER SHUTDOWN");

    for (int i = 1; i <= nfds; i++) {
        if (pfds[i].fd != -1) {
            send(pfds[i].fd, "[SYSTEM] Server shutting down. Goodbye.\n", 40, 0);
            close(pfds[i].fd);
        }
    }

    close(server_fd);
    exit(EXIT_SUCCESS);
}

// Helper
int name_exists(const char *name) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (clients[i].registered && strcmp(clients[i].name, name) == 0)
            return 1;
    }
    return 0;
}

int find_client(int fd) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == fd) return i;
    }
    return -1;
}

void add_to_poll(int fd) {
    nfds++;
    pfds[nfds].fd = fd;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;

    int slot = nfds;
    clients[slot].fd = fd;
    clients[slot].registered = 0;
    clients[slot].is_admin = 0;
    clients[slot].awaiting_password = 0;
    memset(clients[slot].name, 0, NAME_SIZE);
}

void remove_from_poll(int idx) {
    int fd = pfds[idx].fd;
    int slot = find_client(fd);

    if (slot != -1) {
        if (clients[slot].registered)
            printf("[THE WIRED] NAVI '%s' disconnected.\n", clients[slot].name);
        else
            printf("[The WIRED] Unregistered fd=%d removed.\n", fd);

        clients[slot].fd = 0;
        clients[slot].registered = 0;
        clients[slot].is_admin = 0;
	clients[slot].awaiting_password = 0;
        memset(clients[slot].name, 0, NAME_SIZE);
    }

    close(fd);

    for (int i = idx; i < nfds; i++) {
        pfds[i] = pfds[i + 1];
        clients[i] = clients[i + 1];
    }
    pfds[nfds].fd = -1;
    nfds--;
}

void send_to_all(int sender_fd, const char *msg) {
    for (int i = 1; i <= nfds; i++) {
        if (pfds[i].fd == -1 || pfds[i].fd == sender_fd) continue;
        int slot = find_client(pfds[i].fd);
        if (slot != -1 && clients[slot].registered && !clients[slot].is_admin)
            send(pfds[i].fd, msg, strlen(msg), 0);
    }
}

// Hitung NAVI aktif (admin tidak dihitung)
int count_active_navi() {
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].registered && !clients[i].is_admin)
            count++;
    }
    return count;
}

// RPC Handler untuk admin
void handle_rpc(int fd, const char *cmd) {
    char resp[BUFFER_SIZE];

    if (strcmp(cmd, "1") == 0) {
	log_message("Admin", "RPC_GET_USERS");
        snprintf(resp, sizeof(resp),
                  "[RPC] Active NAVI: %d\nCommand >> ", count_active_navi());
        send(fd, resp, strlen(resp), 0);

    } else if (strcmp(cmd, "2") == 0) {
	log_message("Admin", "RPC_GET_UPTIME");
        long elapsed = (long)(time(NULL) - start_time);
        long h = elapsed / 3600;
        long m = (elapsed % 3600) / 60;
        long s = elapsed % 60;
        snprintf(resp, sizeof(resp),
                 "[RPC] Server uptime: %ldh %ldm %lds\nCommand >> ", h, m, s);
        send(fd, resp, strlen(resp), 0);

    } else if (strcmp(cmd, "3") == 0) {
	log_message("Admin", "RPC_SHUTDOWN");
        log_message("System", "EMERGENCY SHUTDOWN INITIATED");
        send(fd, "[RPC] Initiating shutdown...\n", 29, 0);
        printf("[THE WIRED] Shutdown ordered by admin.\n");

        for (int i = 1; i <= nfds; i++) {
            if (pfds[i].fd != -1)
                send(pfds[i].fd, "[SYSTEM] Server shutting down. Goodbye.\n", 43, 0);
        }

        for (int i = 1; i <= nfds; i++) {
            if (pfds[i].fd != -1) close(pfds[i].fd);
        }
        close(server_fd);
        exit(EXIT_SUCCESS);

    } else {
        snprintf(resp, sizeof(resp),
            "[RPC] Unknown command. Available: 1, 2, 3, 4\nCommand >> ");
        send(fd, resp, strlen(resp), 0);
    }
}

// Handle aktivitas dari satu client
void handle_message(int idx) {
    int  fd = pfds[idx].fd;
    int  slot = find_client(fd);
    char buf[BUFFER_SIZE];

    memset(buf, 0, sizeof(buf));
    int n = recv(fd, buf, sizeof(buf) - 1, 0);

    if (n <= 0) {
        if (slot != -1 && clients[slot].registered) {
            char notif[BUFFER_SIZE + NAME_SIZE];
            snprintf(notif, sizeof(notif),
                     "[SYSTEM] NAVI '%s' has left The Wired.\n", clients[slot].name);
            send_to_all(fd, notif);
	    char logmsg[BUFFER_SIZE];
            snprintf(logmsg, sizeof(logmsg), "User '%s' disconnected", clients[slot].name);
            log_message("System", logmsg);
        }
        remove_from_poll(idx);
        return;
    }

    buf[strcspn(buf, "\r\n")] = '\0';

// Belum registrasi
    if (slot != -1 && !clients[slot].registered) {
       if (clients[slot].awaiting_password) {
            if (strcmp(buf, ADMIN_PASSWORD) == 0) {
                clients[slot].registered = 1;
                clients[slot].is_admin = 1;
                clients[slot].awaiting_password = 0;

                const char *menu =
                    "[SYSTEM] Authentication Successful. Granted Admin privileges.\n\n"
                    "=== THE KNIGHTS CONSOLE ===\n"
                    "1. Check Active Entites (Users)\n"
                    "2. Check Server Uptime\n"
                    "3. Execute Emergency Shutdown\n"
                    "4. Disconnect\n"
                    "Command >> ";
                send(fd, menu, strlen(menu), 0);

                printf("[THE WIRED] Admin connected. (fd=%d)\n", fd);
                log_message("System", "User 'The Knights' connected");
            } else {
                send(fd, "[SYSTEM] Wrong password. Connection refused.\n", 45, 0);
                log_message("System", "Admin authentication failed");
                remove_from_poll(idx);
            }
            return;
        }

	if (strlen(buf) == 0 || strspn(buf, " ") == strlen(buf)) {
           const char *errmsg = "[SYSTEM] Identity cannot be empty.\nEnter your name: ";
           send(fd, errmsg, strlen(errmsg), 0);
           return;
        }

	if (strcmp(buf, ADMIN_NAME) == 0) {
            clients[slot].awaiting_password = 1;
            strncpy(clients[slot].name, ADMIN_NAME, NAME_SIZE - 1);
            send(fd, "Enter Password: ", 16, 0);
            return;
        }

	if (name_exists(buf)) {
            char errmsg[BUFFER_SIZE + NAME_SIZE + 128];
            snprintf(errmsg, sizeof(errmsg),
                "[SYSTEM] The identity '%s' is already synchronized in The Wired.\nEnter your name: ", buf);
            send(fd, errmsg, strlen(errmsg), 0);
            return;
        }

        strncpy(clients[slot].name, buf, NAME_SIZE - 1);
        clients[slot].registered = 1;

        char welcome[BUFFER_SIZE + NAME_SIZE + 128];
        snprintf(welcome, sizeof(welcome),
            "--- Welcome to The Wired, %s ---\n",buf);
        send(fd, welcome, strlen(welcome), 0);

        printf("[THE WIRED] NAVI '%s' registered. (fd=%d)\n", buf, fd);

        char logmsg[BUFFER_SIZE + NAME_SIZE];
        snprintf(logmsg, sizeof(logmsg), "User '%s' connected", buf);
        log_message("System", logmsg);
        return;
    }

 // Admin: hanya proses RPC, tidak masuk broadcast
    if (slot != -1 && clients[slot].is_admin) {
        if (strcmp(buf, "4") == 0 || strcmp(buf, "/exit") == 0) {
            send(fd, "[SYSTEM] Disconnecting from The Wired...\n", 41, 0);
            printf("[THE WIRED] Admin disconnected.\n");
            log_message("System", "User 'The Knights' disconnected");
            remove_from_poll(idx);
            return;
        }

	if (strcmp(buf, "1") != 0 && strcmp(buf, "2") != 0 && strcmp(buf, "3") != 0) {
            char out[BUFFER_SIZE + 64];
            snprintf(out, sizeof(out), "[The Knights]: %s\n", buf);
            send_to_all(fd, out);
            send(fd, out, strlen(out), 0);
            char logmsg[BUFFER_SIZE + 64];
            snprintf(logmsg, sizeof(logmsg), "[The Knights]: %s", buf);
            log_message("Admin", logmsg);
            return;
        }
        handle_rpc(fd, buf);
        return;
    }

    if (slot == -1 || !clients[slot].registered) return;

    if (strcmp(buf, "/exit") == 0) {
        send(fd, "[SYSTEM] Disconnecting from The Wired...\n", 41, 0);

        char notif[BUFFER_SIZE + NAME_SIZE];
        snprintf(notif, sizeof(notif),
                 "[SYSTEM] NAVI '%s' has left The Wired.\n",
                 clients[slot].name);
        send_to_all(fd, notif);

        printf("[THE WIRED] NAVI '%s' exited cleanly.\n", clients[slot].name);

	char logmsg[BUFFER_SIZE];
        snprintf(logmsg, sizeof(logmsg), "User '%s' disconnected", clients[slot].name);
        log_message("System", logmsg);

        remove_from_poll(idx);
        return;
    }

    char out[BUFFER_SIZE + NAME_SIZE + 32];
    snprintf(out, sizeof(out), "[%s]: %s\n", clients[slot].name, buf);
    send(fd, out, strlen(out), 0);
    send_to_all(fd, out);
    printf("%s", out);

    char logmsg[BUFFER_SIZE + NAME_SIZE + 32];
    snprintf(logmsg, sizeof(logmsg), "[%s]: %s", clients[slot].name, buf);
    log_message("User", logmsg);
}

int main() {
    signal(SIGINT, handle_sigint);
    start_time = time(NULL);

    printf("[THE WIRED] Starting server on %s:%d\n", HOST, PORT);

    memset(clients, 0, sizeof(clients));
    memset(pfds, 0, sizeof(pfds));
    for (int i = 0; i <= MAX_CLIENTS; i++) pfds[i].fd = -1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
	perror("[ERROR] socket");
	exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr(HOST);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[ERROR] bind"); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0) {
        perror("[ERROR] listen"); exit(EXIT_FAILURE);
    }

    pfds[0].fd = server_fd;
    pfds[0].events = POLLIN;
    nfds = 0;

    printf("[THE WIRED] Listening for NAVI connections...\n");
    log_message("System", "SERVER ONLINE");

    while (1) {
      int ready = poll(pfds, nfds + 1, -1);
      if (ready < 0) { perror("[ERROR] poll"); break; }

      if (pfds[0].revents & POLLIN) {
          struct sockaddr_in client_addr;
          socklen_t client_len = sizeof(client_addr);
          int new_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

          if (new_fd < 0) {
              perror("[ERROR] accept");
          } else if (nfds >= MAX_CLIENTS) {
              send(new_fd, "[SYSTEM] Server full. Connection refused.\n", 41, 0);
              close(new_fd);
          } else {
              add_to_poll(new_fd);
              printf("[THE WIRED] New connection from %s:%d (fd=%d)\n",
                     inet_ntoa(client_addr.sin_addr),
                     ntohs(client_addr.sin_port), new_fd);
              send(new_fd, "Enter your name: ", 17, 0);
          }
          ready--;
      }

      for (int i = 1; i <= nfds && ready > 0; i++) {
          if (pfds[i].revents & POLLIN) {
              handle_message(i);
              if (pfds[i].fd == -1) i--;
              ready--;
          }
      }
  }

  close(server_fd);
  return 0;
}

