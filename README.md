# SISOP-3-2026-IT-056
## Laporan Resmi
**Praktikum Sistem Operasi 2026 Modul 3 - Thread, IPC, & RPC**  

---
**Nama : Aliya Rahmadina**  
**NRP : 5027251056**   

---
### _Soal 1_
#### _Deskripsi Soal_
Pada soal 1 diperintahkan untuk membangun sistem komunikasi jaringan bernama _The Wired_ yang terdiri dari sebuah server (`wired.c`) dan client (`navi.c`).

#### _Penjelasan Kode_
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


#### _Output_
