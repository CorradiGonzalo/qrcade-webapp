-- QRcade — Tipo de placa (garra vs. cafetera)
-- ─────────────────────────────────────────────────────────────
-- Vamos a sumar placas de cafetera (firmware "ESP32 MPAY", heredado y
-- adaptado) a la misma flota que las garras. Son binarios TOTALMENTE
-- distintos — pines, protocolo, lógica de negocio — así que hace falta
-- una forma de que el backend jamás pueda ofrecerle a una placa el OTA
-- del tipo equivocado. Eso sería catastrófico en una cafetera: el relé
-- que habilita la botonera (GPIO25) dejaría de responder.
--
-- La placa declara su propio `kind` en el body del check-in (ver
-- api/device/checkin/route.ts). Si no lo manda (firmware viejo de
-- garra, que no conoce este campo), default 'garra' — compatibilidad
-- hacia atrás total con las placas ya desplegadas.
alter table public.devices
  add column if not exists kind text not null default 'garra' check (kind in ('garra', 'cafetera'));

comment on column public.devices.kind is 'Tipo de hardware/firmware: "garra" (máquina de peluches, flujo QR + fichas) o "cafetera" (ESP32 MPAY: relé habilitador de botonera + polling directo a MP). Lo declara la propia placa en su primer check-in.';

create index if not exists devices_kind_idx on public.devices(kind);

-- Cada versión de firmware publicada en el panel admin aplica a UN solo
-- kind. uploadFirmwareAction sólo inserta device_firmware_status para
-- placas del MISMO kind que la versión — nunca notifica (ni mucho menos
-- instala) un firmware de garra en una cafetera o viceversa.
alter table public.firmware_versions
  add column if not exists kind text not null default 'garra' check (kind in ('garra', 'cafetera'));

comment on column public.firmware_versions.kind is 'A qué tipo de placa aplica este .bin — ver devices.kind. Filtra a quién se le notifica/ofrece esta versión.';

create index if not exists firmware_versions_kind_idx on public.firmware_versions(kind);
