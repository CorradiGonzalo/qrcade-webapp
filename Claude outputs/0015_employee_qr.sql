-- QRcade — "QR de empleado" para cafeteras
-- ─────────────────────────────────────────────────────────────
-- Pedido: además del QR principal (para clientes, sin cambios de
-- ningún tipo), la pantalla de la cafetera va a poder mostrar un
-- SEGUNDO QR, de uso interno para empleados, con un precio más bajo.
-- Se destapa tocando la pantalla y escribiendo un PIN numérico
-- configurable desde el panel — ver firmware
-- qrcade_cafetera_display_v2.0.0 y api/display/[code]/qr.
--
-- Decisiones clave (todas a pedido explícito del dueño):
--   - Mismo mecanismo de cobro que el principal (Store → Caja → Orden
--     de monto fijo de Mercado Pago), nunca otro sistema ni "gratis".
--   - NO se pide un local nuevo: el QR de empleado SIEMPRE usa el
--     mismo `local_name`/`store_id` que ya tiene configurado el QR
--     principal de esa placa — se reutiliza, nunca se vuelve a
--     preguntar. Si el QR principal todavía no está aprovisionado, no
--     hay de dónde sacar un local, así que el QR de empleado tampoco
--     se puede crear (lo valida el server action).
--   - Sólo se pide nombre de caja + monto, con la misma semántica de
--     find-or-create ("si existe la usa, si no existe la crea") que
--     ya tiene el QR principal.
--
-- Todo esto es ADITIVO: ninguna columna ni lógica del QR principal
-- (local_name, caja_name, mp_monto_fijo, store_id, pos_id, qr_data,
-- provisioned_at, cafe_mp_external_reference) se toca ni se renombra.
alter table public.devices
  add column if not exists empleado_pin text,
  add column if not exists empleado_caja_name text,
  add column if not exists empleado_mp_monto_fijo numeric,
  add column if not exists empleado_store_id text,
  add column if not exists empleado_pos_id text,
  add column if not exists empleado_qr_data text,
  add column if not exists empleado_provisioned_at timestamptz,
  -- Mismo patrón que cafe_mp_external_reference (migración 0011): un
  -- external_reference determinístico a partir de la MAC, pero con un
  -- prefijo distinto ('CAFEEMP' en vez de 'CAFE') para que nunca pueda
  -- colisionar con el de la caja principal de la misma placa.
  add column if not exists cafe_mp_external_reference_empleado text
    generated always as ('CAFEEMP' || regexp_replace(upper(mac), '[^A-Z0-9]', '', 'g')) stored;

comment on column public.devices.empleado_pin is 'PIN numérico (4-6 dígitos) que la pantalla de la cafetera pide para destapar el QR de empleado. Null = función desactivada, la pantalla nunca ofrece el QR de empleado.';
comment on column public.devices.empleado_caja_name is 'Nombre de la caja/POS de Mercado Pago del QR de empleado (find-or-create, igual que caja_name del QR principal).';
comment on column public.devices.empleado_mp_monto_fijo is 'Monto fijo del QR de empleado — siempre modo "fijo", nunca combos.';
comment on column public.devices.empleado_store_id is 'store_id de Mercado Pago usado por el QR de empleado — siempre el MISMO store_id que ya tiene el QR principal (devices.store_id), nunca uno nuevo.';
comment on column public.devices.empleado_pos_id is 'external_id de la caja de Mercado Pago del QR de empleado (= cafe_mp_external_reference_empleado para cafeteras). Es contra lo que matchea el webhook para distinguir un pago de empleado de uno de cliente.';
comment on column public.devices.empleado_qr_data is 'QR estático (EMV) de la caja de empleado, el que la pantalla muestra durante los 30s después de un PIN correcto.';
comment on column public.devices.empleado_provisioned_at is 'Último momento en que se aprovisionó/re-armó el QR de empleado en Mercado Pago.';
comment on column public.devices.cafe_mp_external_reference_empleado is 'external_reference determinístico (a partir de la MAC) de la caja de empleado de esta cafetera — ver comentario de la migración. Nunca coincide con cafe_mp_external_reference (QR principal).';
