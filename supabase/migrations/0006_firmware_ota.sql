-- Habilita el flujo real de actualización remota (OTA): hasta ahora
-- "aceptar" una versión en el panel sólo cambiaba una fila de estado, pero
-- nada le avisaba a la placa que había algo para bajar. Ahora el check-in
-- (api/device/checkin) le manda la URL del .bin cuando el dueño aceptó una
-- versión, y la ESP la descarga y se flashea sola (ver HTTPUpdate en el
-- .ino). Sólo hace falta un tipo de evento nuevo para loguear esto en el
-- panel — el resto de la lógica vive en código, no en el esquema.
alter table public.device_events
  drop constraint if exists device_events_type_check;

alter table public.device_events
  add constraint device_events_type_check
  check (type in ('wifi_reconnect', 'dispense_test', 'dispense_payment', 'restart_requested', 'firmware_update'));
