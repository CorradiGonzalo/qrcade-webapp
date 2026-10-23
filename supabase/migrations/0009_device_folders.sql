-- QRcade — Carpetas para organizar máquinas en el panel
-- ─────────────────────────────────────────────────────────────
-- Árbol de carpetas que cada dueño arma a su gusto (ej: "Dino Mall" >
-- "Cafeteras" > placas sueltas). Puramente organizativo/visual — no
-- cambia ningún permiso, la propiedad real de cada placa sigue siendo
-- devices.owner_id.
--
-- El admin tiene SU PROPIO árbol aparte de los de cada usuario (filas
-- con owner_id = su propio profiles.id), que sí puede agrupar placas de
-- cualquier dueño — útil para organizar por ubicación física sin
-- importar de quién es cada máquina.
create table if not exists public.device_folders (
  id uuid primary key default gen_random_uuid(),
  owner_id uuid not null references public.profiles(id) on delete cascade,
  parent_id uuid references public.device_folders(id) on delete cascade,
  name text not null check (char_length(trim(name)) > 0),
  created_at timestamptz not null default now()
);

create index if not exists device_folders_owner_id_idx on public.device_folders(owner_id);
create index if not exists device_folders_parent_id_idx on public.device_folders(parent_id);

comment on table public.device_folders is 'Árbol de carpetas por dueño (incluido el admin, con su propio árbol) para organizar máquinas en el panel. Visual únicamente.';

-- Una máquina vive en, como mucho, una carpeta (como un archivo normal).
-- null = sin carpeta, aparece "suelta" en la raíz.
alter table public.devices
  add column if not exists folder_id uuid references public.device_folders(id) on delete set null;

create index if not exists devices_folder_id_idx on public.devices(folder_id);

alter table public.device_folders enable row level security;

create policy "device_folders: dueño o admin puede ver"
  on public.device_folders for select
  using (owner_id = auth.uid() or public.current_role_is_admin());

create policy "device_folders: dueño o admin puede crear"
  on public.device_folders for insert
  with check (owner_id = auth.uid() or public.current_role_is_admin());

create policy "device_folders: dueño o admin puede editar"
  on public.device_folders for update
  using (owner_id = auth.uid() or public.current_role_is_admin())
  with check (owner_id = auth.uid() or public.current_role_is_admin());

create policy "device_folders: dueño o admin puede borrar"
  on public.device_folders for delete
  using (owner_id = auth.uid() or public.current_role_is_admin());
