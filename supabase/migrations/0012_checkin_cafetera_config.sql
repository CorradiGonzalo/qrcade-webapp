-- QRcade — El check-in también le manda su config a una cafetera
-- ─────────────────────────────────────────────────────────────
-- Mismo mecanismo que ya usan comandos/combos/firmware (migración 0010):
-- en el mismo viaje del check-in, si la placa es kind='cafetera' y está
-- claimed, le mandamos también su config operativa (ver migración 0011)
-- más el Access Token y collector_id de SU DUEÑO (reusado de
-- profiles.mp_access_token/mp_user_id — la misma cuenta que ya carga
-- para las garras, no hace falta cargar nada de MP de nuevo). La placa
-- cachea esto en su NVS — si el backend está caído sigue con el último
-- valor bueno que tenía.
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
  firmware_pending jsonb,
  cafe_config jsonb
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
  v_cafe jsonb;
begin
  select d.last_seen_at into v_previous
  from public.devices d
  where d.mac = p_mac;

  if not found then
    return;
  end if;

  update public.devices
  set last_seen_at = now(),
      firmware_version = p_firmware_version,
      wifi_rssi = p_wifi_rssi
  where mac = p_mac
  returning public.devices.* into v_device;

  if v_device.status = 'claimed' then
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

    if v_device.kind = 'cafetera' then
      select jsonb_build_object(
        'mp_access_token', p.mp_access_token,
        'mp_collector_id', p.mp_user_id,
        'mp_external_reference', v_device.cafe_mp_external_reference,
        'pulso_min_us', v_device.cafe_pulso_min_us,
        'off_delay_ms', v_device.cafe_off_delay_ms,
        'clean_pulse_ms', v_device.cafe_clean_pulse_ms,
        'heat_on_min', v_device.cafe_heat_on_min,
        'heat_off_min', v_device.cafe_heat_off_min,
        'clean_every_n_cycles', v_device.cafe_clean_every_n_cycles,
        'bt_reset_cmd', v_device.cafe_bt_reset_cmd,
        'pulso_nivel_high', v_device.cafe_pulso_nivel_high
      )
      into v_cafe
      from public.profiles p
      where p.id = v_device.owner_id;
    else
      v_cafe := null;
    end if;
  else
    v_commands := '[]'::jsonb;
    v_combos := null;
    v_firmware := null;
    v_cafe := null;
  end if;

  device := v_device;
  previous_last_seen_at := v_previous;
  pending_commands := v_commands;
  combos := v_combos;
  firmware_pending := v_firmware;
  cafe_config := v_cafe;
  return next;
end;
$$;

comment on function public.touch_device_checkin(text, text, integer) is
  'Check-in completo de una placa EXISTENTE en un solo viaje: actualiza last_seen_at/firmware_version/wifi_rssi, reconoce comandos pendientes, y trae combos + firmware pendiente + (si es cafetera) su config operativa y credenciales de MP de su dueño. No hace nada si la MAC no existe (la placa es nueva).';

revoke all on function public.touch_device_checkin(text, text, integer) from public, anon, authenticated;
grant execute on function public.touch_device_checkin(text, text, integer) to service_role;
