-- QRcade — Config operativa de las cafeteras, manejada desde la app
-- ─────────────────────────────────────────────────────────────
-- El firmware original "ESP32 MPAY" cargaba TODO esto (incluido el
-- Access Token de Mercado Pago) a mano, por placa, desde el portal
-- cautivo de WiFiManager — 21 placas, 21 veces tipeando lo mismo a mano,
-- sin poder tocar nada después sin ir físicamente hasta la máquina.
--
-- El firmware nuevo (kind='cafetera') sólo usa el portal cautivo para el
-- WiFi. Todo lo demás se configura acá y se lo lleva el propio check-in
-- (ver migración 0012 / resolverFirmwareUpdate en route.ts) — la placa
-- lo cachea en su NVS, así que si el backend está caído sigue
-- funcionando con el último valor que tenía.
--
-- El Access Token y el collector_id NO se guardan por placa: se
-- REUSAN de profiles.mp_access_token / mp_user_id, la misma cuenta de MP
-- que ya carga el dueño en "Mi cuenta" para las garras — una cafetera
-- más no pide cargar nada de MP de nuevo.
--
-- mp_external_reference SÍ es por placa (es lo que distingue los pagos
-- de una cafetera de los de otra dentro de la misma cuenta de MP) — se
-- genera solo, determinístico a partir de la MAC, para no depender de
-- que alguien lo tipee bien. OJO: para una placa que ya tenía pagos
-- funcionando con el firmware viejo (un external_reference tipeado a
-- mano, distinto), hay que recargar ese valor en la config de cobro de
-- Mercado Pago de esa cafetera puntual para que siga matcheando.
alter table public.devices
  add column if not exists cafe_mp_external_reference text
    generated always as ('CAFE' || regexp_replace(upper(mac), '[^A-Z0-9]', '', 'g')) stored,
  add column if not exists cafe_pulso_min_us integer not null default 50000,
  add column if not exists cafe_off_delay_ms integer not null default 500,
  add column if not exists cafe_clean_pulse_ms integer not null default 3000,
  add column if not exists cafe_heat_on_min integer not null default 20,
  add column if not exists cafe_heat_off_min integer not null default 5,
  add column if not exists cafe_clean_every_n_cycles integer not null default 50,
  add column if not exists cafe_bt_reset_cmd text not null default 'RESET847200',
  add column if not exists cafe_pulso_nivel_high boolean not null default true;

comment on column public.devices.cafe_mp_external_reference is 'external_reference que esta cafetera usa para que MP distinga sus pagos de los de otra máquina de la misma cuenta. Generado solo a partir de la MAC — ver comentario de la migración.';
comment on column public.devices.cafe_pulso_min_us is 'Duración mínima válida (µs) del pulso de "fin de ciclo" en GPIO27. Equivalente a pulseus del firmware original.';
comment on column public.devices.cafe_off_delay_ms is 'Delay (ms) antes de apagar el relé de habilitación (GPIO25) tras el fin de ciclo. Equivalente a offdly.';
comment on column public.devices.cafe_clean_pulse_ms is 'Duración (ms) de cada uno de los 2 pulsos del ciclo de limpieza automática. Equivalente a cleanpms.';
comment on column public.devices.cafe_heat_on_min is 'Minutos que el calentador permanece encendido por ciclo. Equivalente a heaton.';
comment on column public.devices.cafe_heat_off_min is 'Minutos que el calentador permanece apagado por ciclo. Equivalente a heatoff.';
comment on column public.devices.cafe_clean_every_n_cycles is 'Cada cuántos créditos usados se dispara la limpieza automática. Equivalente a clncyc.';
comment on column public.devices.cafe_bt_reset_cmd is 'Texto que hay que mandar por BLE (Nordic UART Service) para autorizar un reset de fábrica remoto. Equivalente a btrstcmd.';
comment on column public.devices.cafe_pulso_nivel_high is 'Nivel activo esperado del pulso de entrada en GPIO27 (true=HIGH, false=LOW). Equivalente a pulslvl.';
