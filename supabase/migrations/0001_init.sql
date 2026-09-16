-- QRcade Fleet Manager — esquema inicial
-- Pensado para correrse en el SQL editor de Supabase (o via `supabase db push`)

create extension if not exists pgcrypto;

-- ─────────────────────────────────────────────────────────────
-- profiles: 1 fila por usuario (admin o dueño de máquinas)
-- ─────────────────────────────────────────────────────────────
create table if not exists public.profiles (
  id uuid primary key references auth.users(id) on delete cascade,
  username text unique not null,
  role text not null check (role in ('admin', 'usuario')) default 'usuario',
  must_change_password boolean not null default true,
  recovery_email text,
  display_name text,
  created_at timestamptz not null default now()
);

comment on table public.profiles is 'Extiende auth.users con rol, username de login y flag de primer ingreso.';

-- ─────────────────────────────────────────────────────────────
-- app_settings: pares clave/valor para config global (ej. hash de la
-- clave de fábrica que valida el alta automática de una ESP)
-- ─────────────────────────────────────────────────────────────
create table if not exists public.app_settings (
  key text primary key,
  value text not null,
  updated_at timestamptz not null default now()
);

-- ─────────────────────────────────────────────────────────────
-- devices: una fila por placa ESP32
-- ─────────────────────────────────────────────────────────────
create table if not exists public.devices (
  id uuid primary key default gen_random_uuid(),
  mac text unique not null,
  claim_code text unique,                      -- código de 6 caracteres, sólo mientras status='unclaimed'
  alias text,
  owner_id uuid references public.profiles(id) on delete set null,
  status text not null check (status in ('unclaimed', 'claimed', 'revoked')) default 'unclaimed',

  -- estado operativo reportado por la ESP
  firmware_version text,
  last_seen_at timestamptz,
  wifi_rssi integer,
  is_paused boolean not null default false,

  -- configuración de cobro — la carga el dueño, no el admin
  mp_access_token text,
  mp_caja_name text,
  mp_monto_fijo numeric(10, 2),

  created_at timestamptz not null default now(),
  claimed_at timestamptz,
  revoked_at timestamptz
);

create index if not exists devices_owner_id_idx on public.devices(owner_id);
create index if not exists devices_status_idx on public.devices(status);

comment on column public.devices.mac is 'MAC de la ESP32, identificador físico único.';
comment on column public.devices.claim_code is 'Código corto que la ESP muestra en pantalla para que el dueño la vincule desde la app.';

-- ─────────────────────────────────────────────────────────────
-- device_registration_attempts: auditoría de intentos de alta
-- (útil para ver intentos rechazados por clave de fábrica inválida)
-- ─────────────────────────────────────────────────────────────
create table if not exists public.device_registration_attempts (
  id uuid primary key default gen_random_uuid(),
  mac text,
  accepted boolean not null,
  reason text,
  created_at timestamptz not null default now()
);

-- ─────────────────────────────────────────────────────────────
-- firmware_versions: binarios subidos por el admin
-- ─────────────────────────────────────────────────────────────
create table if not exists public.firmware_versions (
  id uuid primary key default gen_random_uuid(),
  version text unique not null,
  bin_url text not null,
  changelog text,
  created_by uuid references public.profiles(id),
  created_at timestamptz not null default now()
);

-- ─────────────────────────────────────────────────────────────
-- device_firmware_status: adopción de cada versión por placa
-- ─────────────────────────────────────────────────────────────
create table if not exists public.device_firmware_status (
  device_id uuid not null references public.devices(id) on delete cascade,
  firmware_version_id uuid not null references public.firmware_versions(id) on delete cascade,
  status text not null check (status in ('notified', 'accepted', 'downloading', 'updated', 'dismissed', 'failed')) default 'notified',
  updated_at timestamptz not null default now(),
  primary key (device_id, firmware_version_id)
);

-- ─────────────────────────────────────────────────────────────
-- device_commands: cola simple de comandos (reiniciar) que la ESP
-- consume y borra/marca en su próximo check-in
-- ─────────────────────────────────────────────────────────────
create table if not exists public.device_commands (
  id uuid primary key default gen_random_uuid(),
  device_id uuid not null references public.devices(id) on delete cascade,
  command text not null check (command in ('restart')),
  created_by uuid references public.profiles(id),
  created_at timestamptz not null default now(),
  acknowledged_at timestamptz
);

