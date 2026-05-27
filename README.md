# Pakan Otomatis

## Identitas Kelompok

```
Kelompok    2
Kelas       B

Dosen     : Dr. M. Agus Syamsul Arifin, S.St., M.Kom
```

| NIM       | Nama                    |
| --------- | ----------------------- |
| H1H024042 | Fachriel Yoga Wicaksono |
|           | Dimas                   |
| H1H024044 | Chaedar Ali Amrulloh    |
| H1H024045 | Bintang Nugraha Putra   |
| H1H024046 | M.Fawaz Akbar           |
|           | Gerard                  |

## Overview

Sebuah perangkat IoT berbasis ESP32 yang berfungsi untuk memberikan pakan otomatis sesuai dengan jadwal yang sudah diatur. `tambahna yog`

## Skematik

## Cara kerja

Langkah awal sistem akan melakukan inisialisasi perangkat pada ESP32 seperti LCD/OLED, koneksi Wi-Fi, dan interrupt pada push button. Setelah tahap inisialisasi selesai, sistem akan menjalankan sinkronisasi waktu terhadap timezone menggunakan NTP dan kemudian memuat jadwal pakan yang tersimpan pada memori lokal untuk ditampilkan pada LCD/OLED secara real-time.

Default state dari sistem ini membuat ESP32 akan terus berupaya memonitor waktu real-time dan melakukan perbandingan dengan jadwal perbandingan pakan yang telah diatur. Apabila perbandingan kedua waktu tersebut sama, sebuah servo motor yang menutup jalur pakan akan bergerak untuk membuka wadah pakan selama beberapa detik dan kemudian menutup kembali. Sistem kemudian memberikan sinyal output berupa HTTP request ke server untuk menyimpan data log hasil pakan.

Ketikan push button pada sistem ditekan, sistem akan masuk ke menu state yang akan menampilkan dua opsi pada layar yaitu untuk menambahkan atau menghapus jadwal. Navigasi pada menu dapat dilakukan menggunakan rotary encoder untuk memilih diantar kedua opsi tersebut, push button dapat ditekan kembali untuk melakukan konfirmasi pada sistem terhadap opsi yang dipilih. Pada opsi tambah jadwal, sistem akan menampilkan jam dan menit pada layar yang dapat dikonfigurasikan menggunakan rotary encoder. Pertama sistem akan membaca nilai jam terlebih dahulu, kemudian ketika button ditekan sistem akan membaca nilai untuk menit. Ketika button ditekan kembali sistem akan menyimpan nilai tersebut dan dijadikan jadwal baru untuk pakan. Ketika opsi hapus jadwal dipilih, sistem akan menampilkan konfirmasi pada layar dan user dapat mengkonfirmasi penghapusan dengan menekan push button tersebut.
