-- QRcade — Terminar de juntar el check-in en UN solo viaje a Supabase
-- ─────────────────────────────────────────────────────────────
-- La migración 0007 juntó el select+update de la placa en un solo RPC,
-- pero el check-in (api/device/checkin/route.ts) seguía haciendo, en
-- TODO check-in de una placa claimed (así no haya nada pendiente):
--   1. select device_commands pendientes
--   2. select device_firmware_status + firmware_versions (join)
--   3. select device_ficha_combos (sólo si mode='combo')
-- Cada uno es otro viaje al API gateway de Supabase -> otra entrada de
-- log. Con el check-in cada 5s (CHECKIN_INTERVAL_MS) y 8 placas ya
-- reclamadas, eso es ~4 consultas × 17280 check-ins/placa/día × 8 placas
-- ≈ más de 500.000 consultas/día — y va a escalar linealmente con cada
-- placa nueva que sumemos (empezando por las 21 cafeteras).
--
-- Esta versión de touch_device_checkin trae TODO lo que el check-in
-- necesita en la misma llamada: comandos pendientes (y los reconoce ahí
-- mismo, atómico — antes era un select + un update aparte), combos (si
-- corresponde) y el firmware_version pendiente (si hay). El camino feliz
-- (nada pendiente) pasa de ~4 viajes a Supabase a 1 solo.
drop function if exists public.touch_device_checkin(text, text, integer);

create function public.touch_device_checkin(
  p_mac text,
  p_firmware_version text,
  p_wifi_rssi integer
)
returns table (
  device public.devices,
  previous_last_seen_at timestamptz,
  pending_commands jsonb,
  combos jsonb,
  firmware_pending jsonb
)
language plpgsql
security definer
set search_path = public
as $$
declare
  v_previous timestamptz;
  v_device public.devices;
  v_commands jsonb;
  v_combos jsonb;
  v_firmware jsonb;
begin
  select d.last_seen_at into v_previous
  from public.devices d
  where d.mac = p_mac;

  if not found then
    -- Placa que todavía no existe: no devolvemos filas, el backend sigue
    -- con el camino de alta nueva (sí valida factory_key, eso es aparte).
    return;
  end if;

  update public.devices
  set last_seen_at = now(),
      firmware_version = p_firmware_version,
      wifi_rssi = p_wifi_rssi
  where mac = p_mac
  returning public.devices.* into v_device;

  -- Todo lo de abajo sólo importa para una placa 'claimed' — para
  -- revoked/unclaimed el backend corta antes de usarlo (igual que antes).
  if v_device.status = 'claimed' then
    -- Comandos pendientes (restart / test_dispense / etc.): los traemos
    -- Y los marcamos reconocidos en el mismo UPDATE...RETURNING — la ESP
    -- los da por hechos apenas se los mandamos, no hay ack de vuelta, así
    -- que esto es exactamente lo mismo que select + update por separado,
    -- pero en un solo statement.
    with acked as (
      update public.device_commands
      set acknowledged_at = now()
      where device_id = v_device.id
        and acknowledged_at is null
      returning id, command, created_at
    )
    select coalesce(
      jsonb_agg(jsonb_build_object('id', id, 'command', command) order by created_at),
      '[]'::jsonb
    )
    into v_commands
    from acked;

    if v_device.mode = 'combo' then
      select coalesce(
        jsonb_agg(jsonb_build_object('monto', monto, 'fichas', fichas) order by monto),
        '[]'::jsonb
      )
      into v_combos
      from public.device_ficha_combos
      where device_id = v_device.id;
    else
      v_combos := null;
    end if;

    select jsonb_build_object(
      'status', dfs.status,
      'updated_at', dfs.updated_at,
      'firmware_version_id', dfs.firmware_version_id,
      'version', fv.version,
      'bin_url', fv.bin_url,
      'changelog', fv.changelog,
      'kind', fv.kind
    )
    into v_firmware
    from public.device_firmware_status dfs
    join public.firmware_versions fv on fv.id = dfs.firmware_version_id
    where dfs.device_id = v_device.id
      and dfs.status in ('accepted', 'downloading')
    order by dfs.updated_at desc
    limit 1;
  else
    v_commands := '[]'::jsonb;
    v_combos := null;
    v_firmware := null;
  end if;

  device := v_device;
  previous_last_seen_at := v_previous;
  pending_commands := v_commands;
  combos := v_combos;
  firmware_pending := v_firmware;
  return next;
end;
$$;

comment on function public.touch_device_checkin(text, text, integer) is
  'Check-in completo de una placa EXISTENTE en un solo viaje: actualiza last_seen_at/firmware_version/wifi_rssi, reconoce comandos pendientes, y trae combos + firmware pendiente (si la placa está claimed). No hace nada si la MAC no existe (la placa es nueva).';

revoke all on function public.touch_device_checkin(text, text, integer) from public, anon, authenticated;
grant execute on function public.touch_device_checkin(text, text, integer) to service_role;
