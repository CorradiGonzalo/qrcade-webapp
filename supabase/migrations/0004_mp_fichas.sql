-- QRcade — Rediseño de integración con Mercado Pago
-- ─────────────────────────────────────────────────────────────
-- Token a nivel de CUENTA (no por placa): cada dueño carga su Access
-- Token una sola vez; cada placa después sólo elige local/caja/modo.
-- ─────────────────────────────────────────────────────────────
alter table public.profiles
  add column if not exists mp_access_token text,
  add column if not exists mp_user_id text;

comment on column public.profiles.mp_access_token is 'Access Token de la cuenta de Mercado Pago del dueño. El dinero de cada cobro va directo a esa cuenta.';
comment on column public.profiles.mp_user_id is 'user_id de MP asociado al token de arriba — se usa para identificar al dueño cuando llega un webhook de pago.';

-- ─────────────────────────────────────────────────────────────
-- devices: se va el token por placa (ahora vive en profiles), se
-- agregan local/caja/modo y los datos que arma el aprovisionamiento
-- contra la API de MP.
-- ─────────────────────────────────────────────────────────────
alter table public.devices
  drop column if exists mp_access_token;

alter table public.devices
  rename column mp_caja_name to caja_name;

alter table public.devices
  add column if not exists local_name text,
  add column if not exists mode text not null default 'fijo' check (mode in ('fijo', 'combo')),
  add column if not exists store_id text,
  add column if not exists pos_id text,
  add column if not exists qr_data text,
  add column if not exists provisioned_at timestamptz;

comment on column public.devices.mode is '''fijo'': QR bloqueado a mp_monto_fijo (1 pago = 1 ficha). ''combo'': QR de monto abierto, las fichas se asignan según device_ficha_combos por monto exacto.';
comment on column public.devices.pos_id is 'external_id de la Caja/POS en Mercado Pago — es el external_reference que va a traer cada pago de esta placa.';
comment on column public.devices.qr_data is 'Payload del QR dinámico de MP, cacheado la última vez que se aprovisionó — esto es lo que la ESP dibuja, sin llamar a MP.';

-- ─────────────────────────────────────────────────────────────
-- device_ficha_combos: lista fija de combos {fichas, monto} por placa,
-- sólo aplica cuando devices.mode = 'combo'
-- ─────────────────────────────────────────────────────────────
create table if not exists public.device_ficha_combos (
  id uuid primary key default gen_random_uuid(),
  device_id uuid not null references public.devices(id) on delete cascade,
  fichas integer not null check (fichas > 0),
  monto numeric(10, 2) not null check (monto > 0),
  created_at timestamptz not null default now(),
  unique (device_id, monto)
);

comment on table public.device_ficha_combos is 'Combos de "fichas por monto abierto": si el pago coincide EXACTO con `monto`, se acreditan `fichas`. Sin coincidencia exacta no se acredita nada.';

-- ─────────────────────────────────────────────────────────────
-- mp_payments: auditoría + dedupe de pagos ya procesados (los webhooks
-- de MP se pueden reenviar más de una vez para el mismo pago)
-- ─────────────────────────────────────────────────────────────
create table if not exists public.mp_payments (
  id uuid primary key default gen_random_uuid(),
  device_id uuid references public.devices(id) on delete set null,
  mp_payment_id text unique not null,
  amount numeric(10, 2),
  fichas_dispensed integer,
  status text,
  created_at timestamptz not null default now()
);

-- ─────────────────────────────────────────────────────────────
-- device_commands: se habilita el comando "dispense:N" (N = cantidad de
-- fichas), generado por el webhook de pago — además de los que ya había
-- ─────────────────────────────────────────────────────────────
alter table public.device_commands
  drop constraint if exists device_commands_command_check;

alter table public.device_commands
  add constraint device_commands_command_check
  check (command in ('restart', 'test_dispense') or command like 'dispense:%');

-- ═════════════════════════════════════════════════════════════
-- Row Level Security
-- ═════════════════════════════════════════════════════════════
alter table public.device_ficha_combos enable row level security;
alter table public.mp_payments enable row level security;

create policy "device_ficha_combos: dueño o admin puede ver"
  on public.device_ficha_combos for select
  using (
    public.current_role_is_admin()
    or exists (
      select 1 from public.devices d
      where d.id = device_ficha_combos.device_id and d.owner_id = auth.uid()
    )
  );

create policy "device_ficha_combos: dueño puede crear"
  on public.device_ficha_combos for insert
  with check (
    exists (
      select 1 from public.devices d
      where d.id = device_ficha_combos.device_id and d.owner_id = auth.uid()
    )
  );

create policy "device_ficha_combos: dueño puede borrar"
  on public.device_ficha_combos for delete
  using (
    exists (
      select 1 from public.devices d
      where d.id = device_ficha_combos.device_id and d.owner_id = auth.uid()
    )
  );

-- mp_payments: sólo lectura para el dueño de la placa (o admin); el
-- insert/update siempre lo hace el webhook con el cliente admin.
create policy "mp_payments: dueño o admin puede ver"
  on public.mp_payments for select
  using (
    public.current_role_is_admin()
    or exists (
      select 1 from public.devices d
      where d.id = mp_payments.device_id and d.owner_id = auth.uid()
    )
  );
