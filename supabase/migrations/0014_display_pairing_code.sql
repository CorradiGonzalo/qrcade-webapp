-- QRcade — Código de emparejamiento para una pantalla externa
-- ─────────────────────────────────────────────────────────────
-- Caso puntual: algunas cafeteras (las primeras 20, armadas con una
-- placa soldada sin pantalla) van a usar una SEGUNDA ESP32, con una
-- pantallita, que sólo muestra el QR — la placa que cobra y habilita
-- la botonera sigue siendo exactamente la misma de siempre, sin tocar
-- nada de su lógica. (Las ~100 cafeteras que vienen después van con
-- una sola placa con pantalla integrada, sin necesitar esto.)
--
-- Para que esa segunda placa pueda pedir el QR sin que el sistema la
-- trate como una máquina nueva (y así no le aparezca duplicada a nadie
-- en el panel), se identifica con este código — distinto del
-- claim_code, que es el que vincula la placa real a un dueño. Este
-- código sólo sirve para leer el QR vigente de una máquina YA
-- reclamada, nunca para reclamarla ni para mandarle comandos.
alter table public.devices
  add column if not exists display_pairing_code text unique;

comment on column public.devices.display_pairing_code is 'Código opcional para que una ESP32 "sólo pantalla" (ver api/display/[code]/qr) pida el QR vigente de esta máquina sin ser tratada como una placa nueva. No sirve para reclamar la placa ni para mandarle comandos — sólo lectura del QR.';
