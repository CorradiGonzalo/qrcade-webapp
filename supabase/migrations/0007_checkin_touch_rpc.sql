-- QRcade — Bajar el consumo de "Log Ingestion" de Supabase en el check-in
-- de las placas.
--
-- El check-in corre cada 5s por placa (CHECKIN_INTERVAL_MS en el .ino) y
-- hasta ahora disparaba, siempre, estas consultas aparte contra Supabase:
--   1. select app_settings (factory_key_hash)  <- nunca se usaba para una
--      placa ya existente, sólo hacía falta para dar de alta una nueva
--   2. select devices by mac
--   3. update devices (last_seen_at / firmware_version / wifi_rssi)
--   4. select device_commands pendientes
--   5. select device_firmware_status
-- Cada una es un viaje HTTP aparte al API gateway de Supabase, y cada uno
-- genera su propia entrada de log — con 10 placas ya veníamos pasados del
-- 1GB/mes incluido en el plan Free (bastante antes de llegar a la escala
-- de cientos de placas que tenemos pensada).
--
-- Esta función junta los pasos 2 y 3 en un solo viaje (1 llamada RPC) sin
-- cambiar nada de lo que devuelven: sigue trayendo el last_seen_at
-- ANTERIOR (para detectar que la placa estuvo >20s sin avisar y loguear
-- la reconexión de WiFi, igual que antes) y hace el UPDATE en el mismo
-- paso. El paso 1 (factory_key) se saca por completo del check-in de una
-- placa ya existente — ver api/device/checkin/route.ts.
create or replace function public.touch_device_checkin(
  p_mac text,
  p_firmware_version text,
  p_wifi_rssi integer
)
returns table (
  device public.devices,
  previous_last_seen_at timestamptz
)
language plpgsql
security definer
set search_path = public
as $$
declare
  v_previous timestamptz;
  v_device public.devices;
begin
  select d.last_seen_at into v_previous
  from public.devices d
  where d.mac = p_mac;

  if not found then
    -- Placa que todavía no existe en la tabla: no devolvemos filas, el
    -- backend sigue con el camino de alta nueva (sí valida factory_key).
    return;
  end if;

  update public.devices
  set last_seen_at = now(),
      firmware_version = p_firmware_version,
      wifi_rssi = p_wifi_rssi
  where mac = p_mac
  returning public.devices.* into v_device;

  device := v_device;
  previous_last_seen_at := v_previous;
  return next;
end;
$$;

comment on function public.touch_device_checkin(text, text, integer) is
  'Check-in rápido de una placa EXISTENTE: lee last_seen_at anterior + actualiza last_seen_at/firmware_version/wifi_rssi en un solo viaje. No hace nada si la MAC no existe (la placa es nueva).';

-- Sólo lo llama el backend con el cliente admin (service role), que ya
-- bypassea RLS — no hace falta (ni conviene) exponerlo a anon/authenticated.
revoke all on function public.touch_device_checkin(text, text, integer) from public, anon, authenticated;
grant execute on function public.touch_device_checkin(text, text, integer) to service_role;