-- ═════════════════════════════════════════════════════════════
-- Helper: rol del usuario autenticado (evita recursión en policies)
-- ═════════════════════════════════════════════════════════════
create or replace function public.current_role_is_admin()
returns boolean
language sql
stable
security definer
set search_path = public
as $$
  select exists (
    select 1 from public.profiles
    where id = auth.uid() and role = 'admin'
  );
$$;

-- ═════════════════════════════════════════════════════════════
-- Helper de login: resolver el email interno a partir del username
-- (Supabase Auth exige email; el admin sólo maneja "usuario y clave")
-- ═════════════════════════════════════════════════════════════
create or replace function public.email_for_username(p_username text)
returns text
language sql
stable
security definer
set search_path = public, auth
as $$
  select u.email
  from public.profiles p
  join auth.users u on u.id = p.id
  where p.username = lower(p_username)
  limit 1;
$$;

grant execute on function public.email_for_username(text) to anon, authenticated;

-- ═════════════════════════════════════════════════════════════
-- Row Level Security
-- ═════════════════════════════════════════════════════════════
alter table public.profiles enable row level security;
alter table public.devices enable row level security;
alter table public.device_registration_attempts enable row level security;
alter table public.firmware_versions enable row level security;
alter table public.device_firmware_status enable row level security;
alter table public.device_commands enable row level security;
alter table public.app_settings enable row level security;

-- profiles: cada uno ve/edita su propia fila; admin ve todas.
-- Las altas de usuario las hace el admin vía API route con service role
-- (necesita crear el auth.users también), no un insert directo del cliente.
create policy "profiles: ver la propia o todas si admin"
  on public.profiles for select
  using (id = auth.uid() or public.current_role_is_admin());

create policy "profiles: actualizar la propia"
  on public.profiles for update
  using (id = auth.uid())
  with check (id = auth.uid());

-- devices: el dueño ve/edita las suyas; admin ve/edita todas.
-- El alta (insert) y el claim los maneja el backend con service role.
create policy "devices: dueño o admin puede ver"
  on public.devices for select
  using (owner_id = auth.uid() or public.current_role_is_admin());

create policy "devices: dueño puede actualizar su config de cobro"
  on public.devices for update
  using (owner_id = auth.uid() or public.current_role_is_admin())
  with check (owner_id = auth.uid() or public.current_role_is_admin());

-- firmware_versions: cualquier usuario autenticado puede leer (para ver
-- si hay una versión nueva); sólo admin sube (vía API route).
create policy "firmware_versions: lectura autenticada"
  on public.firmware_versions for select
  using (auth.uid() is not null);

-- device_firmware_status: visible para el dueño de la placa o el admin
create policy "device_firmware_status: dueño o admin"
  on public.device_firmware_status for select
  using (
    public.current_role_is_admin()
    or exists (
      select 1 from public.devices d
      where d.id = device_firmware_status.device_id and d.owner_id = auth.uid()
    )
  );

create policy "device_firmware_status: dueño puede actualizar su decisión"
  on public.device_firmware_status for update
  using (
    exists (
      select 1 from public.devices d
      where d.id = device_firmware_status.device_id and d.owner_id = auth.uid()
    )
  );

-- device_commands: el dueño puede crear/ver comandos de sus placas
create policy "device_commands: dueño o admin puede ver"
  on public.device_commands for select
  using (
    public.current_role_is_admin()
    or exists (
      select 1 from public.devices d
      where d.id = device_commands.device_id and d.owner_id = auth.uid()
    )
  );

create policy "device_commands: dueño puede crear"
  on public.device_commands for insert
  with check (
    exists (
      select 1 from public.devices d
      where d.id = device_commands.device_id and d.owner_id = auth.uid()
    )
  );

-- device_registration_attempts y app_settings: sólo admin (lectura;
-- la escritura la hace siempre el backend con service role)
create policy "device_registration_attempts: sólo admin lee"
  on public.device_registration_attempts for select
  using (public.current_role_is_admin());

create policy "app_settings: sólo admin lee"
  on public.app_settings for select
  using (public.current_role_is_admin());
