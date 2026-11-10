-- QRcade — Aviso de "pago acreditado" en la pantalla de la cafetera
-- ─────────────────────────────────────────────────────────────
-- La pantalla externa (ver api/display/[code]/qr) sólo hace polling de
-- sólo-lectura cada 15s — no se entera de un pago individual salvo que
-- compare algo. Este timestamp es justamente eso: se actualiza cada vez
-- que el webhook de Mercado Pago acredita una venta (QR principal O QR
-- de empleado/"QR oferta", sin distinguir cuál) y la pantalla lo usa
-- para detectar "cambió desde la última vez que lo vi" y mostrar un
-- cartel de "Pago acreditado" unos segundos antes de volver al QR.
--
-- A propósito es UN SOLO timestamp (no uno por QR): alcanza para que la
-- pantalla sepa "hubo un pago nuevo", no hace falta saber de cuál.
alter table public.devices
  add column if not exists last_payment_at timestamptz;

comment on column public.devices.last_payment_at is 'Último momento en que el webhook de MP acreditó un pago de esta máquina (QR principal o QR oferta) — lo usa la pantalla externa para mostrar un aviso de "Pago acreditado" sin tener que enterarse del pago individual.';
