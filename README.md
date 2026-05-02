# SISOP-3-2026-IT-056
## Laporan Resmi
**Praktikum Sistem Operasi 2026 Modul 3 - Thread, IPC, & RPC**  

---
**Nama : Aliya Rahmadina**  
**NRP : 5027251056**   

---
### _Soal 1_
#### _Deskripsi Soal_
Pada soal 1 diperintahkan untuk membangun sistem komunikasi jaringan bernama _The Wired_ yang terdiri dari sebuah server (`wired.c`) dan client (`navi.c`). Server menangani banyak klien sekaligus menggunakan `poll()`, sedangkan client berjalan dengan dua thread asinkron untuk mengirim dan menerima pesan. Terdapat admin _The Knights_ yang dapat mengakses konsol RPC untuk mengelola server. Semua aktivitas dicatat ke `history.log`.

#### _Penjelasan Kode_
_protocol.h_  
File header yang menyimpan semua konstanta yang digunakan bersama oleh server dan client.
```c
#define HOST           "127.0.0.1"
#define PORT           9000
#define ADMIN_NAME     "The Knights"
#define ADMIN_PASSWORD "000"
#define MAX_CLIENTS    100
#define BUFFER_SIZE    1024
#define NAME_SIZE      64
```
- `HOST` dan `PORT` menentukan alamat dan port server.
- `ADMIN_NAME` dan `ADMIN_PASSWORD` adalah kredensial akun admin.
- `MAX_CLIENTS` membatasi jumlah koneksi yang bisa ditangani.
- `BUFFER_SIZE` adalah ukuran buffer pesan.
- `NAME_SIZE` adalah panjang maksimal nama pengguna.

_wired.c_
Struct Client berguna untuk menyimpan data setiap koneksi. Array digunakan `pfds[]` untuk keperluan `poll()` dan `clients[]` untuk menyimpan data identitas klien.
```c
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
```
`log_message` mencatat setiap aktivitas ke dalam file `history.log` dalam mode append ("a"). Setiap baris log memiliki format `[YYYY-MM-DD HH:MM:SS] [actor] [msg]`, di mana actor bisa berisi System, Admin, atau User, dan msg berisi pesan atau perintah yang dijalankan.
```c
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
```
Signal handler yang dipanggil saat server menerima sinyal SIGINT (Ctrl+C). Server mengirimkan pesan perpisahan ke semua client yang masih terhubung, lalu menutup semua file descriptor dan keluar dengan bersih. Aktivitas shutdown dicatat ke log sebelum keluar.
```c
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
```
Fungsi helper mengecek apakah nama yang ingin didaftarkan sudah digunakan oleh client lain. Iterasi dilakukan pada semua slot yang sudah registered. Mengembalikan 1 jika nama sudah ada, 0 jika belum.
```c
int name_exists(const char *name) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (clients[i].registered && strcmp(clients[i].name, name) == 0)
            return 1;
    }
    return 0;
}
```
Mencari indeks slot di array `clients[]` berdasarkan file descriptor fd. Digunakan untuk menghubungkan data di `pfds[]` (yang hanya menyimpan fd) dengan data lengkap di `clients[]`. Mengembalikan indeks slot jika ditemukan, atau -1 jika tidak.
```c
int find_client(int fd) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == fd) return i;
    }
    return -1;
}
```
Mendaftarkan koneksi baru ke dalam array `pfds[]` agar dapat dipantau oleh `poll()`, sekaligus menginisialisasi slot data client yang baru dengan nilai default. POLLIN menandakan bahwa kita hanya memantau event data masuk dari client tersebut.
```c
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
```
Menghapus client dari daftar polling saat client disconnect. File descriptor ditutup, data client direset, lalu array `pfds[]` dan `clients[]` digeser ke kiri agar tidak ada celah kosong di tengah. `nfds` dikurangi satu setelah proses selesai.
```c
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
```
Mengirimkan pesan ke semua client yang terdaftar, kecuali pengirim itu sendiri dan akun admin. Ini memastikan broadcast hanya diterima oleh pengguna biasa yang sedang aktif di ruang obrolan.
```c
void send_to_all(int sender_fd, const char *msg) {
    for (int i = 1; i <= nfds; i++) {
        if (pfds[i].fd == -1 || pfds[i].fd == sender_fd) continue;
        int slot = find_client(pfds[i].fd);
        if (slot != -1 && clients[slot].registered && !clients[slot].is_admin)
            send(pfds[i].fd, msg, strlen(msg), 0);
    }
}
```
Menghitung jumlah pengguna biasa yang sedang aktif terhubung ke server, tidak termasuk akun admin. Hasilnya digunakan oleh RPC command 1.
```c
int count_active_navi() {
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].registered && !clients[i].is_admin)
            count++;
    }
    return count;
}
```
Command 1 : memanggil `count_active_navi()` dan mengirimkan jumlah NAVI aktif ke admin.
Command 2 : menghitung selisih waktu saat ini dengan `start_time` dan mengirimkan uptime server dalam format jam, menit, dan detik.
Command 3 : mencatat emergency shutdown ke log, mengirimkan notifikasi ke semua client, lalu menutup semua koneksi dan menghentikan server.

