-- QRcade — Registro de actividad por placa (log visible en el panel)
-- ─────────────────────────────────────────────────────────────
-- device_events: línea de tiempo simple de eventos relevantes por placa
-- (reconexión de WiFi, disparo de relé, etc.). Siempre lo escribe el
-- backend con el cliente admin — nunca lo inserta la ESP directamente.
-- ─────────────────────────────────────────────────────────────
create table if not exists public.device_events (
  id uuid primary key default gen_random_uuid(),
  device_id uuid not null references public.devices(id) on delete cascade,
  type text not null check (
    type in ('wifi_reconnect', 'dispense_test', 'dispense_payment', 'restart_requested')
  ),
  message text not null,
  created_at timestamptz not null default now()
);

create index if not exists device_events_device_id_created_at_idx
  on public.device_events(device_id, created_at desc);

comment on table public.device_events is 'Línea de tiempo de eventos por placa (reconexión WiFi, disparo de relé de prueba o por pago, reinicio solicitado). Puramente informativo, no reemplaza a mp_payments.';

alter table public.device_events enable row level security;

create policy "device_events: dueño o admin puede ver"
  on public.device_events for select
  using (
    public.current_role_is_admin()
    or exists (
      select 1 from public.devices d
      where d.id = device_events.device_id and d.owner_id = auth.uid()
    )
  );

-- El insert siempre lo hace el backend con el cliente admin (service role),
-- que bypassea RLS — no hace falta una policy de insert para el cliente.
