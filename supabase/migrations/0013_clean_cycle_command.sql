-- QRcade — Botón de "limpieza manual" para cafeteras
-- ─────────────────────────────────────────────────────────────
-- El firmware original de la cafetera dispara el ciclo de limpieza solo
-- (2 pulsos de GPIO25 separados por 20s) cada N créditos usados. El
-- dueño pidió poder dispararlo también a mano desde el panel — mismo
-- mecanismo que "Test" y "Reiniciar" (device_commands + check-in), sólo
-- hace falta sumar el tipo de evento para loguearlo.
alter table public.device_events
  drop constraint if exists device_events_type_check;

alter table public.device_events
  add constraint device_events_type_check
  check (type in ('wifi_reconnect', 'dispense_test', 'dispense_payment', 'restart_requested', 'firmware_update', 'clean_cycle'));
