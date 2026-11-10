-- QRcade — Mejoras a la cafetera basadas en el análisis del firmware
-- original de otra placa (ver ESP32_MPAY_Analisis / sesión de reversing):
-- ─────────────────────────────────────────────────────────────────────
-- 1) `is_cleaning`: la placa avisa cuando está en medio de un ciclo de
--    limpieza, para que la pantalla externa (ver api/display/[code]/qr)
--    pueda ocultar el QR mientras tanto — nadie debería poder pagar (y no
--    recibir su café) mientras la máquina está limpiando.
-- 2) `cafe_mp_check_interval_ms` / `cafe_clean_gap_ms`: el firmware
--    original tenía estos dos tiempos como configurables (no fijos en el
--    código) — hacemos lo mismo que ya hicimos con pulso/delay/heat/etc.
--    en la migración 0011, para poder ajustarlos sin reflashear.
-- 3) Nuevo tipo de evento 'restart_reason' para loguear por qué se
--    reinició la placa la última vez (heap bajo, WiFi caído, OTA,
--    comando remoto, factory reset, timeout de portal) — mismo patrón
--    que 'clean_cycle' en la migración 0013.
alter table public.devices
  add column if not exists is_cleaning boolean not null default false,
  add column if not exists cafe_mp_check_interval_ms integer not null default 10000,
  add column if not exists cafe_clean_gap_ms integer not null default 20000;

comment on column public.devices.is_cleaning is 'true mientras la cafetera está en medio de un ciclo de limpieza (automática o manual) — la placa lo manda en cada check-in. La pantalla externa lo usa para ocultar el QR y no aceptar pagos mientras dura.';
comment on column public.devices.cafe_mp_check_interval_ms is 'Cada cuánto consulta la placa directamente a la API de Mercado Pago (polling propio, independiente del webhook). Configurable para poder subirlo si en el futuro hay rate-limit por compartir el mismo Access Token entre varias cafeteras.';
comment on column public.devices.cafe_clean_gap_ms is 'Espera entre el primer y el segundo pulso del ciclo de limpieza automático/manual (el original lo tenía como parámetro, no fijo).';

alter table public.device_events
  drop constraint if exists device_events_type_check;

alter table public.device_events
  add constraint device_events_type_check
  check (type in ('wifi_reconnect', 'dispense_test', 'dispense_payment', 'restart_requested', 'firmware_update', 'clean_cycle', 'restart_reason'));
