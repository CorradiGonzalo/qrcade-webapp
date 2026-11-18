-- ─────────────────────────────────────────────────────────────
-- "Operadores" — sub-cuentas que el DUEÑO de una máquina (no un
-- admin) crea desde el panel de esa máquina puntual. Un operador:
--   - Inicia sesión como cualquier usuario (mismo flujo de
--     usuario/contraseña, mismo "cambiar contraseña" en el primer
--     ingreso).
--   - NO ve el dashboard normal: su panel es una pantalla mínima con
--     un solo botón "Ficha gratis", atado a UNA máquina puntual
--     (operator_device_id) — nunca ve otras máquinas, nunca puede
--     configurar cobro ni nada más.
--   - "Ficha gratis" dispara el MISMO comando que ya usa el botón
--     "Test" del dueño (device_commands.command = 'test_dispense') —
--     cero cambios de firmware, ni de garra ni de cafetera.
--   - El dueño puede desactivarlo en cualquier momento
--     (operator_active = false) sin borrar la cuenta ni perder el
--     historial de "quién dio qué ficha" en device_events.
-- ─────────────────────────────────────────────────────────────

alter table public.profiles
  drop constraint if exists profiles_role_check;

alter table public.profiles
  add constraint profiles_role_check
  check (role in ('admin', 'usuario', 'operador'));

alter table public.profiles
  add column if not exists operator_device_id uuid references public.devices(id) on delete set null,
  add column if not exists operator_active boolean not null default true;

comment on column public.profiles.operator_device_id is 'Sólo para role=operador: la ÚNICA máquina a la que esta cuenta tiene acceso (botón "Ficha gratis"). Null para cualquier otro rol. Si la máquina se borra, queda null (no tiene sentido mantener la cuenta atada a nada), pero la fila de profiles no se borra sola -- el dueño la ve como huérfana y decide.';
comment on column public.profiles.operator_active is 'El dueño la desactiva/reactiva desde el panel de la máquina sin borrar la cuenta (mismo criterio que devices.status=revoked para placas). Un operador con operator_active=false sigue pudiendo iniciar sesión pero su pantalla le avisa que no tiene acceso -- no puede dar fichas.';

-- device_events: nuevo tipo para que quede un registro de quién dio
-- cada ficha gratis (ej. "Kevin dio una ficha gratis."), visible en la
-- misma línea de tiempo que ya ve el dueño.
alter table public.device_events
  drop constraint if exists device_events_type_check;

alter table public.device_events
  add constraint device_events_type_check
  check (type in (
    'wifi_reconnect', 'dispense_test', 'dispense_payment', 'restart_requested',
    'firmware_update', 'clean_cycle', 'restart_reason', 'ficha_gratis'
  ));