Setiap perintah yang berhasil dijalankan dicatat ke `history.log` dengan label Admin.
```c
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
        // ...tutup semua koneksi dan exit
    } else {
        snprintf(resp, sizeof(resp),
            "[RPC] Unknown command. Available: 1, 2, 3, 4\nCommand >> ");
        send(fd, resp, strlen(resp), 0);
    }
}
```
- Disconnect paksa, jika `recv()` mengembalikan nilai ≤ 0, artinya client terputus secara tidak bersih. Server broadcast notifikasi ke pengguna lain, mencatat ke log, lalu memanggil `remove_from_poll()`.
- Belum registrasi, client yang baru terhubung diminta memasukkan nama. Jika nama adalah `ADMIN_NAME`, server beralih ke mode menunggu password. Jika password benar, client naik status menjadi admin dan mendapatkan tampilan konsol RPC.
- Admin, pesan dari admin diperiksa apakah berupa angka perintah RPC (1/2/3) atau angka 4//exit untuk disconnect. Jika bukan keduanya, pesan dianggap sebagai chat biasa dan di-broadcast ke semua pengguna.
- User biasa, pesan diformat menjadi [nama]: pesan lalu dikirim balik ke pengirim, di-broadcast ke semua klien lain, dan dicatat ke log.
```c
void handle_message(int idx) {
    int  fd = pfds[idx].fd;
    int  slot = find_client(fd);
    char buf[BUFFER_SIZE];

    memset(buf, 0, sizeof(buf));
    int n = recv(fd, buf, sizeof(buf) - 1, 0);

    if (n <= 0) {
        // client disconnect paksa → broadcast notif + log
        remove_from_poll(idx);
        return;
    }

    buf[strcspn(buf, "\r\n")] = '\0';

    // Belum registrasi
    if (slot != -1 && !clients[slot].registered) {
        // ... proses pendaftaran nama / autentikasi admin
        return;
    }

    // Admin: proses RPC atau broadcast pesan
    if (slot != -1 && clients[slot].is_admin) {
        // ...
        return;
    }

    // User biasa: handle /exit atau broadcast pesan
    if (strcmp(buf, "/exit") == 0) {
        // ...kirim notif disconnect, log, remove
        return;
    }

    char out[BUFFER_SIZE + NAME_SIZE + 32];
    snprintf(out, sizeof(out), "[%s]: %s\n", clients[slot].name, buf);
    send(fd, out, strlen(out), 0);
    send_to_all(fd, out);
    log_message("User", out);
}
```
Fungsi `main()` inisialisasi socket TCP, binding ke `HOST:PORT`, dan mulai listening. Indeks 0 pada `pfds[]` selalu dialokasikan untuk `server_fd` agar koneksi baru dapat terdeteksi. Loop utama menggunakan `poll()` dengan timeout -1 (blocking tanpa batas waktu) untuk memantau semua file descriptor secara bersamaan. Jika `pfds[0]` aktif, ada koneksi baru yang perlu di-accept. Jika salah satu indeks lainnya aktif, `handle_message()` dipanggil untuk memproses data masuk dari client tersebut.
```c
int main() {
    signal(SIGINT, handle_sigint);
    start_time = time(NULL);

    // ...setup socket, bind, listen

    pfds[0].fd = server_fd;
    pfds[0].events = POLLIN;
    nfds = 0;

    log_message("System", "SERVER ONLINE");

    while (1) {
        int ready = poll(pfds, nfds + 1, -1);

        if (pfds[0].revents & POLLIN) {
            // terima koneksi baru → add_to_poll()
        }

        for (int i = 1; i <= nfds && ready > 0; i++) {
            if (pfds[i].revents & POLLIN) {
                handle_message(i);
                if (pfds[i].fd == -1) i--;
                ready--;
            }
        }
    }
}
```

_navi.c_
Signal handler client untuk Ctrl+C. Sebelum keluar, client mengirim perintah /exit ke server dan menunggu respons konfirmasi disconnect dari server. Setelah itu, flag running diset ke 0, socket ditutup, dan program keluar.
```c
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
```
Thread yang berjalan di latar belakang dan terus menunggu pesan dari server. Setiap pesan yang diterima langsung ditampilkan ke layar. Jika server terputus `(recv() ≤ 0)` atau pesan mengandung kata "Disconnecting", thread berhenti dengan mengeset running = 0.
```c
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
```
Thread yang membaca input dari pengguna melalui stdin dan mengirimkannya ke server. Prompt >  ditampilkan sebelum setiap input. Jika pengguna mengetik /exit, thread mengirim perintah tersebut ke server lalu berhenti dengan mengeset running = 0.
```c
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
```
Di `main()`  membuat koneksi TCP ke server menggunakan alamat dari `protocol.h`. Sebelum masuk ke mode chat, client menjalani fase registrasi dalam loop sinkron, merespons prompt server untuk memasukkan nama atau password. Setelah registrasi berhasil, dua pthread dibuat, `recv_thread` untuk menerima pesan dan `send_thread` untuk mengirim input pengguna. Program menunggu kedua thread selesai sebelum menutup socket.
```c
int main() {
    signal(SIGINT, handle_sigint);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    // ...connect ke HOST:PORT

    printf("[SYSTEM] Connected to The Wired (%s:%d)\n", HOST, PORT);

    // Loop registrasi: kirim nama / password hingga masuk
    while (1) {
        // ...recv prompt dari server, kirim input
        if (strstr(buf, "Welcome to The Wired") ||
            strstr(buf, "THE KNIGHTS CONSOLE")) break;
        if (strstr(buf, "Connection refused")) { close(sock); exit(EXIT_FAILURE); }
    }

    pthread_t tid_recv, tid_send;
    pthread_create(&tid_recv, NULL, recv_thread, NULL);
    pthread_create(&tid_send, NULL, send_thread, NULL);

    pthread_join(tid_recv, NULL);
    pthread_join(tid_send, NULL);

    close(sock);
    return 0;
}
```

#### _Output_
- Saat server terhubung
<img width="1473" height="141" alt="Screenshot 2026-05-03 054450" src="https://github.com/user-attachments/assets/051537ae-69fb-49df-80da-fb777d51540b" />

- Saat 2 user tergabung
<img width="1474" height="169" alt="Screenshot 2026-05-03 054751" src="https://github.com/user-attachments/assets/e2ca89b9-6e3c-4f1b-99dd-7982929d5206" />
<img width="1474" height="168" alt="Screenshot 2026-05-03 054650" src="https://github.com/user-attachments/assets/cb2e94a3-092e-4fbc-885b-1020108e5960" />

- Saat user lain chat ke user alice
<img width="1461" height="125" alt="Screenshot 2026-05-03 054830" src="https://github.com/user-attachments/assets/91af1b1c-5602-4d7c-b2b9-df5c4511f68e" />
<img width="1472" height="141" alt="Screenshot 2026-05-03 054814" src="https://github.com/user-attachments/assets/3d94570e-0652-4dff-8248-13e79f93c14d" />

- Saat user disconnect
<img width="1473" height="186" alt="Screenshot 2026-05-03 055142" src="https://github.com/user-attachments/assets/6411aaac-3c36-409f-b465-c7e7b07180fd" />

- Saat admin login
<img width="1476" height="277" alt="Screenshot 2026-05-03 055258" src="https://github.com/user-attachments/assets/089e99e4-f474-437b-a4c1-eee0883e0c3f" />

- Isi file `history.log`
<img width="1463" height="253" alt="Screenshot 2026-05-03 055428" src="https://github.com/user-attachments/assets/8f75f045-5f77-455d-89f7-77d873a87de5" />
